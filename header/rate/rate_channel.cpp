
#include "rate_channel.h"
#include <math.h>
#include <cmath>

rate_channel::rate_channel(rate_type mode)
{
	rate_type_ = mode;
	useful_size_ = 0;
	overall_size_ = 0;
}

rate_channel::~rate_channel(void)
{
}

void rate_channel::reset_rate()
{
	std::lock_guard<std::recursive_mutex> gurad(current_data_lock_);

	useful_size_ = 0;
	overall_size_ = 0;
}

void rate_channel::insert_useful(const uint64 &useful_size)
{
	std::lock_guard<std::recursive_mutex> gurad(current_data_lock_);
	useful_size_ += useful_size;
}

void rate_channel::insert_overall(const uint64 &overall_size)
{
	std::lock_guard<std::recursive_mutex> gurad(current_data_lock_);
	overall_size_ += overall_size;
}

uint64 rate_channel::get_overall()
{
	std::lock_guard<std::recursive_mutex> gurad(current_data_lock_);
	return overall_size_;
}

uint64 rate_channel::get_useful()
{
	std::lock_guard<std::recursive_mutex> gurad(current_data_lock_);
	return useful_size_;
}