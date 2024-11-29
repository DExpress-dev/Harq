
#include <math.h>
#include <cmath>
#include <algorithm>

#include "trendline_estimator.h"

bool trendline_estimator::IsNewer(rudptimer value, rudptimer prev_value) {
	constexpr rudptimer kBreakpoint = (std::numeric_limits<rudptimer>::max() >> 1) + 1;
	if (value - prev_value == kBreakpoint) {
		return value > prev_value;
	}
	return value != prev_value && static_cast<rudptimer>(value - prev_value) < kBreakpoint;
}

bool trendline_estimator::IsNewerTimestamp(rudptimer timestamp, rudptimer prev_timestamp) {
	return IsNewer(timestamp, prev_timestamp);
}

rudptimer trendline_estimator::LatestTimestamp(rudptimer timestamp1, rudptimer timestamp2) {
	return IsNewerTimestamp(timestamp1, timestamp2) ? timestamp1 : timestamp2;
}

trendline_estimator::trendline_estimator(){
	reset();
	groupReset();
}

void trendline_estimator::reset(){
	num_of_deltas_ = 0;
	first_arrival_time_ms_ = -1;
	accumulated_delay_ = 0;
	smoothed_delay_ = 0;
	threshold_ = 12.5;
	prev_modified_trend_ = 0;
	last_update_ms_ = -1;
	prev_trend_ = 0.0;
	time_over_using_ = -1;
	overuse_counter_ = 0;
	hypothesis_ = BandwidthUsage::kNone;
	hypothesis_predicted_ = BandwidthUsage::kNone;
	math_state_ = false;
}

trendline_estimator::~trendline_estimator(void){
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
	free_queue();
	free_bitrate_buffer();
	delay_hist_.clear();
}

void trendline_estimator::init(){
	thread_ptr_ = std::thread(&trendline_estimator::execute, this);
	thread_ptr_.detach();
}

