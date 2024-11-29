#include "delay_linux.h"

#include <iostream>
#include <stdio.h>
#include <cstdio>
#include <time.h>
#include <sys/sysinfo.h>

int sleep_delay_linux(const int &delay_timer, const timer_mode &cur_timer_mode)
{
	struct timeval tv;
	if (Second == cur_timer_mode)
	{
		//�뼶�ӳ�
		tv.tv_sec = delay_timer;
		tv.tv_usec = 0;
	}
	else if (Millisecond == cur_timer_mode)
	{
		//���뼶��ʱ		
		tv.tv_sec = delay_timer / 1000;
		tv.tv_usec = (delay_timer % 1000) * 1000;

	}
	else if (Microsecond == cur_timer_mode)
	{
		//΢�뼶��ʱ
		tv.tv_sec = delay_timer / (1000 * 1000);
		tv.tv_usec = delay_timer % (1000 * 1000);
	}

	int err;
	do
	{
		err = select(0, NULL, NULL, NULL, &tv);
	} while (err < 0 && errno == EINTR);

	return 0;
}

int get_cpu_core_num()
{
	return get_nprocs();
}