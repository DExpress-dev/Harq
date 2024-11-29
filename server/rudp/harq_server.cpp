
#include <math.h>
#include <algorithm>
#include <mutex>
#include <memory>
#include <errno.h>
#include <random>
#include <limits.h>

#include "harq_server.h"
#include "../../../header/write_log.h"
#include "../../header/include/rudp_error.h"
#include "../../header/include/rudp_public.h"
#include "../../header/include/rudp_linux.h"
#include "../../header/frames/rudp_frames.h"
#include "../../header/rate/rate_channel.h"
#include "../../header/rate/rate_timer.h"

#if defined(_WIN32)
	#include <Winsock2.h>
	#include <windows.h>
	#define path_separator "/"
#else
	#include <arpa/inet.h>
	#define path_separator "//"
#endif

namespace ustd{
	namespace harq_server{
		protocol_thread::protocol_thread(harq_linker *parent){
			harq_linker_ = parent;
			current_state_ = tst_init;
			first_ = nullptr;
			last_ = nullptr;
		}

		void protocol_thread::init(){
			thread_ptr_ = std::thread(&protocol_thread::execute, this);
			thread_ptr_.detach();
		}

		protocol_thread::~protocol_thread(void){
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
		}

		void protocol_thread::add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer, struct sockaddr_in remote_addr){
			std::lock_guard<std::recursive_mutex> gurad(rudp_buffer_lock_);
			rudp_buffer *buffer_ptr = new rudp_buffer();
			buffer_ptr->message_type_ = message_type;
			buffer_ptr->next_ = nullptr;
			buffer_ptr->size_ = size;
			buffer_ptr->recv_timer_ = recv_timer;
			memset(buffer_ptr->buffer_, 0, sizeof(buffer_ptr->buffer_));
			if(size > 0){
				memcpy(buffer_ptr->buffer_, data, size);
			}
			memset(&buffer_ptr->remote_addr_, 0, sizeof(buffer_ptr->remote_addr_));
			memcpy(&buffer_ptr->remote_addr_, &remote_addr, sizeof(remote_addr));
			if(first_ != nullptr)
				last_->next_ = buffer_ptr;
			else
				first_ = buffer_ptr;

			last_ = buffer_ptr;
		}
		void protocol_thread::free_queue(){
			std::lock_guard<std::recursive_mutex> gurad(rudp_buffer_lock_);
			rudp_buffer *next_ptr = nullptr;
			while(first_ != nullptr){
				next_ptr = first_->next_;
				delete first_;
				first_ = next_ptr;
			}
			first_ = nullptr;
			last_ = nullptr;
		}

		void protocol_thread::execute(){
			current_state_ = tst_runing;
			while(tst_runing == current_state_){
				protocol_dispense();
				ustd::rudp_public::sleep_delay(PROTOCOL_TIMER, Millisecond);
			}
			current_state_ = tst_stoped;
		}

		void protocol_thread::protocol_dispense(){
			rudp_buffer *work_ptr = nullptr;
			rudp_buffer *next_ptr = nullptr;
			{
				std::lock_guard<std::recursive_mutex> gurad(rudp_buffer_lock_);
				if(work_ptr == nullptr && first_ != nullptr){
					work_ptr = first_;
					first_ = nullptr;
					last_ = nullptr;
				}
			}
			while(work_ptr != nullptr){
				next_ptr = work_ptr->next_;
				//添加整体数据量;
				if(harq_linker_->recv_rate_ != nullptr){
					harq_linker_->recv_rate_->insert_overall(work_ptr->size_);
				}
				if(message_ack == work_ptr->message_type_)	{
					handle_ack_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
				}else if(message_syn == work_ptr->message_type_){
					handle_syn_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
				}else if(message_sack == work_ptr->message_type_){
					if(OPEN == harq_linker_->get_rudp_state()){
						handle_sack_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
					}
				}else if(message_sysc_time == work_ptr->message_type_){
					handle_sysc_time_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
				}else if(message_nul == work_ptr->message_type_){
					if(OPEN == harq_linker_->get_rudp_state()){
						handle_null_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
					}
				}else if(message_rst == work_ptr->message_type_){
					handle_rst_protocol(work_ptr->buffer_, work_ptr->size_, work_ptr->remote_addr_);
				}else if(message_error == work_ptr->message_type_){
					if(OPEN == harq_linker_->get_rudp_state()){
						harq_linker_->rudp_group_->add_queue(work_ptr->message_type_, work_ptr->buffer_, work_ptr->size_, work_ptr->recv_timer_);
					}
				}else if(message_data == work_ptr->message_type_){
					if(OPEN == harq_linker_->get_rudp_state()){
						struct frames_buffer *frames_ptr = (struct frames_buffer *)work_ptr->buffer_;
						//为了避免出现整帧非分组的整数倍因此同时给分组和接收通道同时加入;
						if(harq_linker_->rudp_group_ != nullptr){
							harq_linker_->rudp_group_->add_queue(work_ptr->message_type_, work_ptr->buffer_, work_ptr->size_, work_ptr->recv_timer_);
						}
						if(harq_linker_->recv_channel_ != nullptr){
							harq_linker_->recv_channel_->add_queue(work_ptr->message_type_, work_ptr->buffer_, work_ptr->size_, work_ptr->recv_timer_, false);	
						}
						//为了求RTO这里需要对于第一次发送的数据直接接行ack返回;
						if(0 == frames_ptr->body_.send_count_){
							harq_linker_->directly_index_ack(frames_ptr->header_.index_);
						}
						//设置接收数据的时间;
						harq_linker_->linker_record_.last_recv_timer_ = global_rudp_timer_.get_current_timer();
					}			
				}
				delete work_ptr;
				work_ptr = next_ptr;
			}
		}

		bool protocol_thread::handle_ack_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			if(OPEN != harq_linker_->get_rudp_state())
				return false;

