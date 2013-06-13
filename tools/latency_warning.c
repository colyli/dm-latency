#include <stdio.h>

int is_dm_target_io_latency_ok(const char *target_name,
				int latency_threshold,
				int latency_warning_nr);

int main()
{
	int ret;
	char *dev = "dm-0";
	int loops = 100;

	while((loops--) > 0) {
		ret = is_dm_target_io_latency_ok(dev, 0, 0);
		printf("latency of %s is ", dev);

		if (ret == 0)
			printf("bad");
		else if (ret == 1)
			printf("ok");
		else
			printf("error");
		printf("\n");
		sleep(5);
	}


	return 0;
}
