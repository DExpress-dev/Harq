
#include <math.h>
#include <cmath>
#include <math.h>

#include "kalman_estimator.h"

// webrtc的算法公式：jitterDelay = theta[0] * (maxFrameSize_ - avgFrameSize_) + [noiseStdDevs * sqrt(varNoise) - noiseStdDevOffset];
// theta[0]: 信道传输速率的倒数
// MaxFrameSize_：自回话开始以来收到的最大帧大小
// AvgFrameSize_：平均帧大小
// noiseStdDevs：噪声系数 2.33
// varNoise：噪声方差
// noiseStdDevOffset：噪声扣除常数
// 注意：算法中每次都会对varNoise进行更新

kalman_estimator::kalman_estimator(){
	reset();
}

void kalman_estimator::reset(){
	theta_[0] = 1 / (512e3 / 8);
	theta_[1] = 0;
	thetaCov_[0][0] = 1e-4;
	thetaCov_[1][1] = 1e2;
	thetaCov_[0][1] = 0;
	thetaCov_[1][0] = 0;
	Qcov_[0][0] = 2.5e-10;
	Qcov_[1][1] = 1e-10;
	Qcov_[0][1] = 0;
	Qcov_[1][0] = 0;
	maxFrameSize_ = 500;
	fsSum_ = 0;
	fsCount_ = 0;
	avgFrameSize_ = 500;
	varNoise_ = 4.0;
	varFrameSize_ = 100;
	startupCount_ = 0;
	prevEstimate_ = -1.0;
	prevFrameSize_ = 0;
	alphaCount_ = 1;
	avgNoise_ = 0.0;
	lastUpdateTimer_ = 0;
	filterJitterEstimate_ = 0;
	last_show_bitrate_timer_ = time(nullptr);
}

kalman_estimator::~kalman_estimator(void){
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
}

void kalman_estimator::init(){
	thread_ptr_ = std::thread(&kalman_estimator::execute, this);
	thread_ptr_.detach();
}

void kalman_estimator::execute(){
	current_state_ = tst_runing;
	while(tst_runing == current_state_){
		dispense();
		ustd::rudp_public::sleep_delay(BITRATE_TIMER, Millisecond);
	}
	current_state_ = tst_stoped;
}

