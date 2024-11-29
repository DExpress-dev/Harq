#pragma once

#ifndef timer_thread_h
#define timer_thread_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>

typedef std::function<void()> ON_TIMER_TICK;
class timer_thread{
public:
	ON_TIMER_TICK on_second_timer_tick_ = nullptr;
	ON_TIMER_TICK on_millisecond_tick_ = nullptr;
	timer_thread(uint32 millisecond_timer);
	~timer_thread(void);
	void init();
public:
	thread_state_type current_state_;
	std::thread thread_ptr_;
	void execute();
public:
	uint32 millisecond_timer_ = 1;
	rudptimer last_second_timer_;
	rudptimer last_millisecond_timer_;
	void check_second_timer();
	void check_millisecond_timer();
};

#endif /* timer_thread_h */