void trendline_estimator::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		dispense();
		check_current_state();
		ustd::rudp_public::sleep_delay(CHECK_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void trendline_estimator::add_queue(uint64 index, uint64 size, rudptimer send_timer, rudptimer recv_timer){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	trendline_buffer *buffer_ptr = new trendline_buffer;
	buffer_ptr->buffer_.index = index;
	buffer_ptr->buffer_.recv_timer = recv_timer;
	buffer_ptr->buffer_.send_timer = send_timer;
	buffer_ptr->buffer_.size = size;
	buffer_ptr->next_ = nullptr;
	if(first_ != nullptr)
		last_->next_ = buffer_ptr;
	else
		first_ = buffer_ptr;

	last_ = buffer_ptr;
}
	
void trendline_estimator::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	trendline_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

bool compare_body(std::shared_ptr<body_buffer> x, std::shared_ptr<body_buffer> y) {
	return x->send_timer < y->send_timer;
}

void trendline_estimator::dispense(){
	trendline_buffer *work_ptr = nullptr;
	{
		std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
		if(work_ptr == nullptr && first_ != nullptr){
			work_ptr = first_;
			first_ = nullptr;
			last_ = nullptr;
		}
	}
	//转换数据到map中
	convert_buffer(work_ptr);
	//排序;
	body_list_.sort(compare_body);
	//循环处理数据;
	rudptimer now_ms = global_rudp_timer_.get_current_timer();
	while(!body_list_.empty()){
		double send_timestamp_delta;	//发送时间差
		double recv_timestamp_delta;	//接收时间差
		double packet_size_delta;		//数据包大小差
		//获取最顶层的数据包
		std::shared_ptr<body_buffer> body_buffer_ptr = body_list_.front();
		//计算偏差
		ComputeDeltas(body_buffer_ptr->send_timer, body_buffer_ptr->recv_timer, now_ms, body_buffer_ptr->size, &send_timestamp_delta, &recv_timestamp_delta, &packet_size_delta);
		//计算斜率
		UpdateTrendline(recv_timestamp_delta, send_timestamp_delta, body_buffer_ptr->send_timer, body_buffer_ptr->recv_timer, body_buffer_ptr->size);
		//删除最顶层数据包
		body_list_.pop_front();
	}
}

void trendline_estimator::convert_buffer(trendline_buffer *work_ptr){
	//将所有数据放入到map中;
	trendline_buffer *next_ptr = nullptr;
	while(work_ptr != nullptr){
		next_ptr = work_ptr->next_;
		add_bitrate_buffer(work_ptr->buffer_);
		delete work_ptr;
		work_ptr = nullptr;
		work_ptr = next_ptr;
	}
}

void trendline_estimator::groupReset(){
	num_consecutive_reordered_packets_ = 0;
	current_timestamp_group_.size_ = 0;
	current_timestamp_group_.first_timestamp_ = 0;
	current_timestamp_group_.timestamp_ = 0;
	current_timestamp_group_.first_arrival_ms_ = -1;
	current_timestamp_group_.complete_time_ms_ = - 1;
	prev_timestamp_group_.size_ = 0;
	prev_timestamp_group_.first_timestamp_ = 0;
	prev_timestamp_group_.timestamp_ = 0;
	prev_timestamp_group_.first_arrival_ms_ = -1;
	prev_timestamp_group_.complete_time_ms_ = - 1;
}

bool trendline_estimator::PacketInOrder(rudptimer timestamp) {
	return (timestamp - current_timestamp_group_.first_timestamp_) < 0x80000000;
}

bool trendline_estimator::ComputeDeltas(rudptimer send_timestamp_ms, rudptimer recv_timestamp_ms, rudptimer system_time_ms, int32 packet_size,
										double* send_timestamp_delta, double* recv_timestamp_delta, double* packet_size_delta){
	bool calculated_deltas = false;
	if(IsFirstPacket(current_timestamp_group_))	{
		//是否是第一个包;
		current_timestamp_group_.timestamp_ = send_timestamp_ms;
		current_timestamp_group_.first_timestamp_ = send_timestamp_ms;
		current_timestamp_group_.first_arrival_ms_ = recv_timestamp_ms;
	}else if(!PacketInOrder(send_timestamp_ms)){
		return false;
	}else if(NewTimestampGroup(recv_timestamp_ms, send_timestamp_ms))	{
		//判断前一个分组是否有数据;
		if (prev_timestamp_group_.complete_time_ms_ >= 0) {
			//包组发送时间差:当前包组最后一个包发送时间减去前一个包组最后一个包发送时间
			*send_timestamp_delta = static_cast<double>(current_timestamp_group_.timestamp_ - prev_timestamp_group_.timestamp_);
			//包组到达时间差:当前包组最后一个包到达时间减去前一个包组最后一个包到达时间
			*recv_timestamp_delta = static_cast<double>(current_timestamp_group_.complete_time_ms_ - prev_timestamp_group_.complete_time_ms_);
			//得到系统时间偏差
			int64_t system_timestamp_delta = current_timestamp_group_.last_system_time_ms_ - prev_timestamp_group_.last_system_time_ms_;
			//检查系统的时间差，看看是否有不相称的跳跃到达时间。在这种情况下，重置到达间计算。
			if (*recv_timestamp_delta - system_timestamp_delta >= kArrivalTimeOffsetThresholdMs) {
				groupReset();
				return false;
			}
			if (*recv_timestamp_delta < 0) {
				++num_consecutive_reordered_packets_;
				if (num_consecutive_reordered_packets_ >= kReorderedResetThreshold) {
					groupReset();
				}
				return false;
			} else {
				num_consecutive_reordered_packets_ = 0;	
			}
			*packet_size_delta = static_cast<double>(current_timestamp_group_.size_) - static_cast<double>(prev_timestamp_group_.size_);
			calculated_deltas = true;
		}
		prev_timestamp_group_ = current_timestamp_group_;
		current_timestamp_group_.first_timestamp_ = send_timestamp_ms;
		current_timestamp_group_.timestamp_ = send_timestamp_ms;
		current_timestamp_group_.first_arrival_ms_ = recv_timestamp_ms;
		current_timestamp_group_.size_ = 0;
	}else{
		current_timestamp_group_.timestamp_ = LatestTimestamp(current_timestamp_group_.timestamp_, send_timestamp_ms);
	}
	//增加当前分组的累计信息
	current_timestamp_group_.size_ += packet_size;
	current_timestamp_group_.complete_time_ms_ = recv_timestamp_ms;
	current_timestamp_group_.last_system_time_ms_ = system_time_ms;
	return calculated_deltas;
}

bool trendline_estimator::IsFirstPacket(packet_group timestamp_group){
	return timestamp_group.complete_time_ms_ == -1;
}

bool trendline_estimator::NewTimestampGroup(rudptimer recv_timestamp_ms, rudptimer send_timestamp_ms) {
	if (IsFirstPacket(current_timestamp_group_)) {
		//如果是第一个包则不被视为新分组
		return false;
	} else if (BelongsToBurst(recv_timestamp_ms, send_timestamp_ms)) {
		//如果是突发数据将不被视为新分组
		return false;
	} else {
		//如果发送时间减去本分组的第一个发送时间，大于5毫秒，则为新的分组
		rudptimer send_timestamp_diff = send_timestamp_ms - current_timestamp_group_.first_timestamp_;
		return send_timestamp_diff > kTimestampGroupLengthMs;
	}
}

bool trendline_estimator::BelongsToBurst(rudptimer recv_timestamp_ms, rudptimer send_timestamp_ms){
	rudptimer recv_time_delta_ms = recv_timestamp_ms - current_timestamp_group_.complete_time_ms_;
	double timestamp_diff = (double)send_timestamp_ms - (double)current_timestamp_group_.timestamp_;
	double ts_delta_ms = kTimestampToMs * timestamp_diff + 0.5;
	if (ts_delta_ms == 0)
		return true;

	int propagation_delta_ms = static_cast<int>(recv_time_delta_ms - ts_delta_ms);
	if (propagation_delta_ms < 0 && recv_time_delta_ms <= kBurstDeltaThresholdMs && recv_timestamp_ms - current_timestamp_group_.first_arrival_ms_ < kMaxBurstDurationMs)
		return true;

	return false;
}

void trendline_estimator::add_delay_hist(double time_deltas, double smoothed_delay_ms, double raw_delay_ms){
   	std::shared_ptr<packet_timing> packet_timing_ptr(new packet_timing);
	packet_timing_ptr->arrival_time_ms_ = time_deltas;
	packet_timing_ptr->raw_delay_ms_ = raw_delay_ms;
	packet_timing_ptr->smoothed_delay_ms_ = smoothed_delay_ms;
	delay_hist_.push_back(packet_timing_ptr);
}

bool compare(std::shared_ptr<packet_timing> x, std::shared_ptr<packet_timing> y) {
	return x->arrival_time_ms_ < y->arrival_time_ms_;
}

void trendline_estimator::UpdateTrendline(double recv_delta_ms, double send_delta_ms, rudptimer send_time_ms, rudptimer recv_time_ms, int packet_size){
	//得到帧数据的时间传输时间间隔
	double delta_ms = recv_delta_ms - send_delta_ms;
	//增加统计次数;
	++num_of_deltas_;
	num_of_deltas_ = (std::min)(num_of_deltas_, kDeltaCounterMax);
	if (first_arrival_time_ms_ == -1)
    	first_arrival_time_ms_ = recv_time_ms;

	//增加统计延时;
	accumulated_delay_ += delta_ms;
	//计算得到平滑后的时延
	smoothed_delay_ = kDefaultTrendlineSmoothingCoeff * smoothed_delay_ + (1 - kDefaultTrendlineSmoothingCoeff) * accumulated_delay_;
	//插入到list中
	add_delay_hist(static_cast<double>(recv_time_ms - first_arrival_time_ms_), smoothed_delay_, accumulated_delay_);
	//排序
	delay_hist_.sort(compare);
	//判断是否已经填满;
	if (delay_hist_.size() > window_size){
		math_state_ = true;
		delay_hist_.pop_front();
	}
	//判断进行均值处理;
	// add_log(LOG_TYPE_INFO, "UpdateTrendline---->>>>");
	double trend = prev_trend_;
	if (delay_hist_.size() == window_size) {
		// 0 < trend < 1   ->  时延梯度增加
		//   trend == 0    ->  时延梯度不变
		//   trend < 0     ->  时延梯度减小
		if(!linearFitSlope(&trend)){
			// add_log(LOG_TYPE_INFO, "linearFitSlope reulst false trend=0");
			trend = 0;
		}
	}
	detect(trend, send_delta_ms, recv_time_ms);
}

//线性拟合斜率
bool trendline_estimator::linearFitSlope(double *trend){
	if(delay_hist_.size() <= 2){
		*trend = 0;
		return true;
	}
	// 线性回归公式：y = k * x + b
	// x : 包组达到时间
	// y : 平滑累计延迟
	double sum_x = 0;
	double sum_y = 0;
	for(auto iter = delay_hist_.begin(); iter != delay_hist_.end(); iter++){
		std::shared_ptr<packet_timing> packet = *iter;
		if(packet != nullptr){
			sum_x += packet->arrival_time_ms_;
    		sum_y += packet->smoothed_delay_ms_;
		}		
	}
	double x_avg = sum_x / delay_hist_.size();	// x均值
	double y_avg = sum_y / delay_hist_.size();	// y均值
	// add_log(LOG_TYPE_INFO, "sum_x=%lf sum_y=%lf delay_hist_.size()=%d x_avg=%lf y_avg=%lf", sum_x, sum_y, delay_hist_.size(), x_avg, y_avg);
	// 计算斜率 k = sum ((x - x_avg) * (y - y_avg)) / sum ((x - x_avg) * (x - x_avg))
	double numerator = 0;
	double denominator = 0;
	for(auto iter = delay_hist_.begin(); iter != delay_hist_.end(); iter++){
		std::shared_ptr<packet_timing> packet = *iter;
		if(packet != nullptr){
			double x = packet->arrival_time_ms_;
    		double y = packet->smoothed_delay_ms_;
			numerator += (x - x_avg) * (y - y_avg);
    		denominator += (x - x_avg) * (x - x_avg);
		}		
	}
	// add_log(LOG_TYPE_INFO, "numerator=%lf denominator=%lf", numerator, denominator);
	if(0 == denominator)
		return false;

	//返回延迟变化趋势直线斜率k，delay的斜率；
	// >0:	网络发生拥塞；
	// =0:	发送速率正好符合当前带宽；
	// <=	网络未饱和；
	*trend = numerator / denominator;
	return true;
}

//根据时延变化增长趋势计算当前网络状态
void trendline_estimator::detect(double trend, double ts_delta, rudptimer now_ms){
	if (num_of_deltas_ < 2) {
		//统计采样至少为2个
		hypothesis_ = BandwidthUsage::kNone;
		return;
	}
	// kMinNumDeltas : 						60
	// trend :         						传入的斜率值
	// kDefaultTrendlineThresholdGain : 	4.0
	const double modified_trend = (std::min)(num_of_deltas_, kMinNumDeltas) * trend * kDefaultTrendlineThresholdGain;
	prev_modified_trend_ = modified_trend;
	// threshold_初始值为12.5
	/**
	 * 与一个动态阈值threshold_作对比，从而得到网络状态
	 * modified_trend > threshold_，表示overuse状态
	 * modified_trend < -threshold_，表示underuse状态
	 * -threshold_ <= modified_trend <= threshold_，表示normal状态
	 */
	if(modified_trend > threshold_){
		if (time_over_using_ == -1) {
			time_over_using_ = ts_delta / 2;
		} else {
			// Increment timer
			time_over_using_ += ts_delta;
		}
		overuse_counter_++;
		if (time_over_using_ > kOverUsingTimeThreshold && overuse_counter_ > 1) {
			if (trend >= prev_trend_) {
				// add_log(LOG_TYPE_INFO, "time_over_using_=%lf overuse_counter_=%d trend=%lf prev_trend_=%lf", time_over_using_, overuse_counter_, trend, prev_trend_);
				time_over_using_ = 0;
				overuse_counter_ = 0;
				hypothesis_ = BandwidthUsage::kBwOverusing;
			}
		}
	}else if(modified_trend < -threshold_){
		// add_log(LOG_TYPE_INFO, "modified_trend=%lf threshold_=%lftrend=%lf", modified_trend, -threshold_);
		time_over_using_ = -1;
		overuse_counter_ = 0;
		hypothesis_ = BandwidthUsage::kBwUnderusing;
	}else{
		time_over_using_ = -1;
		overuse_counter_ = 0;
		hypothesis_ = BandwidthUsage::kBwNormal;
	}
	prev_trend_ = trend;
	// 阈值threshold_是动态调整的，代码实现在UpdateThreshold函数中
	updateThreshold(modified_trend, now_ms);
}

double trendline_estimator::bound(const double &lower, const double &middle, const double &upper){
	return (std::min)(std::max(lower, middle), upper);
}

void trendline_estimator::updateThreshold(double modified_trend, rudptimer now_ms) {
	if (last_update_ms_ == -1){
		last_update_ms_ = now_ms;
	}
	if (fabs(modified_trend) > threshold_ + kMaxAdaptOffsetMs) {
		last_update_ms_ = now_ms;
		return;
	}
	const double k = fabs(modified_trend) < threshold_ ? k_down : k_up;
	rudptimer time_delta_ms = (std::min)(now_ms - last_update_ms_, kMaxTimeDeltaMs);
	threshold_ += k * (fabs(modified_trend) - threshold_) * time_delta_ms;
	threshold_ = bound(threshold_, 6.f, 600.f);
	last_update_ms_ = now_ms;
}

void trendline_estimator::add_bitrate_buffer(body_buffer buffer_ptr){
	std::lock_guard<std::recursive_mutex> gurad(bitrate_lock_);
   	std::shared_ptr<body_buffer> bitrate_ptr(new body_buffer);
	bitrate_ptr->index = buffer_ptr.index;
	bitrate_ptr->size = buffer_ptr.size;
	bitrate_ptr->recv_timer = buffer_ptr.recv_timer;
	bitrate_ptr->send_timer = buffer_ptr.send_timer;
	body_list_.push_back(bitrate_ptr);
}
	
void trendline_estimator::free_bitrate_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(bitrate_lock_);
	body_list_.clear();
}

BandwidthUsage trendline_estimator::state() {
	//return network_state_predictor_ ? hypothesis_predicted_ : hypothesis_;
	return hypothesis_;
}

std::string trendline_estimator::state_string() {
	BandwidthUsage hypothesis = state();
	switch(hypothesis){
		case kNone:
			return "kNone";
		case kBwNormal:
			return "kBwNormal";
		case kBwUnderusing:
			return "kBwUnderusing";	//带宽未充分利用
		case kBwOverusing:
			return "kBwOverusing";	//带宽使用过载
		case kLast:
			return "kLast";
		default:
			return "Not State";
	}
}

void trendline_estimator::check_current_state(){
	time_t curr_time = time(nullptr);
	int second = static_cast<int>(difftime(curr_time, last_check_timer_));
	if((second >= 1 || second < -1)){
		//add_log(LOG_TYPE_INFO, "trendline_estimator---->>>> state=%s", state_string().c_str());
		last_check_timer_ = time(nullptr);
	}
}

void trendline_estimator::add_log(const int log_type, const char *context, ...){
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