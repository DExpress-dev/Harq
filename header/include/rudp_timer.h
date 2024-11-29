#pragma once

#ifndef RUDP_TIMER_H_
#define RUDP_TIMER_H_

#include <iostream>
#include <stdio.h>
#include "rudp_def.h"

#if defined(_WIN32)

	#include <windows.h>

	#define harq_file_handle HANDLE
#else

	#include <sys/time.h>
#endif

class rudp_timer
{
public:
	rudp_timer(void);
	~rudp_timer(void);
public:
	rudptimer get_current_timer();
	rudptimer timer_interval(rudptimer timer);
	rudptimer timer_interval(rudptimer end_timer, rudptimer begin_timer);
};

#endif  // RUDP_TIMER_H_
