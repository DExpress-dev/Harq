
#include <math.h>
#include <cmath>
#include <math.h>

#include "loss_estimator.h"

loss_estimator::loss_estimator(){
}

loss_estimator::~loss_estimator(void){
}

void loss_estimator::init(){
}

float loss_estimator::get_current_loss_scale(){
	return current_loss_scale_;
}

uint32 loss_estimator::get_max_bandwidth(){
	return max_bandwidth_;
}

void loss_estimator::set_max_bandwidth(const uint32 &max_bandwidth){
	max_bandwidth_ = max_bandwidth;
}

uint64 loss_estimator::loss_bandwidth(uint64 send_packet_count, uint64 loss_packet_count, uint64 current_bandwidth){
	//采用均值
	float loss_scale = ((float)loss_packet_count / (float)send_packet_count);
	current_loss_scale_ = static_cast<float>(0.8 * current_loss_scale_ + 0.2 * loss_scale);
	uint64 new_bandwidth;
	if(current_loss_scale_ <= loss_gain[0]){
		new_bandwidth = static_cast<uint64>(current_bandwidth * 1.1);
	}else if(current_loss_scale_ > loss_gain[0] && current_loss_scale_ < loss_gain[1]){
		new_bandwidth = current_bandwidth;
	}else {
		new_bandwidth = static_cast<uint64>(current_bandwidth * (1 - current_loss_scale_));
	}
	//这里限制带宽使用不能太大避免溢出
	if(new_bandwidth >= get_max_bandwidth()){
		new_bandwidth = get_max_bandwidth();
		if (new_bandwidth >= MAX_BANDWIDTH){
			new_bandwidth = MAX_BANDWIDTH;
		}
	}
	return new_bandwidth;
}

void loss_estimator::add_log(const int log_type, const char *context, ...){
	const int array_length = 10 * KB;
	char log_text[array_length];
	memset(log_text, 0x00, array_length);
	va_list arg_ptr;
	va_start(arg_ptr, context);
	int result = harq_vsprintf(log_text, context, arg_ptr);
	va_end(arg_ptr);
	if (result <= 0)
		return;

	if (result > array_length)
		return;

	if (nullptr != write_log_ptr_){
		write_log_ptr_->write_log3(log_type, log_text);
	}
}