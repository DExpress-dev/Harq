
#include <iostream>
#include <stdio.h>
#include "rudp_timer.h"

#if defined(_WIN32)

	#include <windows.h>
	#include <Mmsystem.h>
	#include "windows/timer_windows.h"
	#define gettimeofday(a, b) gettimeofday(a, b)
#else

	#include <sys/time.h>
	#define gettimeofday(a, b) gettimeofday(a, b)
#endif

//使用毫秒时钟
static rudptimer secondTime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	long long second = tv.tv_sec;
	return second + tv.tv_usec / (1000 * 1000);
}

//使用毫秒时钟
static rudptimer millisecondTime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	long long second = tv.tv_sec;
	return second * 1000 + tv.tv_usec / 1000;
}

//使用微秒时钟
static rudptimer microsecondTime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	
	long long second = tv.tv_sec;
	return second * 1000 * 1000 + tv.tv_usec;
}

rudp_timer::rudp_timer(void)
{
}

rudp_timer::~rudp_timer(void)
{
}

rudptimer rudp_timer::get_current_timer()
{
	if(Second == current_timer_mode)
	{	
		return secondTime();
	}
	else if(Millisecond == current_timer_mode)
	{
		return millisecondTime();
	}
	else if(Microsecond == current_timer_mode)
	{
		return microsecondTime();
	}
}

rudptimer rudp_timer::timer_interval(rudptimer timer)
{
	if(Second == current_timer_mode)
	{
		return secondTime();
	}
	else if(Millisecond == current_timer_mode)
	{
		return millisecondTime() - timer;
	}
	else if(Microsecond == current_timer_mode)
	{
		return microsecondTime() - timer;
	}
}

rudptimer rudp_timer::timer_interval(rudptimer end_timer, rudptimer begin_timer)
{
	return	end_timer - begin_timer;
}
