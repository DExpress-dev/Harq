
#include "rate_timer.h"
#include <math.h>
#include <cmath>

rate_tick_thread::rate_tick_thread()
{
}

rate_tick_thread::~rate_tick_thread(void)
{
	if(current_state_ == tst_runing)
	{
		current_state_ = tst_stoping;
		time_t last_timer = time(nullptr);
		int timer_interval = 0;
		while((timer_interval <= KILL_THREAD_TIMER))
		{
			time_t current_timer = time(nullptr);
			timer_interval = static_cast<int>(difftime(current_timer, last_timer));
			if(current_state_ == tst_stoped)
			{
				break;
			}
			ustd::rudp_public::sleep_delay(DESTROY_TIMER, Millisecond);
		}
	}
}

void rate_tick_thread::init()
{
	last_tick_timer_ = global_rudp_timer_.get_current_timer();
	thread_ptr_ = std::thread(&rate_tick_thread::execute, this);
	thread_ptr_.detach();
}

void rate_tick_thread::execute()
{
	current_state_ = tst_runing;
	while(tst_runing == current_state_)
	{
		rate_tick();
		ustd::rudp_public::sleep_delay(TICK_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void rate_tick_thread::rate_tick()
{
	
	rudptimer current_timer = global_rudp_timer_.get_current_timer();
	rudptimer interval = global_rudp_timer_.timer_interval(current_timer, last_tick_timer_);
	if(interval >= 1000)
	{
		if(on_rate_tick_ != nullptr)
		{
			on_rate_tick_(current_timer, interval);
		}
		last_tick_timer_ = global_rudp_timer_.get_current_timer();
	}
}