void kalman_estimator::add_queue(uint64 index, uint64 size, rudptimer send_timer, rudptimer recv_timer){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	kalman_buffer *buffer_ptr = new kalman_buffer;
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
	
void kalman_estimator::free_queue(){
	std::lock_guard<std::recursive_mutex> gurad(buffer_lock_);
	kalman_buffer *next_ptr = nullptr;
	while(first_ != nullptr){
		next_ptr = first_->next_;
		delete first_;
		first_ = next_ptr;
	}
	first_ = nullptr;
	last_ = nullptr;
}

void kalman_estimator::dispense(){
	kalman_buffer *work_ptr = nullptr;
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
	//循环获取数据
	while(!body_map_.empty()){
		//得到本次计算的帧大小和帧间隔时间;
		kalmanFrameDelayMS();
	}
}

void kalman_estimator::convert_buffer(kalman_buffer *work_ptr){
	//将所有数据放入到map中;
	kalman_buffer *next_ptr = nullptr;
	while(work_ptr != nullptr){
		next_ptr = work_ptr->next_;
		add_bitrate_buffer(work_ptr->buffer_);
		delete work_ptr;
		work_ptr = nullptr;
		work_ptr = next_ptr;
	}
}

uint64 kalman_estimator::statistics_frame_size(rudptimer *dT_send_timer, rudptimer *dT_recv_timer){
	if(body_map_.empty())
		return 0;

	rudptimer first_send_timer = 0;
	uint64 frameSizeBytes = 0;
	rudptimer min_frame_send_timer = LLONG_MAX;
	rudptimer max_frame_send_timer = LLONG_MIN;
	rudptimer min_frame_recv_timer = LLONG_MAX;
	rudptimer max_frame_recv_timer = LLONG_MIN;
	for(auto iter = body_map_.begin(); iter != body_map_.end();){
		std::shared_ptr<body_buffer> buffer_ptr = iter->second;
		if(0 == first_send_timer)		{
			first_send_timer = buffer_ptr->send_timer;
		}
		if(buffer_ptr->send_timer - first_send_timer > BITRATE_TIMER)
			break;

		frameSizeBytes +=buffer_ptr->size;
		//得到最大和最小发送接收时间
		min_frame_send_timer = (std::min)(min_frame_send_timer, buffer_ptr->send_timer);
		max_frame_send_timer = (std::max)(max_frame_send_timer, buffer_ptr->send_timer);
		min_frame_recv_timer = (std::min)(min_frame_recv_timer, buffer_ptr->recv_timer);
		max_frame_recv_timer = (std::max)(max_frame_recv_timer, buffer_ptr->recv_timer);
		iter =	body_map_.erase(iter);
	}
	*dT_send_timer = max_frame_send_timer - min_frame_send_timer;
	*dT_recv_timer = max_frame_recv_timer - min_frame_recv_timer;
	return frameSizeBytes;
}

void kalman_estimator::kalmanFrameDelayMS(){
	//得到本次参与计算的帧大小量;
	rudptimer dTSend; 
	rudptimer dTRecv;
	uint64 frameSizeBytes = statistics_frame_size(&dTSend, &dTRecv);
	if(0 == frameSizeBytes)
		return;

	//得到数据量和之前的数据量变化；
	int32 deltaFS = frameSizeBytes - prevFrameSize_;
	//得到帧数据的时间传输时间间隔（此值需要进行kalman滤波进行光滑处理）;
	rudptimer frameDelayMS = dTRecv - dTSend;
	//前五次采取均值算法求出均值;
	if (fsCount_ < FS_START_COUNT) {
		fsSum_ += frameSizeBytes;
		fsCount_++;
	} else if (fsCount_ == FS_START_COUNT) {
		avgFrameSize_ = static_cast<double>(fsSum_) / static_cast<double>(fsCount_);
		fsCount_++;
	}
	//根据帧大小和帧大小均值重新计算帧大小均值和方差;
	if (frameSizeBytes > avgFrameSize_) {
		//帧均值
		double avgFrameSize = PHI * avgFrameSize_ + (1 - PHI) * frameSizeBytes;
		if (frameSizeBytes < avgFrameSize_ + 2 * sqrt(varFrameSize_)) {
			avgFrameSize_ = avgFrameSize;
		}
		//帧方差均值;
		varFrameSize_ = VCM_MAX(PHI * varFrameSize_ + (1 - PHI) * (frameSizeBytes - avgFrameSize) * (frameSizeBytes - avgFrameSize), 1.0);
	}
	//最大帧
	maxFrameSize_ = static_cast<uint64>(VCM_MAX(PSI * maxFrameSize_, static_cast<double>(frameSizeBytes)));
	//更新上一次帧大小
	if (prevFrameSize_ == 0) {
		prevFrameSize_ = frameSizeBytes;
		return;
	}
	prevFrameSize_ = frameSizeBytes;
	//最大时间偏差
	int64 max_time_deviation_ms = static_cast<int64>(_time_deviation_upper_bound * sqrt(varNoise_) + 0.5);
	//得到最大时间偏差的正负值和当前帧延时时间偏差三个数值中的中间值（目的是为了得到一个相对较精确的时间偏差）
	//**实际帧延迟
	frameDelayMS = (std::max)((std::min)(frameDelayMS, max_time_deviation_ms), -max_time_deviation_ms);
	add_log(LOG_TYPE_INFO, "BitRate Control frameDelayMS=%lld ", frameDelayMS);
	//根据当前的坡道、偏差得到应该产生的延迟，并用实际帧延迟减去这个计算延迟 
	//**偏差 = 实际帧延迟 - 理论计算帧延迟
	double deviation = frameDelayMS - (theta_[0] * deltaFS + theta_[1]);
	//如果 偏差小于噪音的一定倍数 或者 帧大小大于平均大小 + 帧大小方差的一定倍数 则重新计算科尔曼相关参数（说明突然有大量数据发送过来，需要重新计算Kalman参数）
	if (fabs(deviation) < _numStdDevDelayOutlier * sqrt(varNoise_) || frameSizeBytes > avgFrameSize_ + _numStdDevFrameSizeOutlier * sqrt(varFrameSize_)) {
		//更新与Kalman给出的直线偏差的方差过滤器。
		estimateRandomJitter(deviation);
		//重新计算带宽信息（只有当kalman过滤器的参数发生变化的时候，才会重新计算）
		if ((deviation >= 0.0) && static_cast<double>(deltaFS) > -0.25 * maxFrameSize_) {
			kalmanEstimateChannel(frameDelayMS, deltaFS);
		}
	} else{
		//根据偏差对于kalman参数进行调整
		int32 nStdDev = (deviation >= 0) ? _numStdDevDelayOutlier : -_numStdDevDelayOutlier;
		estimateRandomJitter(nStdDev * sqrt(varNoise_));
	}
	//间隔一定次数向外部投递一次本次计算的预估带宽;
	if (startupCount_ >= POST_KALMAN_ESTIMATE) {
		filterJitterEstimate_ = calculateEstimate();
		add_log(LOG_TYPE_INFO, "BitRate Control filterJitterEstimate_=%lf <<<<----", filterJitterEstimate_);
	} else{
		startupCount_++;
	}
}

double kalman_estimator::noiseThreshold(){
  double noiseThreshold = NOISE_STDDEVS * sqrt(varNoise_) - _noiseStdDevOffset;
  if (noiseThreshold < 1.0) {
	  noiseThreshold = 1.0;
  }
  return noiseThreshold;
}

double kalman_estimator::calculateEstimate(){
	//算法公式：jitterDelay = theta[0] * (maxFrameSize_ - avgFrameSize_) + [noiseStdDevs * sqrt(varNoise) - noiseStdDevOffset];
	double noise_threshold = noiseThreshold();
	double ret = theta_[0] * (maxFrameSize_ - avgFrameSize_) + noise_threshold;
	if (ret < 1.0) {
		//很低的估计值（或负值）将被忽略了。
		if (prevEstimate_ <= 0.01) {
			ret = 1.0;
		} else {
			ret = prevEstimate_;
		}
	}
	if (ret > 10000.0) {
		//过大，选择10000;
		ret = 10000.0;
	}
	prevEstimate_ = ret;
	return ret;
}

double kalman_estimator::getFrameRate(){
	return 30.0;
}

//通过计算样本的方差来估计随机抖动与theta_给出的线的距离，这里的deviation参数在网络文章中被称作（residual）
void kalman_estimator::estimateRandomJitter(double deviation) {
	double alpha = static_cast<double>(alphaCount_ - 1) / static_cast<double>(alphaCount_);
	alphaCount_++;
	if (alphaCount_ > MAX_ALPHA_COUNT)
		alphaCount_ = MAX_ALPHA_COUNT;

	//这里注意，将fps作为了一个kalman参数来进行处理，这个地方可以取消这种关联关系;
	double fps = getFrameRate();
	if (fps > 0.0) {
		double rate_scale = 30.0 / fps;
		// if (alphaCount_ < _kStartupDelaySamples) 
		// {
		// 	rate_scale = (alphaCount_ * rate_scale + (_kStartupDelaySamples - alphaCount_)) / _kStartupDelaySamples;
		// }
		alpha = pow(alpha, rate_scale);
	}
	//噪音 注意这里的减法为（deviation - avgNoise_）网上很多文章写成了（deviation - varNoise_）
	double avgNoise = alpha * avgNoise_ + (1 - alpha) * deviation;
	double varNoise = alpha * varNoise_ + (1 - alpha) * (deviation - avgNoise_) * (deviation - avgNoise_);
	if (varNoise > varNoise_) {
		avgNoise_ = avgNoise;
		varNoise_ = varNoise;
	}
	if (varNoise_ < 1.0) {
		//方差永远不能为0，要不然会出现问题;
		varNoise_ = 1.0;
	}
}

void kalman_estimator::kalmanEstimateChannel(rudptimer frameDelayMS, int64 deltaFSBytes){
	double Eh[2];
	double hEh_sigma;
	double kalmanGain[2];
	double measureRes;
	double t00, t01;
	// 计算误差协方差和过程噪声协方差的和：E = E + Q；
	thetaCov_[0][0] += Qcov_[0][0];
	thetaCov_[0][1] += Qcov_[0][1];
	thetaCov_[1][0] += Qcov_[1][0];
	thetaCov_[1][1] += Qcov_[1][1];
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel thetaCov_[0][0]=%lf thetaCov_[0][1]=%lf", thetaCov_[0][0], thetaCov_[0][1]);
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel thetaCov_[1][0]=%lf thetaCov_[1][1]=%lf", thetaCov_[1][0], thetaCov_[1][1]);
	// 计算卡尔曼增益:
	// K = E*h'/(sigma + h*E*h')
	// h = [deltaFS 1], Eh = E*h'
	// hEh_sigma = h*E*h' + sigma
	Eh[0] = thetaCov_[0][0] * deltaFSBytes + thetaCov_[0][1];
	Eh[1] = thetaCov_[1][0] * deltaFSBytes + thetaCov_[1][1];
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel Eh[0]=%lf Eh[1]=%lf", Eh[0], Eh[1]);
	//sigma为测量噪声标准差的指数平均滤波结果，即测量噪声协方差R。
	double sigma = (300.0 * exp(-fabs(static_cast<double>(deltaFSBytes)) / (1e0 * maxFrameSize_)) +1) * sqrt(varNoise_);
	if(sigma < 1.0){
		sigma = 1.0;
	}
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel sigma=%lf", sigma);
	hEh_sigma = deltaFSBytes * Eh[0] + Eh[1] + sigma;
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel hEh_sigma=%lf", hEh_sigma);
	//求kalman增益(一个是传输率增益，一个是偏移量增益);
	kalmanGain[0] = Eh[0] / hEh_sigma;
	kalmanGain[1] = Eh[1] / hEh_sigma;
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel kalmanGain[0]=%lf kalmanGain[1]=%lf", kalmanGain[0], kalmanGain[1]);
	//计算残差，获得最优估计值
	measureRes = (double)frameDelayMS - ((double)deltaFSBytes * theta_[0] + theta_[1]);
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel measureRes=%lf deltaFSBytes=%lld theta_[0]=%lf theta_[1]=%lf frameDelayMS=%lld", 
	// 	measureRes, 
	// 	deltaFSBytes, 
	// 	theta_[0], 
	// 	theta_[1], 
	// 	frameDelayMS);
	theta_[0] += kalmanGain[0] * measureRes;
	theta_[1] += kalmanGain[1] * measureRes;
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel theta_[0]=%lf theta_[1]=%lf", theta_[0], theta_[1]);
	if(theta_[0] < MIN_THETA_LOW){
		theta_[0] = MIN_THETA_LOW;
	}
	// 更新误差协方差：E = (I - K*h)*E；
	t00 = thetaCov_[0][0]; 
	t01 = thetaCov_[0][1];
	thetaCov_[0][0] = (1 - kalmanGain[0] * deltaFSBytes) * t00 - kalmanGain[0] * thetaCov_[1][0];
	thetaCov_[0][1] = (1 - kalmanGain[0] * deltaFSBytes) * t01 - kalmanGain[0] * thetaCov_[1][1];
	thetaCov_[1][0] = thetaCov_[1][0] * (1 - kalmanGain[1]) - kalmanGain[1] * deltaFSBytes * t00;
	thetaCov_[1][1] = thetaCov_[1][1] * (1 - kalmanGain[1]) - kalmanGain[1] * deltaFSBytes * t01;
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel thetaCov_[0][0]=%lf thetaCov_[0][1]=%lf", thetaCov_[0][0], thetaCov_[0][1]);
	// add_log(LOG_TYPE_INFO, "BitRate Control kalmanEstimateChannel thetaCov_[1][0]=%lf thetaCov_[1][1]=%lf", thetaCov_[1][0], thetaCov_[1][1]);
}

void kalman_estimator::add_bitrate_buffer(body_buffer buffer_ptr){
	std::lock_guard<std::recursive_mutex> gurad(bitrate_lock_);
   	std::shared_ptr<body_buffer> bitrate_ptr(new body_buffer);
	bitrate_ptr->index = buffer_ptr.index;
	bitrate_ptr->size = buffer_ptr.size;
	bitrate_ptr->recv_timer = buffer_ptr.recv_timer;
	bitrate_ptr->send_timer = buffer_ptr.send_timer;
	body_map_.insert(std::make_pair(bitrate_ptr->send_timer, bitrate_ptr));
}
	
void kalman_estimator::free_bitrate_buffer(){
	std::lock_guard<std::recursive_mutex> gurad(bitrate_lock_);
	body_map_.clear();
}

void kalman_estimator::checkShowed(){
	time_t curr_time = time(nullptr);
	int second = abs(static_cast<int>(difftime(curr_time, last_show_bitrate_timer_)));
	if(second >= 1){
		showBitrate(0, 0);
		last_show_bitrate_timer_ = time(nullptr);
	}
}

void kalman_estimator::showBitrate(uint64 frameSizeBytes, rudptimer frameDelayMS){
	//显示各种参数信息;
	add_log(LOG_TYPE_INFO, "BitRate Control---->>>> \n \
							theta_[0]=%lf theta_[1]=%lf \n \
							thetaCov_[0][0]=%lf thetaCov_[0][1]=%lf thetaCov_[1][0]=%lf thetaCov_[1][1]=%lf \n \
							Qcov_[0][0]=%lf Qcov_[0][1]=%lf Qcov_[1][0]=%lf Qcov_[1][1]=%lf \n \
							frameSizeBytes=%d \n \
							frameDelayMS=%lld \n \
							maxFrameSize_=%d \n \
							fsSum_=%d \n \
							fsCount_=%d \n \
							avgFrameSize_=%lf \n \
							varNoise_=%lf \n \
							varFrameSize_=%lf \n \
							prevFrameSize_=%d \n \
							startupCount_=%d \n \
							prevEstimate_=%lf \n \
							lastUpdateTimer_=%lld \n \
							alphaCount_=%d \n \
							avgNoise_=%lf \n \
							filterJitterEstimate_=%lf \n \n", 
							theta_[0], theta_[1],
							thetaCov_[0][0], thetaCov_[0][1], thetaCov_[1][0], thetaCov_[1][1],
							Qcov_[0][0], Qcov_[0][1], Qcov_[1][0], Qcov_[1][1],
							frameSizeBytes,
							frameDelayMS,
							maxFrameSize_,
							fsSum_,
							fsCount_,
							avgFrameSize_,
							varNoise_,
							varFrameSize_,
							prevFrameSize_,
							startupCount_,
							prevEstimate_,
							lastUpdateTimer_,
							alphaCount_,
							avgNoise_,
							filterJitterEstimate_);
}

void kalman_estimator::add_log(const int log_type, const char *context, ...){
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