			ack_rudp_header header = ustd::rudp_public::get_ack_header2(data, size);
			if(nullptr != harq_linker_->send_channel_){
				harq_linker_->send_channel_->add_ack_queue(header);
			}
			return true;
		}

		bool protocol_thread::handle_sack_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			if(OPEN != harq_linker_->get_rudp_state())
				return false;

			sack_rudp_header header = ustd::rudp_public::get_sack_header2(data, size);
			if(nullptr != harq_linker_->send_channel_){
				harq_linker_->send_channel_->add_sack_queue(header);
			}
			return true;
		}

		bool protocol_thread::handle_syn_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			syn_rudp_header header = ustd::rudp_public::get_syn_header2(data, size);
			if(LISTEN == harq_linker_->get_rudp_state()){
				//判断客户端是否让传输进行加密;
				if(header.encrypt_){
					harq_linker_->set_encrypted(header.encrypt_);
					std::string key((char*)header.key_);
					std::string iv((char*)header.iv_);
					harq_linker_->set_key(key, iv);
				}
				harq_linker_->set_remote_timer(header.client_timer_);
				add_log(LOG_TYPE_INFO, "syn set_remote_timer header.client_timer_=%lld", header.client_timer_);
				//向客户端返回一个syn_ack类型的数据;
				harq_linker_->handle_send_ack_syn(header.header_.index_, INIT_SEGMENT_INDEX, header.client_timer_);
				harq_linker_->set_rudp_state(OPEN);
				if(nullptr != harq_linker_->send_channel_){
					harq_linker_->send_channel_->set_segment_index(INIT_SEGMENT_INDEX);
				}
			}else{
				harq_linker_->handle_send_ack_syn(header.header_.index_, INIT_SEGMENT_INDEX, header.client_timer_);
				harq_linker_->set_rudp_state(OPEN);
			}
			return true;
		}

		bool protocol_thread::handle_null_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			if(CLOSE == harq_linker_->get_rudp_state())
				return false;

			null_rudp_header null_header = ustd::rudp_public::get_null_header2(data, size);
			if(harq_linker_->client_null_index_ != null_header.null_index_){
				harq_linker_->client_null_index_ = null_header.null_index_;
				if(nullptr != harq_linker_->rudp_group_){
					harq_linker_->rudp_group_->set_remote_max_index(null_header.max_index_);
				}
				//设置相关的信息;
				harq_linker_->set_remote_rto(null_header.real_timer_rto_);
				harq_linker_->set_recv_timer();
				harq_linker_->send_channel_->set_remote_nul(null_header);
				harq_linker_->handle_send_nul();
			}
			return true;
		}

		bool protocol_thread::handle_rst_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			if(CLOSE == harq_linker_->get_rudp_state())
				return false;

			return harq_linker_->handle_rst_protocol();
		}

		bool protocol_thread::handle_sysc_time_protocol(char *data, int size, struct sockaddr_in &remote_addr){
			if(CLOSE == harq_linker_->get_rudp_state())
				return false;

			//时钟同步
			sysc_time_header time_header = ustd::rudp_public::get_sysc_time_header2(data, size);
			harq_linker_->frames_class_->set_base_timer(time_header.client_base_timer_, time_header.server_base_timer_);
			harq_linker_->set_sys_time_stamp(time_header.sys_server_time_stamp_, time_header.sys_client_time_stamp_);
			harq_linker_->set_first_rto(time_header.first_rto_);

			// add_log(LOG_TYPE_INFO, "handle_sysc_time_protocol first_rto=%d server_base_timer_=%lld client_base_timer_=%lld sys_server_time_stamp_=%d sys_client_time_stamp_=%d", 
			// 						time_header.first_rto_, 
			// 						time_header.server_base_timer_,
			// 						time_header.client_base_timer_,
			// 						time_header.sys_server_time_stamp_,
			// 						time_header.sys_client_time_stamp_);
			return true;
		}

		void protocol_thread::add_log(const int log_type, const char *context, ...){
			const int array_length = 1024 * 10;
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

			if(write_log_ptr_ != nullptr)
				write_log_ptr_->write_log3(log_type, log_text);
		}

		//*****************
		check_thread::check_thread(harq_manager *parent){
			harq_manager_ = parent;
			last_check_timer_ = time(nullptr);
		}

		void check_thread::init(){
			last_check_timer_ = time(nullptr);
			thread_ptr_ = std::thread(&check_thread::execute, this);
			thread_ptr_.detach();
		}

		check_thread::~check_thread(void){
			if(current_state_ == tst_runing){
				current_state_ = tst_stoping;
				time_t last_timer = time(nullptr);
				int timer_interval = 0;
				while((timer_interval <= KILL_THREAD_TIMER)){
					time_t current_timer = time(nullptr);
					timer_interval = static_cast<int>(difftime(current_timer, last_timer));
					if(current_state_ == tst_stoped)
						break;

					ustd::rudp_public::sleep_delay(DESTROY_TIMER, Millisecond);
				}
			}
		}

		const int32 SECOND_TIMER = 1000;
		void check_thread::execute(){
			current_state_ = tst_runing;
			while(tst_runing == current_state_){
				check();
				ustd::rudp_public::sleep_delay(SECOND_TIMER, Millisecond);
			}
			current_state_ = tst_stoped;
		}

		void check_thread::check(){
			time_t current_timer = time(nullptr);
			int seconds = abs(static_cast<int>(difftime(current_timer, last_check_timer_)));
			if(seconds >= 1){
				//检测超时
				harq_manager_->check_timeout();
				//透出RTO
				harq_manager_->check_rto();
				last_check_timer_ = time(nullptr);
			}
		}

		void check_thread::add_log(const int log_type, const char *context, ...){
			const int array_length = 1024 * 10;
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

			if(write_log_ptr_ != nullptr){
				write_log_ptr_->write_log3(log_type, log_text);
			}
		}

		//*************
		harq_linker::harq_linker(harq_manager *parent){
			harq_manager_ = parent;
			memset(&linker_record_, 0, sizeof(linker_record_));
			current_send_frames_no_ =  INIT_FRAMES_NO;
		}

		void harq_linker::init(harq_fd work_fd, int linker_handle){
			memset(&linker_record_, 0, sizeof(linker_record_));
			memset(remote_ip_, 0, sizeof(remote_ip_));
			linker_record_.linker_handle_ = linker_handle;
			current_send_frames_no_ =  INIT_FRAMES_NO;
			disconnected_ = false;
			linker_record_.last_recv_timer_ = global_rudp_timer_.get_current_timer();
			protocol_thread_ = new protocol_thread(this);
			protocol_thread_->write_log_ptr_ = harq_manager_->write_log_ptr_;
			protocol_thread_->init();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create protocol_thread OK");
			//客户端发送线程;
			send_thread_ = new client_send_thread();
			send_thread_->on_check_linker_state_ = std::bind(&harq_linker::handle_check_linker_state, this, std::placeholders::_1);
			send_thread_->on_set_index_state_ = std::bind(&harq_linker::handle_index_state, this, std::placeholders::_1, std::placeholders::_2);
			send_thread_->write_log_ptr_ = write_log_ptr_;
			send_thread_->init(work_fd, get_max_threhold());
			recv_channel_ = new recv_channel();
			recv_channel_->on_add_frames_ = std::bind(&harq_linker::handle_add_frames, this, std::placeholders::_1);
			recv_channel_->write_log_ptr_ = harq_manager_->write_log_ptr_;
			recv_channel_->init();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create recv_channel OK");
			//创建分组管理类;
			rudp_group_ = new rudp_group();
			rudp_group_->on_add_packet_ = std::bind(&harq_linker::handle_add_packet, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
			rudp_group_->on_send_sack_ = std::bind(	&harq_linker::handle_send_sack, this, std::placeholders::_1);
			rudp_group_->on_remote_rto_ = std::bind(&harq_linker::get_remote_rto, this);
			rudp_group_->on_get_index_ = std::bind(&harq_linker::handle_get_index, this);
			rudp_group_->on_add_log_ = std::bind(&harq_linker::handle_add_log, this, std::placeholders::_1, std::placeholders::_2);
			rudp_group_->write_log_ptr_ = harq_manager_->write_log_ptr_;
			rudp_group_->init();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create rudp_group OK");
			//创建接收速率计算类;
			recv_rate_ = new rate_channel(RECV_TYPE);
			recv_rate_->reset_rate();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create recv_rate OK");
			//创建接收速率计算类;
			send_rate_ = new rate_channel(SEND_TYPE);
			send_rate_->reset_rate();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create send_rate OK");
			//创建帧管理类;
			frames_class_ = new frames_class(SERVER);
			frames_class_->on_handle_recv_ = std::bind(	&harq_linker::handle_on_read, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
			frames_class_->on_handle_recv_useful_ = std::bind(&harq_linker::handle_on_recv_useful, this, std::placeholders::_1);
			frames_class_->on_first_rto_ = std::bind(&harq_linker::get_first_rto, this);
			frames_class_->on_sys_server_timer_stamp_ = std::bind(&harq_linker::get_sys_server_time_stamp, this);
			frames_class_->on_sys_client_timer_stamp_ = std::bind(&harq_linker::get_sys_client_time_stamp, this);
			frames_class_->write_log_ptr_ = harq_manager_->write_log_ptr_;
			harq_manager_->add_log(LOG_TYPE_INFO, "Create frames_class OK");
			//创建发送频道类;
			send_channel_ = new send_channel(linker_record_.linker_handle_, get_start_threhold(), get_max_threhold());
			send_channel_->on_set_frames_id_ = std::bind(&harq_linker::set_current_frames_id, this, std::placeholders::_1);
			send_channel_->on_set_segment_index_ = std::bind(&harq_linker::set_current_segment_index, this, std::placeholders::_1);
			send_channel_->on_add_send_queue_ = std::bind(&harq_linker::handle_add_send_queue, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
			send_channel_->on_add_send_queue_no_feekback_ = std::bind(&harq_linker::handle_add_send_queue_no_feekback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6);
			send_channel_->on_resend_ = std::bind(&harq_linker::handle_resend, this, std::placeholders::_1, std::placeholders::_2);
			send_channel_->on_send_overall_ = std::bind(&harq_linker::handle_on_send_overall, this, std::placeholders::_1);
			send_channel_->on_send_useful_ = std::bind(&harq_linker::handle_on_send_useful, this, std::placeholders::_1);
			send_channel_->write_log_ptr_ = harq_manager_->write_log_ptr_;
			send_channel_->init_fec_threads();
			send_channel_->init();
			harq_manager_->add_log(LOG_TYPE_INFO, "Create send_channel OK");
		}

		void harq_linker::kill_class(){
			harq_manager_->add_log(LOG_TYPE_INFO, "Clean kill_class");
			//释放协议拆分线程
			if(nullptr != protocol_thread_){
				protocol_thread_->write_log_ptr_ = nullptr;
				delete protocol_thread_;
				protocol_thread_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete protocol_thread");
			}
			if (nullptr != send_thread_){
				send_thread_->on_check_linker_state_ = nullptr;
				send_thread_->on_set_index_state_ = nullptr;
				send_thread_->write_log_ptr_ = write_log_ptr_;
				delete send_thread_;
				send_thread_ = nullptr;
			}
			//释放接收频道线程;
			if(recv_channel_ != nullptr){
				//设置回调为空;
				recv_channel_->on_add_frames_ = nullptr;
				recv_channel_->write_log_ptr_ = nullptr;
				delete recv_channel_;
				recv_channel_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete recv_channel");
			}
			//释放发送频道线程（这里有缓存，会释放出一定的内存来）
			if(send_channel_ != nullptr){
				//设置回调为空;
				send_channel_->on_set_frames_id_ = nullptr;
				send_channel_->on_set_segment_index_ = nullptr;
				send_channel_->on_add_send_queue_ = nullptr;
				send_channel_->on_resend_ = nullptr;
				send_channel_->on_send_overall_ = nullptr;
				send_channel_->on_send_useful_ = nullptr;
				send_channel_->write_log_ptr_ = nullptr;
				delete send_channel_;
				send_channel_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete send_channel");
			}
			//释放分组
			if(rudp_group_ != nullptr){
				//设置回调为空;
				rudp_group_->on_add_packet_ = nullptr;
				rudp_group_->on_send_sack_ = nullptr;
				rudp_group_->on_remote_rto_ = nullptr;
				rudp_group_->on_get_index_ = nullptr;
				rudp_group_->write_log_ptr_ = nullptr;
				delete rudp_group_;
				rudp_group_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete rudp_group");
			}
			if(frames_class_ != nullptr){
				//设置回调为空;
				frames_class_->on_handle_recv_ = nullptr;
				frames_class_->on_handle_recv_useful_ = nullptr;
				frames_class_->on_first_rto_ = nullptr;
				frames_class_->on_sys_server_timer_stamp_ = nullptr;
				frames_class_->on_sys_client_timer_stamp_ = nullptr;
				frames_class_->write_log_ptr_ = nullptr;
				delete frames_class_;
				frames_class_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete frames_class");
			}
			if(recv_rate_ != nullptr){
				delete recv_rate_;
				recv_rate_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete recv_rate");
			}
			if(send_rate_ != nullptr){
				delete send_rate_;
				send_rate_ = nullptr;
				harq_manager_->add_log(LOG_TYPE_INFO, "delete send_rate");
			}
		}

		harq_linker::~harq_linker(void){
			//设置状态;
			set_rudp_state(CLOSE);
			//删除类;
			kill_class();
			harq_sleep(0);
		}

		void harq_linker::directly_index_ack(const uint64 &ack_index){
			ack_rudp_header header;
			memset(&header, 0, sizeof(header));
			header.header_.message_type_ = message_ack;
			header.header_.index_ = ack_index;
			header.complete_group_id_ = rudp_group_->get_complete_group_id();
			handle_send_ack(header);
		}

		void harq_linker::handle_send_rst(){
			rudp_header header;
			memset(&header, 0, sizeof(header));
			header.message_type_ = message_rst;
			header.index_ = 0;
			for(int i = 0; i < RST_MAX_GO_COUNT; i++){
				if (nullptr != send_thread_){
					send_thread_->add_buffer_no_feedback_urgent((char*)&header, sizeof(header), linker_record_.linker_handle_, get_remote_addr());
				}
			}
		}

		uint64 harq_linker::get_send_max_index(){
			if(nullptr != send_channel_)
				return send_channel_->get_send_max_index();
			else
				return 0;
		}

		void harq_linker::handle_send_nul(){
			if(OPEN == get_rudp_state()){
				struct sockaddr_in remote_addr = get_remote_addr();
				null_rudp_header header;
				memset(&header, 0, sizeof(header));
				header.header_.message_type_ = message_nul;
				header.null_index_ = null_index();
				header.max_index_ = get_send_max_index();
				header.loss_packet_count_ = recv_channel_->loss_packet_count();
				header.recv_packet_count_ = recv_channel_->recv_packet_count();
				header.loss_packet_interval_ = recv_channel_->loss_packet_interval();
				header.recv_packet_interval_ = recv_channel_->recv_packet_interval();
				header.useful_recv_size_ = recv_rate_->get_useful();
				header.overall_recv_size_ = recv_rate_->get_overall();
				header.real_timer_rto_ = get_local_rto();
				if (nullptr != send_thread_){
					for(int i = 0; i < HEART_MAX_GO_COUNT; i++){
						send_thread_->add_buffer_no_feedback_urgent((char*)&header, sizeof(header), linker_record_.linker_handle_, remote_addr);
						handle_on_send_overall(sizeof(null_rudp_header));
					}
				}
			}
		}

		void harq_linker::handle_send_ack_syn(	const uint64 &ack_index, const uint64 &segment_index, const rudptimer &client_timer){
			ack_syn_rudp_header header;
			memset(&header, 0, sizeof(header));
			header.header_.message_type_ = message_syn_ack;
			header.header_.index_ = ack_index;
			header.server_timer_ = global_rudp_timer_.get_current_timer();
			header.client_timer_ = client_timer;
			header.encrypt_ = linker_record_.encrypted_;
			if(header.encrypt_){
				memcpy(header.key_, linker_record_.key_, sizeof(linker_record_.key_));
				memcpy(header.iv_, linker_record_.iv_, sizeof(linker_record_.iv_));
			}
			if (nullptr != send_thread_){
				send_thread_->add_buffer_no_feedback_urgent((char*)&header, sizeof(header), linker_record_.linker_handle_, get_remote_addr());
			}
		}

		void harq_linker::handle_send_error(const error_buffer &error_buffer_ptr){
			if (nullptr != send_thread_)
				send_thread_->add_buffer_no_feedback(0, (char*)&error_buffer_ptr, sizeof(error_buffer_ptr), linker_record_.linker_handle_, get_remote_addr());
		}

		void harq_linker::handle_send_ack(const ack_rudp_header &ack_rudp_header_ptr){
			if(CLOSE == get_rudp_state())
				return;

			if (nullptr != send_thread_)
				send_thread_->add_buffer_no_feedback_urgent((char*)&ack_rudp_header_ptr, sizeof(ack_rudp_header_ptr), linker_record_.linker_handle_, get_remote_addr());
		}

		void harq_linker::handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created){
			//加入到recv_channel中
			if(recv_channel_ != nullptr)
				recv_channel_->add_queue(message_type, data, size, recv_timer, fec_created);

			//加入到group中;
			if(rudp_group_ != nullptr)
				rudp_group_->add_queue(message_type, data, size, recv_timer);

			//回复;
			frames_buffer *frames_ptr = (frames_buffer *)data;
			directly_index_ack(frames_ptr->header_.index_);
		}

		uint64 harq_linker::handle_get_index(){
			if(nullptr != recv_channel_)
				return recv_channel_->get_complete_index();
			else
				return 0;
		}

		void harq_linker::handle_add_log(const int log_type, const char *context){
			add_log(log_type, context);
		}

		uint64 harq_linker::null_index(){
			return null_index_++;
		}

		bool harq_linker::handle_check_linker_state(const struct sockaddr_in &remote_addr){
			if (CLOSE == get_rudp_state())
				return false;

			return true;
		}

		void harq_linker::handle_index_state(const int &linker_handle, const uint64 &index){
			if (nullptr == send_channel_)
				return;

			send_channel_->set_confirm_buffer_wait(index);
		}

		bool harq_linker::handle_threshold(const int &linker_handle, const uint64 &size){
			if (nullptr == send_channel_)
				return false;

			bool can_send = send_channel_->is_send_thread_threshold(size);
			if (can_send){
				send_channel_->add_send_thread_flow(size);
				return true;
			}
			else
				return false;
		}

		void harq_linker::handle_send_sack(const sack_rudp_header &sack_rudp_header_ptr){
			if(CLOSE == get_rudp_state())
				return;

			if (nullptr != send_thread_)
				send_thread_->add_buffer_no_feedback_urgent((char*)&sack_rudp_header_ptr, sizeof(sack_rudp_header_ptr), linker_record_.linker_handle_, get_remote_addr());

		}

		void harq_linker::handle_resend(std::shared_ptr<confirm_buffer> confirm_buffer_ptr, bool followed){
			if(OPEN != get_rudp_state())
				return;

			int buffer_size = sizeof(frames_buffer) - FRAMES_BUFFER_SIZE + confirm_buffer_ptr->buffer_.body_.size_;
			confirm_buffer_ptr->buffer_.body_.recv_timer_ = global_rudp_timer_.get_current_timer();

			if (nullptr != send_thread_){
				send_thread_->add_buffer_addr_urgent(confirm_buffer_ptr->segment_index_, message_data, (char*)&confirm_buffer_ptr->buffer_, buffer_size, linker_record_.linker_handle_, get_remote_addr(), followed);
			}
			return ;
		}

		bool harq_linker::handle_rst_protocol(){
			//设置状态为关闭;
			set_rudp_state(CLOSE);
			//向上层抛出断开信息;
			handle_disconnect();
			return true;
		}

		void harq_linker::handle_add_send_queue(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr){
			if (nullptr != send_thread_)
				send_thread_->add_buffer_addr(index, message_type, data, size, linker_handle, get_remote_addr());

		}
		
		void harq_linker::handle_add_send_queue_no_feekback(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr){
			if (nullptr != send_thread_)
				send_thread_->add_buffer_no_feedback(index, data, size, linker_record_.linker_handle_, get_remote_addr());

		}

		int harq_linker::handle_get_linker_handle(){
			return linker_record_.linker_handle_;
		}

		void harq_linker::handle_add_frames(std::shared_ptr<frames_record> frames_ptr){
			if(nullptr != frames_class_)
				frames_class_->add_queue(frames_ptr);

		}

		uint64 harq_linker::inc_current_send_frames_no(){
			return ++current_send_frames_no_;
		}

		int harq_linker::send_buffer(char* data, int size){
			//检测发送数据量是否正确
			if(size <= 0)
				return INVALID_SEND;

			//检测发送数据对象是否存在
			if(nullptr == send_channel_)
				return INVALID_CLASS;

			//针对云游戏，这次进行了屏蔽 2021-04-05
			// //带宽判断
			// int block_ret = send_channel_->block_bandwidth(size);
			// if (block_ret != 0)
			// {
			// 	return block_ret;
			// }

			//判断发送数据包的大小是否过大（985 * 64 KB = 61.56 MB）;
			if(((size + FRAMES_BUFFER_SIZE - 1) / FRAMES_BUFFER_SIZE) >= USHRT_MAX)
				return SIZE_OVERLOAD;

			//计入流量;
			send_channel_->add_send_chennel_flow(size);
			//发送;
			std::lock_guard<std::recursive_mutex> gurad(harq_linker_send_buffer_lock_);
			set_send_timer();
			uint64 current_send_frames_no = inc_current_send_frames_no();
			// add_log(LOG_TYPE_INFO, "harq_linker::send_buffer current_send_frames_no=%d", current_send_frames_no);
			send_channel_->add_buffer_to_send_channel(current_send_frames_no, data, size, get_remote_addr());
			return size;
		}

		void harq_linker::handle_disconnect(){
			if(!disconnected_){
				disconnected_ = true;
				harq_manager_->on_disconnect(linker_record_.linker_handle_, remote_ip(), remote_port());
			}
		}

		void harq_linker::handle_error(const int &error){
			harq_manager_->on_error(error, linker_record_.linker_handle_, remote_ip(), remote_port());
		}

		void harq_linker::set_rudp_state(const rudp_state &state){
			std::lock_guard<std::recursive_mutex> harq_linker_state_gurad(harq_linker_state_lock_);
			harq_linker_state_.rudp_state_ = state;
			harq_linker_state_.state_timer_ = global_rudp_timer_.get_current_timer();
		}

		rudp_state harq_linker::get_rudp_state(){
			return harq_linker_state_.rudp_state_;
		}

		void harq_linker::set_remote_timer(rudptimer remote_timer){
			std::lock_guard<std::recursive_mutex> remote_timer_gurad(remote_timer_lock_);
			remote_base_timer_ = remote_timer;
			local_base_timer_ = global_rudp_timer_.get_current_timer();
		}

		void harq_linker::set_remote_addr(struct sockaddr_in remote_addr){
			memset(&remote_addr_, 0, sizeof(remote_addr_));
			memcpy(&remote_addr_, &remote_addr, sizeof(remote_addr));
			std::string remote_ip = ustd::rudp_public::get_remote_ip(remote_addr);
			remote_port_ = ustd::rudp_public::get_remote_port(remote_addr);
			memcpy(remote_ip_, remote_ip.c_str(), remote_ip.length());
		}
			
		rudptimer harq_linker::get_base_timer_interval(){
			return remote_base_timer_ - local_base_timer_;
		}

		rudptimer harq_linker::get_rudp_state_timer(){
			return harq_linker_state_.state_timer_;
		}

		void harq_linker::handle_on_recv_useful(int size){
			if(recv_rate_ != nullptr)
				recv_rate_->insert_useful(static_cast<uint64>(size));

		}

		void harq_linker::handle_on_send_overall(int size){
			if(send_rate_ != nullptr)
				send_rate_->insert_overall(size);

		}
			
		void harq_linker::handle_on_send_useful(int size){
			if(send_rate_ != nullptr)
				send_rate_->insert_useful(size);

		}

		bool harq_linker::get_cumulative_timer(rudptimer *min_timer, rudptimer *max_timer, rudptimer *average_timer){
			if(frames_class_ != nullptr){
				rudptimer tmp_min_timer;
				rudptimer tmp_max_timer;
				rudptimer tmp_average_timer;
				frames_class_->get_cumulative_timer(&tmp_min_timer, &tmp_max_timer, &tmp_average_timer);
				*min_timer = tmp_min_timer;
				*max_timer = tmp_max_timer;
				*average_timer = tmp_average_timer;
				return true;
			}
			return false;
		}

		void harq_linker::handle_on_read(char* data, int size, int consume_timer){
			if(harq_manager_->on_read)
				harq_manager_->on_read(data, size, linker_record_.linker_handle_, remote_ip(), remote_port(), consume_timer);

		}

		void harq_linker::set_first_rto(const uint64 &first_rto){
			std::lock_guard<std::recursive_mutex> gurad(first_rto_lock_);
			first_rto_ = first_rto;
		}

		void harq_linker::set_start_threhold(const uint32 &start_threhold){
			std::lock_guard<std::recursive_mutex> gurad(start_threhold_lock_);
			start_threhold_ = start_threhold;
		}

		uint32 harq_linker::get_start_threhold(){
			std::lock_guard<std::recursive_mutex> gurad(start_threhold_lock_);
			return start_threhold_;
		}

		void harq_linker::set_max_threhold(const uint32 &max_threhold){
			std::lock_guard<std::recursive_mutex> gurad(max_threhold_lock_);
			max_threhold_ = max_threhold;
		}

		uint32 harq_linker::get_max_threhold(){
			std::lock_guard<std::recursive_mutex> gurad(max_threhold_lock_);
			return max_threhold_;
		}
			
		uint64 harq_linker::get_first_rto(){
			std::lock_guard<std::recursive_mutex> gurad(first_rto_lock_);
			return first_rto_;
		}

		void harq_linker::set_sys_time_stamp(const int32 &sys_server_time_stamp, const int32 &sys_client_time_stamp){
			std::lock_guard<std::recursive_mutex> gurad(sys_time_stamp_lock_);
			sys_server_time_stamp_ = sys_server_time_stamp;
			sys_client_time_stamp_ = sys_client_time_stamp;
		}
			
		int32 harq_linker::get_sys_server_time_stamp(){
			std::lock_guard<std::recursive_mutex> gurad(sys_time_stamp_lock_);
			return sys_server_time_stamp_;
		}
			
		int32 harq_linker::get_sys_client_time_stamp(){
			std::lock_guard<std::recursive_mutex> gurad(sys_time_stamp_lock_);
			return sys_client_time_stamp_;
		}

		uint64 harq_linker::get_local_rto(){
			if(nullptr != send_channel_)
				return send_channel_->get_local_rto();
			else
				return 0;
		}

		void harq_linker::set_remote_rto(const uint64 &real_timer_rto){
			std::lock_guard<std::recursive_mutex> gurad(remote_rto_lock_);
			remote_real_timer_rto_ = real_timer_rto;
		}

		void harq_linker::set_encrypted(bool encrypted){
			encrypted_ = encrypted;
		}

		void harq_linker::set_key(const std::string &key, const std::string &iv){
			key_ = key;
			iv_ = iv;
		}

		uint64 harq_linker::get_remote_real_timer_rto(){
			std::lock_guard<std::recursive_mutex> gurad(remote_rto_lock_);
			return remote_real_timer_rto_;
		}

		uint64 harq_linker::get_remote_rto(){
			uint64 rto = get_remote_real_timer_rto();
			if(0 == rto){
				rto = get_first_rto();
			}
			return rto;
		}

		void harq_linker::force_close(){
			handle_send_rst();
			set_rudp_state(CLOSE);
		}

		void harq_linker::set_current_segment_index(const uint64 &segment_index){
			current_segment_index_ = segment_index;
		}

		void harq_linker::set_current_frames_id(const uint64 &frames_id){
			current_frames_id_ = frames_id;
		}

		void harq_linker::set_current_confirm_map_size(const uint64 &current_confirm_map_size){
			current_confirm_map_size_ = current_confirm_map_size;
		}

		void harq_linker::set_current_other_group_id(const uint64 &other_group_id){
			current_other_group_id_ = other_group_id;
		}

		void harq_linker::set_current_pop_to_upper_index(const uint64 &current_pop_to_upper_index){
			current_pop_to_upper_index_ = current_pop_to_upper_index;
		}

		void harq_linker::set_current_fec_group_map_size(const uint64 &current_fec_group_map_size){
			current_fec_group_map_size_ = current_fec_group_map_size;
		}

		void harq_linker::set_current_complete_group_map_size(const uint64 &current_complete_group_map_size){
			current_complete_group_map_size_ = current_complete_group_map_size;
		}

		void harq_linker::add_log(const int log_type, const char *context, ...){
			const int array_length = 1024 * 10;
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

			if(write_log_ptr_ != nullptr)
				write_log_ptr_->write_log3(log_type, log_text);

		}

		//****************rudp管理;
		harq_manager::harq_manager(const uint16 &thread_count, const bool &showed, const std::string &log){
			#if defined(_WIN32)
				WSADATA wsa_data;
				if (WSAStartup(0x0202, &wsa_data) != 0)
				{
					return;
				}
			#endif
			//设置版本信息
			char buffer[1024] = {0};
			std::string tmp_version(buffer, sizeof(buffer)); 
			version_string_ = tmp_version;
			thread_count_ = thread_count;
			current_socket_handle_ = INIT_SOCKET_HANDLE;
			set_manager_state(MS_CREATED);
			core_log_ = log + path_separator + "core" + path_separator + "server";
			//创建日志
			write_log_ptr_ = new ustd::log::write_log(showed);
			write_log_ptr_->init("core_server", core_log_, 1);
			add_log(LOG_TYPE_INFO, "****%s**** \n", version());
			//创建tick时钟
			tick_thread_ = new rate_tick_thread();
			tick_thread_->on_rate_tick_ = std::bind(&harq_manager::handle_rate_tick, this, std::placeholders::_1, std::placeholders::_2);
			tick_thread_->init();
		}

		void harq_manager::stop_check_thread(){
			//删除监测线程;
			if(nullptr != check_thread_){
				delete check_thread_;
				check_thread_ = nullptr;
			}
		}

		void harq_manager::stop_tick_thread(){
			//删除监测线程;
			if(nullptr != tick_thread_){
				delete tick_thread_;
				tick_thread_ = nullptr;
			}
		}

		harq_manager::~harq_manager(void){
			stop_check_thread();
			harq_sleep(0);
			stop_tick_thread();
			harq_sleep(0);
			free_recv_threads();
			harq_sleep(0);
			free_harq_linker();
			#if defined(_WIN32)
				::WSACleanup();
			#endif
		}

		int harq_manager::get_socket_handle(){
			std::shared_ptr<harq_linker> harq_linker_ptr = nullptr;
			do{
				current_socket_handle_++;
				if(current_socket_handle_ < DEFAULT_HANDLE){
					current_socket_handle_ = DEFAULT_HANDLE;
				}
				harq_linker_ptr = find_harq_linker(current_socket_handle_);
			}while(nullptr != harq_linker_ptr);
			return current_socket_handle_;
		}

		void harq_manager::handle_error(const int &error, const int &linker_handle){
			if (on_error != NULL){
				std::shared_ptr<harq_linker> harq_linker_ptr = find_harq_linker(linker_handle);
				if(nullptr != harq_linker_ptr)
					on_error(error, linker_handle, harq_linker_ptr->remote_ip(), harq_linker_ptr->remote_port());
			}
		}

		void harq_manager::free_recv_threads(){
			//产生工作套接字和工作线程;
			for(auto iter = recv_threads_vector_.begin(); iter != recv_threads_vector_.end(); iter++){
				std::shared_ptr<rudp_thread> thread_ptr = *iter;
				if(nullptr != thread_ptr->recv_thread_){
					delete thread_ptr->recv_thread_;
					thread_ptr->recv_thread_ = nullptr;
				}
				free_fd(thread_ptr->thread_fd_);
			}
			recv_threads_vector_.clear();
		}

		void harq_manager::free_fd(harq_fd fd){
			if(fd != INVALID_SOCKET)
				harq_close(fd);
		}

		void harq_manager::add_log(const int log_type, const char *context, ...){
			const int array_length = 1024 * 10;
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

			if(write_log_ptr_ != nullptr)
				write_log_ptr_->write_log3(log_type, log_text);
		}

		bool harq_manager::bind_fd(const harq_fd &fd, const std::string &bind_ip, const int &port){
			//设置此套接字监听本地端口信息;
			struct sockaddr_in tmp_local_addr_;
			memset(&tmp_local_addr_, 0, sizeof(tmp_local_addr_));
			tmp_local_addr_.sin_family = AF_INET;
			tmp_local_addr_.sin_port = htons(port);
			//绑定指定的IP地址;
			if("0.0.0.0" == bind_ip)
				tmp_local_addr_.sin_addr.s_addr = htonl(INADDR_ANY);
			else
				tmp_local_addr_.sin_addr.s_addr = inet_addr(bind_ip.c_str());

			//设置套接字属性;
			uint32 use_send_threhold = (std::max)(MAX_RECV_BUFFER, get_max_threhold());
			bool ret = ustd::rudp_linux::set_fd_attribute(fd, 0, use_send_threhold);
			if(!ret){
				add_log(LOG_TYPE_ERROR, "Set Socket Attribute Fail fd %d errorid=%d errno=%s", fd, ret, strerror(errno));
				harq_close(fd);
				return false;
			}
			add_log(LOG_TYPE_INFO, "Set Fd Attribute Success fd=%d", fd);
			//绑定套接字;
			int result_int = bind(fd, (struct sockaddr *)&tmp_local_addr_, sizeof(struct sockaddr));
			if(SOCKET_ERROR == result_int){
				add_log(LOG_TYPE_ERROR, "bind Function Fail errno=%s", strerror(errno));
				harq_close(fd);
				return false;
			}
			add_log(LOG_TYPE_INFO, "Bind fd Success fd=%d", fd);
			return true;
		}

		bool harq_manager::create_fd(int port, harq_fd *fd){
			add_log(LOG_TYPE_INFO, "harq_manager::create_fd port=%d", port);
			*fd = socket(AF_INET, SOCK_DGRAM , 0);
			if(INVALID_SOCKET == *fd){
				add_log(LOG_TYPE_ERROR, "Create Socket Fail errno=%s", strerror(errno));
				return false;
			}
			add_log(LOG_TYPE_INFO, "Create fd=%d port=%d", *fd, port);
			return true;
		}

		void harq_manager::set_manager_state(const manager_state &state){
			cur_manager_state_ = state;
		}

		void harq_manager::set_port(int port){
			port_ = port;
		}

		void harq_manager::set_bind_ip(const std::string &bind_ip){
			bind_ip_ = bind_ip;
		}

		void harq_manager::set_local_addr(){
			//设置本地地址信息;
			memset(&local_addr4_, 0, sizeof(local_addr4_));
			if(bind_ip() == "0.0.0.0")
				local_addr4_.sin_addr.s_addr = htonl(INADDR_ANY);
			else
				local_addr4_.sin_addr.s_addr = inet_addr(bind_ip().c_str());		
			
			local_addr4_.sin_family = AF_INET;
			local_addr4_.sin_port = htons(port());
		}

		void harq_manager::set_delay(bool delay){
			delay_ = delay;
		}

		void harq_manager::set_delay_millisecond(int delay_millisecond){
			delay_millisecond_ = delay_millisecond;
		}

		void harq_manager::set_start_threhold(const uint32 &start_threhold){
			start_threhold_ = start_threhold;
		}
			
		uint32 harq_manager::get_start_threhold(){
			return start_threhold_;
		}

		void harq_manager::set_max_threhold(const uint32 &max_threhold){
			max_threhold_ = max_threhold;
		}

		uint32 harq_manager::get_max_threhold(){
			return max_threhold_;
		}

		void harq_manager::set_option(const std::string &attribute, const int &value){
			//首先判断当前的状态;
			if(MS_CREATED == get_manager_state()){
				std::string tmp_attribute = attribute;
				transform(tmp_attribute.begin(), tmp_attribute.end(), tmp_attribute.begin(), ::tolower);
				//根据属性设置配置;
				if("listen_port" == tmp_attribute){
					//设置监听端口;
					set_port(value);
				}else if("delay_interval" == tmp_attribute){
					//延时时长;
					set_delay_millisecond(value);
				}else if("max_threhold" == tmp_attribute){
					//最大阀值;
					set_max_threhold(value);
				}else if ("start_threhold" == tmp_attribute){
					//起始阀值
					set_start_threhold(value);
				}
			}
		}
			
		void harq_manager::set_option(const std::string &attribute, const bool &value){
			//首先判断当前的状态;
			if(MS_CREATED == get_manager_state()){
				std::string tmp_attribute = attribute;
				transform(tmp_attribute.begin(), tmp_attribute.end(), tmp_attribute.begin(), ::tolower);
				//根据属性设置配置;
				if("delay" == tmp_attribute)
					set_delay(value);
			}
		}

		void harq_manager::set_option(const std::string &attribute, const std::string &value){
			//首先判断当前的状态;
			if(MS_CREATED == get_manager_state()){
				std::string tmp_attribute = attribute;
				transform(tmp_attribute.begin(), tmp_attribute.end(), tmp_attribute.begin(), ::tolower);
				//根据属性设置配置;
				if("bind_ip" == tmp_attribute)
					//绑定端口;
					set_bind_ip(value);
			}
		}

		bool harq_manager::begin_server(){
			//判断是否已经启动
			if(MS_SETUPED == get_manager_state()){
				add_log(LOG_TYPE_ERROR, "Server Already Start Port=%d", port());
				return false;
			}
			set_manager_state(MS_SETUPED);
			//判断属性是否设置
			if(INVALID_PORT == port() || 0 == port()){
				add_log(LOG_TYPE_ERROR, "Listen Port Error Port=%d", port());
				return false;
			}
			//判断绑定的设置的IP和端口
			if(!ustd::rudp_public::socket_bind_udp_port(port(), bind_ip().c_str())){
				add_log(LOG_TYPE_ERROR, "Bind Ip bind_ip=%s Or Listen Port Not available port=%d", bind_ip().c_str(), port());
				return false;
			}
			//输出相应的启动参数;
			add_log(LOG_TYPE_INFO, "Start RUDP Server Params ---->\n \
				bind_ip=%s\n \
				listen_port=%d\n \
				delay=%d\n \
				delay_interval=%d\n \
				max_threhold=%d\n", 
				bind_ip().c_str(), 
				port(),
				delay(),
				delay_interval(),
				get_max_threhold());
			//设置相关参数;
			set_local_addr();
			//创建工作者线程;
			if(!create_recv_threads(port(), bind_ip())){
				add_log(LOG_TYPE_ERROR, "create_work_threads Failed");
				return false;
			}
			add_log(LOG_TYPE_INFO, "Create Work Thread Success");
			//创建检测线程;
			check_thread_ = new check_thread(this);
			check_thread_->write_log_ptr_ = write_log_ptr_;
			check_thread_->init();
			add_log(LOG_TYPE_INFO, "Start Server Finished");
			return true;
		}

		bool harq_manager::create_recv_threads(const int &port, const std::string &bind_ip){
			//此处注意，当没有打开端口复用的时候，只能使用1作为参数;
			if(0 == thread_count_){
				thread_count_ = 2 * ustd::rudp_public::get_cpu_cnum();
			}
			recv_threads_vector_.reserve(thread_count_);
			for(int i = 0; i < thread_count_; i++){
				std::shared_ptr<rudp_thread> thread_ptr(new rudp_thread);
				bool ret = create_fd(port, &work_fd_);
				if(!ret)
					return false;

				ret = bind_fd(work_fd_, bind_ip, port);
				if(!ret)
					return false;

				thread_ptr->postion_ = i;
				thread_ptr->local_port_ = port;
				//创建接收线程
				thread_ptr->recv_thread_ = new recv_thread(SERVER);
				thread_ptr->recv_thread_->on_syn_ = std::bind(&harq_manager::handle_syn, 
																this, 
																std::placeholders::_1, 
																std::placeholders::_2, 
																std::placeholders::_3, 
																std::placeholders::_4,
																std::placeholders::_5);
				thread_ptr->recv_thread_->on_data_ = std::bind(&harq_manager::handle_data, 
																this, 
																std::placeholders::_1, 
																std::placeholders::_2, 
																std::placeholders::_3, 
																std::placeholders::_4,
																std::placeholders::_5);
				thread_ptr->recv_thread_->write_log_ptr_ = write_log_ptr_;
				thread_ptr->recv_thread_->init(work_fd_);
				add_log(LOG_TYPE_INFO, "Create Recv Thread postion=%d", thread_ptr->postion_);
				recv_threads_vector_.push_back(thread_ptr);
			}
			return true;
		}

		bool harq_manager::handle_check_linker_state(const struct sockaddr_in &remote_addr){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(remote_addr);
			if(nullptr == linker_ptr)
				return false;

			if(CLOSE == linker_ptr->get_rudp_state())
				return false;

			return true;
		}

		void harq_manager::handle_index_state(const int &linker_handle, const uint64 &index){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(linker_handle);
			if(nullptr == linker_ptr)
				return;

			if(nullptr == linker_ptr->send_channel_)
				return;

			linker_ptr->send_channel_->set_confirm_buffer_wait(index);
		}

		bool harq_manager::handle_threshold(const int &linker_handle, const uint64 &size){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(linker_handle);
			if(nullptr == linker_ptr)
				return false;

			if(nullptr == linker_ptr->send_channel_)
				return false;

			bool can_send = linker_ptr->send_channel_->is_send_thread_threshold(size);
			if (can_send){
				linker_ptr->send_channel_->add_send_thread_flow(size);
				return true;
			}else{
				add_log(LOG_TYPE_INFO, "Current Send Flow flow=%d size=%d", linker_ptr->send_channel_->get_current_send_thread_flow(), size);
				return false;
			}
		}

		bool harq_manager::handle_syn(uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer){
			std::shared_ptr<harq_linker> tmp_linker_ptr = find_harq_linker(remote_addr);
			if(nullptr != tmp_linker_ptr && nullptr != tmp_linker_ptr->protocol_thread_){
				tmp_linker_ptr->protocol_thread_->add_queue(message_type, data, size, recv_timer, remote_addr);
				return true;
			}else{
				if(!ustd::rudp_public::is_syn_protocol(data, size))
					return false;

				syn_rudp_header *syn_rudp_header_ptr = (syn_rudp_header *)data;
				std::string remote_ip = ustd::rudp_public::get_remote_ip(remote_addr);
				int remote_port = ustd::rudp_public::get_remote_port(remote_addr);
				if(!on_checkip(remote_ip.c_str(), remote_port)){
					add_log(LOG_TYPE_ERROR, "CheckIp Result False IP %s Port %d", remote_ip.c_str(), remote_port);
					return false;
				}
				//创建一个新的连接对象;
				std::shared_ptr<harq_linker> linker_ptr_(new harq_linker(this));
				linker_ptr_->write_log_ptr_ = write_log_ptr_;
				//设置发送阀值;
				linker_ptr_->set_start_threhold(get_start_threhold());
				linker_ptr_->set_max_threhold(get_max_threhold());
				//确定这个对象应该使用的工作者接收线程对象;
				linker_ptr_->init(work_fd_, get_socket_handle());
				linker_ptr_->set_remote_addr(remote_addr);
				if(nullptr != linker_ptr_->frames_class_){
					linker_ptr_->frames_class_->init(delay(), delay_interval(), CUMULATIVE_TIMER);
				}
				linker_ptr_->linker_record_.encrypted_ = syn_rudp_header_ptr->encrypt_;
				memset(linker_ptr_->linker_record_.key_, 0, sizeof(linker_ptr_->linker_record_.key_));
				memset(linker_ptr_->linker_record_.iv_, 0, sizeof(linker_ptr_->linker_record_.iv_));
				if (linker_ptr_->linker_record_.encrypted_){
					memcpy(linker_ptr_->linker_record_.key_, syn_rudp_header_ptr->key_, syn_rudp_header_ptr->key_size_);
					memcpy(linker_ptr_->linker_record_.iv_, syn_rudp_header_ptr->iv_, syn_rudp_header_ptr->iv_size_);
				}
				linker_ptr_->set_rudp_state(LISTEN);
				add_harq_linker(linker_ptr_);
				if(nullptr != linker_ptr_->protocol_thread_){
					linker_ptr_->protocol_thread_->add_queue(message_type, data, size, recv_timer, remote_addr);
				}
				add_log(LOG_TYPE_INFO, "🔌 Create Linker Object IP=[%s] Port=[%d] Handle=[%d]", linker_ptr_->remote_ip(), linker_ptr_->remote_port(), linker_ptr_->linker_record_.linker_handle_);
				//回调连接成功;
				if(on_connect){
					on_connect(linker_ptr_->remote_ip(), linker_ptr_->remote_port(), linker_ptr_->linker_record_.linker_handle_, (linker_ptr_->local_base_timer_ - linker_ptr_->remote_base_timer_));
				}
				return true;
			}
		}

		bool harq_manager::handle_data(uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(remote_addr);
			if(nullptr == linker_ptr){
				return false;
			}
			if(CLOSE == linker_ptr->get_rudp_state()){
				return false;
			}
			if(nullptr == linker_ptr->protocol_thread_){
				return false;
			}
			linker_ptr->protocol_thread_->add_queue(message_type, data, size, recv_timer, remote_addr);
			return true;
		}

		int harq_manager::send_buffer(char *data, int size, int linker_handle){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(linker_handle);
			if(nullptr == linker_ptr){
				return -1;
			}
			if(OPEN != linker_ptr->get_rudp_state()){
				add_log(LOG_TYPE_ERROR, "Linker Object State is`t OPEN state=%d linker_handle=%d", linker_ptr->get_rudp_state(), linker_handle);
				return -1;
			}
			return linker_ptr->send_buffer(data, size);
		}

		int harq_manager::send_buffer(char *data, int size, struct sockaddr_in addr_ptr){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(addr_ptr);
			if(nullptr == linker_ptr){
				std::string remote_ip = ustd::rudp_public::get_remote_ip(addr_ptr);
				int remote_port = ustd::rudp_public::get_remote_port(addr_ptr);
				add_log(LOG_TYPE_ERROR, "Object Not Found remote ip=%s remote port=%d", remote_ip.c_str(), remote_port);
				return -1;
			}
			if(OPEN != linker_ptr->get_rudp_state()){
				std::string remote_ip = ustd::rudp_public::get_remote_ip(addr_ptr);
				int remote_port = ustd::rudp_public::get_remote_port(addr_ptr);
				add_log(LOG_TYPE_ERROR, "Object State Error remote ip=%s remote port=%d state=%d", remote_ip.c_str(), remote_port, linker_ptr->get_rudp_state());
				return -1;
			}
			return linker_ptr->send_buffer(data, size);
		}

		void harq_manager::close_linker(const int &linker_handle){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(linker_handle);
			if(nullptr == linker_ptr)
				return;

			if(OPEN == linker_ptr->get_rudp_state()){
				linker_ptr->handle_send_rst();
				linker_ptr->set_rudp_state(CLOSE);
				linker_ptr->handle_disconnect();
			}
		}

		void harq_manager::close_linker(const struct sockaddr_in &addr_ptr){
			std::shared_ptr<harq_linker> linker_ptr = find_harq_linker(addr_ptr);
			if(nullptr == linker_ptr)
				return;

			if(OPEN == linker_ptr->get_rudp_state()){
				linker_ptr->handle_send_rst();
				linker_ptr->set_rudp_state(CLOSE);
				linker_ptr->handle_disconnect();
			}
		}

		void harq_manager::add_harq_linker(std::shared_ptr<harq_linker> harq_linker_ptr){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			long long tmp_addr = ustd::rudp_public::get_address(harq_linker_ptr->get_remote_addr());
			address_postion_map_.insert(std::make_pair(tmp_addr, harq_linker_ptr));
			linker_postion_map_.insert(std::make_pair(harq_linker_ptr->linker_record_.linker_handle_, harq_linker_ptr));
		}

		void harq_manager::delete_harq_linker(std::shared_ptr<harq_linker> harq_linker_ptr){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			linker_map::iterator iter = linker_postion_map_.find(harq_linker_ptr->linker_record_.linker_handle_);
			if(iter != linker_postion_map_.end()){
				linker_postion_map_.erase(iter);
			}
			long long tmp_addr = ustd::rudp_public::get_address(harq_linker_ptr->get_remote_addr());
			address_map::iterator addr_iter = address_postion_map_.find(tmp_addr);
			if(addr_iter != address_postion_map_.end()){
				address_postion_map_.erase(addr_iter);
			}
		}

		std::shared_ptr<harq_linker> harq_manager::find_harq_linker(const int &linker_handle){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			linker_map::iterator iter = linker_postion_map_.find(linker_handle);
			if(iter != linker_postion_map_.end()){
				return iter->second;
			}
			return nullptr;
		}

		std::shared_ptr<harq_linker> harq_manager::find_harq_linker(struct sockaddr_in addr_ptr){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			long long tmp_addr = ustd::rudp_public::get_address(addr_ptr);
			address_map::iterator linker_iter = address_postion_map_.find(tmp_addr);
			if(linker_iter != address_postion_map_.end()){
				return linker_iter->second;
			}
			return nullptr;
		}

		void harq_manager::free_harq_linker(){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			address_postion_map_.clear();
			linker_postion_map_.clear();
		}

		rudp_state harq_manager::get_rudp_state(const int &linker_handle){
			std::shared_ptr<harq_linker> tmp_harq_linker = find_harq_linker(linker_handle);
			if(tmp_harq_linker != nullptr)
				return tmp_harq_linker->get_rudp_state();
			else
				return CLOSE;
		}

		void harq_manager::check_timeout(){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			for (auto iter = linker_postion_map_.begin(); iter != linker_postion_map_.end();){
				std::shared_ptr<harq_linker> linker_ptr = iter->second;
				int second = static_cast<int>(global_rudp_timer_.timer_interval(linker_ptr->get_recv_timer()));
				if (second > CHECK_TIME_OUT){
					std::string remote_ip = ustd::rudp_public::get_remote_ip(linker_ptr->get_remote_addr());
					int remote_port = ustd::rudp_public::get_remote_port(linker_ptr->get_remote_addr());
					if(OPEN == linker_ptr->get_rudp_state()){
						linker_ptr->handle_send_rst();
						linker_ptr->set_rudp_state(CLOSE);
						linker_ptr->handle_disconnect();
					}
					//从地址中删除;
					long long addr = ustd::rudp_public::get_address(linker_ptr->get_remote_addr());
					address_map::iterator addr_iter = address_postion_map_.find(addr);
					if(addr_iter != address_postion_map_.end()){
						address_postion_map_.erase(addr_iter);
					}
					//从列表中删除;
					iter = linker_postion_map_.erase(iter);
					add_log(LOG_TYPE_INFO, "⛔ Socket TimeOut Send IP=[%s] Port=[%d] Handle=[%d]", remote_ip.c_str(), remote_port, linker_ptr->linker_record_.linker_handle_);
				}else{
					iter++;
				}
			}
		}

		void harq_manager::check_rto(){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			for (auto iter = linker_postion_map_.begin(); iter != linker_postion_map_.end(); iter++){
				std::shared_ptr<harq_linker> linker_ptr = iter->second;
				if(OPEN == linker_ptr->get_rudp_state()){
					if(on_rto != nullptr){
						on_rto(linker_ptr->remote_ip(), linker_ptr->remote_port(), linker_ptr->get_local_rto(), linker_ptr->get_remote_rto());
					}
				}
			}
		}

		void harq_manager::handle_rate_tick(const rudptimer &current_tick, const rudptimer &interval){
			std::lock_guard<std::recursive_mutex> gurad(linker_lock_);
			for (auto iter = linker_postion_map_.begin(); iter != linker_postion_map_.end(); iter++){
				std::shared_ptr<harq_linker> linker_ptr = iter->second;
				if(OPEN == linker_ptr->get_rudp_state()){
					//得到发送速率
					uint64 overall_send_size = linker_ptr->send_rate_->get_overall();
					std::string linker_send_overall_speed = ustd::rudp_public::get_speed(overall_send_size);
					//得到接收速率
					uint64 overall_recv_size = linker_ptr->recv_rate_->get_overall();
					std::string linker_recv_overall_speed = ustd::rudp_public::get_speed(overall_recv_size);
					if (nullptr == linker_ptr->send_channel_)
						continue;

					//得到当前速率
					std::string cur_bandwidth = ustd::rudp_public::get_speed(linker_ptr->send_channel_->get_current_max_bandwidth());
					//通知上层;
					if (on_rate != nullptr){
						on_rate(linker_ptr->remote_ip(), linker_ptr->remote_port(), 0, linker_ptr->recv_rate_->get_useful());
					}
					rudptimer min_timer;
					rudptimer max_timer;
					rudptimer average_timer;
					if (!linker_ptr->get_cumulative_timer(&min_timer, &max_timer, &average_timer)){
						min_timer = 0;
						max_timer = 0;
						average_timer = 0;
					}
					check_log(linker_ptr, linker_send_overall_speed, linker_recv_overall_speed, cur_bandwidth);
					//清空;
					if(linker_ptr->recv_rate_ != nullptr)
						linker_ptr->recv_rate_->reset_rate();

					if(linker_ptr->send_rate_ != nullptr)
						linker_ptr->send_rate_->reset_rate();

					if (linker_ptr->send_channel_ != nullptr){
						linker_ptr->send_channel_->check_bandwidth();
						linker_ptr->send_channel_->check_flow();
					}

					if(linker_ptr->send_thread_ != nullptr)
						linker_ptr->send_thread_->check_send_flow(linker_ptr->send_channel_->get_current_max_bandwidth());
				}
			}
		}

		void harq_manager::check_log(std::shared_ptr<harq_linker> linker_ptr,std::string linker_send_overall_speed,std::string linker_recv_overall_speed,std::string cur_bandwidth){
			#ifdef x86
				add_log(LOG_TYPE_INFO, "Handle=%d Local_RTO=%d Remote_RTO=%d Overall Send=%s Overall Recv=%s Bandwidth=%s",
				linker_ptr->linker_record_.linker_handle_,
				linker_ptr->get_local_rto(),
				linker_ptr->get_remote_real_timer_rto(),
				linker_send_overall_speed.c_str(),
				linker_recv_overall_speed.c_str(),
				cur_bandwidth.c_str()
				);
			#else
				add_log(LOG_TYPE_INFO, "Handle=%d Local_RTO=%lld Remote_RTO=%lld Overall Send=%s Overall Recv=%s  Bandwidth=%s",
				linker_ptr->linker_record_.linker_handle_,
				linker_ptr->get_local_rto(),
				linker_ptr->get_remote_real_timer_rto(),
				linker_send_overall_speed.c_str(),
				linker_recv_overall_speed.c_str(),
				cur_bandwidth.c_str()
				);
			#endif
		}
	}
}
