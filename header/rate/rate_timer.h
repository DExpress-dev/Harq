#pragma once

#ifndef rate_tick_h
#define rate_tick_h

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <thread>
#include <map>
#include <list>

#include "../../../header/write_log.h"

const int TICK_TIMER = 2;

typedef std::function<void(const rudptimer &current_tick, const rudptimer &interval)> ON_TICK;

class rate_tick_thread
{
public:
	ON_TICK on_rate_tick_ = nullptr;
	rate_tick_thread();
	~rate_tick_thread(void);
	void init();

public:
	thread_state_type current_state_ = tst_init;
	std::thread thread_ptr_;
	void execute();

private:
	rudptimer last_tick_timer_;
	void rate_tick();
    
};

#endif /* rate_tick_h */

