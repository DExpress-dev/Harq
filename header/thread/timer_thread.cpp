

#include "timer_thread.h"
#include <string.h>

#include "../include/rudp_def.h"
#include "../include/rudp_timer.h"

//*****************
timer_thread::timer_thread(uint32 millisecond_timer){
	millisecond_timer_ = millisecond_timer;
	last_second_timer_ = global_rudp_timer_.get_current_timer();
	last_millisecond_timer_ = global_rudp_timer_.get_current_timer();
}

void timer_thread::init(){
	last_second_timer_ = global_rudp_timer_.get_current_timer();
	last_millisecond_timer_ = global_rudp_timer_.get_current_timer();
	thread_ptr_ = std::thread(&timer_thread::execute, this);
	thread_ptr_.detach();
}

timer_thread::~timer_thread(void){
	if(current_state_ == tst_runing){
		current_state_ = tst_stoping;
		time_t last_timer = time(nullptr);
		int timer_interval = 0;
		while((timer_interval <= KILL_THREAD_TIMER)){
			time_t current_timer = time(nullptr);
			timer_interval = static_cast<int>(difftime(current_timer, last_timer));
			if(current_state_ == tst_stoped){
				break;
			}
			ustd::rudp_public::sleep_delay(DESTROY_TIMER, Millisecond);
		}
	}
}

void timer_thread::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		check_second_timer();
		check_millisecond_timer();
		ustd::rudp_public::sleep_delay(1, Millisecond);
	}
	current_state_ = tst_stoped;
}

void timer_thread::check_second_timer(){
	rudptimer current_timer = global_rudp_timer_.get_current_timer();
	rudptimer interval = global_rudp_timer_.timer_interval(current_timer, last_second_timer_);
	if(interval >= 1000){
		if(on_second_timer_tick_ != nullptr){
			on_second_timer_tick_();
		}
		last_second_timer_ = global_rudp_timer_.get_current_timer();
	}
}

void timer_thread::check_millisecond_timer(){
	rudptimer current_timer = global_rudp_timer_.get_current_timer();
	rudptimer interval = global_rudp_timer_.timer_interval(current_timer, last_millisecond_timer_);
	if(interval >= millisecond_timer_){
		if(on_millisecond_tick_ != nullptr){
			on_millisecond_tick_();
		}
		last_millisecond_timer_ = global_rudp_timer_.get_current_timer();
	}
}