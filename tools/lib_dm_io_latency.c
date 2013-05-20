#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <ctype.h>

#define IO_LATENCY_THRESHOLD_CONFIG	"/etc/sysconfig/io_latency_threshold"

#define DEBUG

static int check_parameters(const char *target_name,
			   int latency_threshold,
			   int latency_warning_nr)
{
	struct stat stat_buf;
	char *name, *dev;
	char path[512] = {0,};
	int ret;

	if (latency_threshold < 0 ||
	    latency_warning_nr < 0) {
		ret = -EINVAL;
		goto out;
	}

	name = strdup(target_name);
	if (!name)
		goto out;

	dev = basename(name);
	snprintf(path, sizeof(path), "/dev/%s", dev);
	ret = stat(path, &stat_buf);
	if (ret < 0)
		goto out_memory;


	if (!S_ISBLK(stat_buf.st_mode)) {
		ret = -ENOTBLK;
		goto out_memory;
	}

out_memory:
	free(name);
out:
	return ret;

}

struct latency_record {
	int start;
	int length;
	unsigned long nr;
};

static int load_system_default_configs(int *latency_threshold,
				int *latency_warning_nr)
{
	int fd;
	int ret = -1;
	long int val;
	char buf[512], *start, *end;
	fd = open(IO_LATENCY_THRESHOLD_CONFIG,
		  O_RDONLY);
	if (fd < 0)
		goto out;
	ret = read(fd, buf, sizeof(buf));
	if (ret < 0)
		goto out_close_file;
	if (ret >= (sizeof(buf) - 1)) {
		printf("config file is too large\n");
		ret = -1;
		goto out_close_file;
	}

	/* in restricted threshold:number format */

	/* scan threshold */
	start = buf;
	while(*start) {
		if (isdigit(*start))
			break;
		start ++;
	}
	val = strtol(start, &end, 10);
	if ((val == LONG_MIN) ||
	    (val == LONG_MAX) ||
	    (val <= 0)) {
		ret = -1;
		goto out_close_file;
	}
	start = end;
	while (*start) {
		if (*start == ':') {
			start ++;
			break;
		}
		start ++;
	}
	if (!(*start)) {
		ret = -1;
		goto out_close_file;
	}
	*latency_threshold = (int)val;

	/* scan number */
	val = strtol(start, NULL, 10);
	if (val == LONG_MIN || val == LONG_MAX || val <= 0) {
		ret = -1;
		goto out_close_file;
	}
	*latency_warning_nr = (int)val;
	ret = 0;

out_close_file:
	close(fd);
out:
	return ret;
}


static int open_dm_target_latency_profile(const char *target_name)
{
	char *name;
	char *ptr, *dev;
	char path[512] = {0, };
	int fd = -1;

	name = strdup(target_name);
	if (!name)
		goto out;
	ptr = name;
	while (*ptr) {
		if (!isspace(*ptr))
			break;
		ptr ++;
	}
	if (!(*ptr))
		goto out;

	dev = basename(ptr);
	snprintf(path, sizeof(path), "/sys/block/%s/dm/io_latency_ms", dev);
	fd = open(path, O_RDONLY);

out:
	if (name)
		free(name);
	return fd;
}

static void close_dm_target_latency_profile(int *fd)
{
	close(*fd);
	*fd = -1;
}

/* latency stats in format like:
 * 0-9(ms):100
 *
 * we can use ':' in the format to determine number of record lines
 */
static int load_dm_latency_stats(int fd,
			  struct latency_record **records,
			  unsigned long *levels)
{
	char buf[4096];
	char *start, *end, *ptr;
	int i, ret;
	int level_start, level_end;
	unsigned long nr;
	

	ret = read(fd, buf, sizeof(buf));
	/* is file too large? */
	if (ret == sizeof(buf)) {
		ret = -1;
		goto out;
	}
	
	/* scan number of record lines */
	ptr = buf;
	i = 0;
	while ((*ptr) && ((ptr - buf) < sizeof(buf))) {
		start = strchr(ptr, ':');
		if (!start)
			break;
		ptr = start + 1;
		i ++;
	}

	if (i == 0) {
		ret = -1;
		goto out;
	}

	*levels = i;
	*records = malloc(sizeof(struct latency_record) * i);
	if ((*records) == NULL) {
		ret = -1;
		goto out;
	}

	start = buf;
	i = 0;
	while((*start) != '\0') {
		end = start + 1;
		while ((*end) && (*end) != '\n')
			end ++;
		if (!(*end))
			break;

		*end = '\0';
		ret = sscanf(start, "%d-%d(ms):%lu",
			     &level_start, &level_end, &nr);
#ifdef DEBUG
		printf("%d-%d(ms):%lu\n", level_start, level_end, nr);
#endif
		(*records)[i].start = level_start;
		(*records)[i].length = level_end - level_start + 1;
		(*records)[i].nr = nr;
		i ++;
		start = end + 1;
	}

	if (i != *(levels)) {
		free(*records);
		*records = NULL;
		ret = -1;
		goto out;
	}

	ret = 0;
	
out:
	return ret;
}

/*
 * return 1, no threshold triggered
 *        0, latency threshold triggered
 *        -1, error
 */
int is_dm_target_io_latency_ok(const char *target_name,
			       int latency_threshold,
			       int latency_warning_nr)
{
	int ret = -1;
	int fd, i;
	unsigned long levels, total, start, end;
	static unsigned long last_total;
	static int firsttime = 1;
	int delta;

	struct latency_record  *latency_records = NULL;

	/* 0 means load system default configs */
	if (latency_threshold == 0 ||
	    latency_warning_nr == 0) {
		ret = load_system_default_configs(&latency_threshold,
						  &latency_warning_nr);
		if (ret < 0)
			goto out;
	}

	ret = check_parameters(target_name,
				latency_threshold,
				latency_warning_nr);
	if (ret < 0)
		goto out;

	fd = open_dm_target_latency_profile(target_name);
	if (fd < 0) {
		ret = -1;
		goto out;
	}

	ret = load_dm_latency_stats(fd,
				    &latency_records,
				    &levels);
	if (ret < 0)
		goto out_close_profile;

	for (i = 0; i < levels; i ++) {
		start = latency_records[i].start;
		end = start + latency_records[i].length;

		if ((latency_threshold >= start) &&
		    (latency_threshold < end))
			break;
	}

	/* threshold too large, no failure triggered */
	if (i == levels) {
		ret = -1; 
		goto out_release_memory;
	}

	for (total = 0; i < levels; i ++)
		total += latency_records[i].nr;

	delta = total - last_total;
	last_total = total;

	if (firsttime) {
		/* the first time last_total is 0, should ignore */
		firsttime = 0;
		ret = 1;
	} else if (delta >= latency_warning_nr) {
		/* latency threshold triggered */
		ret = 0;
	} else {
		ret = 1;
	}

#ifdef DEBUG
	printf("threshold: %d, nr: %d\n", latency_threshold, latency_warning_nr);
#endif

out_release_memory:
	free(latency_records);
out_close_profile:
	close_dm_target_latency_profile(&fd);
out:
	return ret;
}
