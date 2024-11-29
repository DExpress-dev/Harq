

#include "recv_thread.h"
#include <string.h>

#include "../include/rudp_def.h"
#include "../include/rudp_timer.h"

#if defined(_WIN32)
	#include <Winsock2.h>
	#include <ws2tcpip.h>
	#define harq_get_error() GetLastError()
#else
	#include <arpa/inet.h>
	#define harq_get_error() errno
#endif

recv_thread::recv_thread(service_mode mode){
	current_state_ = tst_init;
	mode_ = mode;
}

void recv_thread::init(const harq_fd &fd){
	fd_ = fd;
	thread_ptr_ = std::thread(&recv_thread::execute, this);
	thread_ptr_.detach();
}

recv_thread::~recv_thread(void){
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
			ustd::rudp_public::sleep_delay(50, Millisecond);
		}
	}
}

bool recv_thread::is_abnormal_header(char* data, int size, uint8 &message_type){
	if(size < static_cast<int>(sizeof(rudp_header)))
		return true;
	
	struct rudp_header *tmp_header = (struct rudp_header *)data;
	if(message_syn == tmp_header->message_type_ ||
		message_ack == tmp_header->message_type_ ||
		message_sack == tmp_header->message_type_ ||
		message_syn_ack == tmp_header->message_type_ ||
		message_rst == tmp_header->message_type_ ||
		message_nul == tmp_header->message_type_ ||
		message_error == tmp_header->message_type_ ||
		message_data == tmp_header->message_type_ ||
		message_trouble == tmp_header->message_type_ ||
		message_sysc_time == tmp_header->message_type_){
		message_type = tmp_header->message_type_;
		return false;
	}
	return true;
}

bool recv_thread::is_abnormal_header2(char* data, int size, uint8 &message_type){
	struct rudp_header header;
	if(size >= static_cast<int>(sizeof(header))){
		memset(&header, 0, sizeof(header));
		memcpy(&header, data, size);
		if(message_syn == header.message_type_ ||
		message_ack == header.message_type_ ||
		message_sack == header.message_type_ ||
		message_syn_ack == header.message_type_ ||
		message_rst == header.message_type_ ||
		message_nul == header.message_type_ ||
		message_error == header.message_type_ ||
		message_data == header.message_type_ ||
		message_trouble == header.message_type_ ||
		message_sysc_time == header.message_type_){
			message_type = header.message_type_;
			return false;
		}		
	}
	return true;
}

void recv_thread::handle_recv(char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer){
	uint8 message_type = 0;
	bool ret = is_abnormal_header(data, size, message_type);
	if(ret){
		add_log(LOG_TYPE_ERROR, "is_abnormal_header False");
		return;
	}

	switch (message_type){
		case message_syn:
		{
			if(nullptr == on_syn_)
				return;

			bool syn_ret = on_syn_(message_type, data, size, remote_addr, recv_timer);
			if(!syn_ret)
			{
				std::string ip = ustd::rudp_public::get_remote_ip(remote_addr);
				int port = ustd::rudp_public::get_remote_port(remote_addr);
				add_log(LOG_TYPE_INFO, "recv_thread::handle_recv on_syn_ result is false remote_ip=%s remote_port=%d", ip.c_str(), port);
			}
			return;
		}
		case message_ack:       		//直接返回ack协议;
		case message_syn_ack:   		//链接返回ack协议;
		case message_sack:      		//具有附加sack;
		case message_rst:				//重启协议;
		case message_nul:       		//心跳协议;
		case message_error:     		//容错协议;
		case message_data:      		//数据协议;
		case message_trouble:			//纠错协议;
		case message_sysc_time:			//时钟同步协议;
		{
			if(nullptr == on_data_)
				return;

			on_data_(message_type, data, size, remote_addr, recv_timer);
			break;
		}
		default:
		{
			add_log(LOG_TYPE_ERROR, "Not Found Message Type message_type=%d", message_type);
			return;
		}
	}
}

void recv_thread::execute(){
	struct sockaddr_in remote_addr_;
	int addr_len = sizeof(struct sockaddr_in);
	current_state_ = tst_runing;
	char recv_buffer_[4 * KB] = {0};
	while(tst_runing == current_state_){
		memset(recv_buffer_, 0, sizeof(recv_buffer_));
		memset(&remote_addr_, 0, sizeof(remote_addr_));
		size_t result = recvfrom(fd_, recv_buffer_, sizeof(recv_buffer_), 0, (struct sockaddr *)(&remote_addr_), (socklen_t*)(&addr_len));
		if(SOCKET_ERROR == static_cast<int>(result) || 0 == static_cast<int>(result)){
			//这里需要注意，如果返回10054，说明为设置SIO_UDP_CONNRESET属性，此设置只针对windows系统。
			#if defined(_WIN32)

				add_log(LOG_TYPE_ERROR, "Recv From Function Error fd=%d GetLastError=%d result=%d", fd_, GetLastError(), result);
			#else

				add_log(LOG_TYPE_ERROR, "Recv From Function Error fd=%d errno=%s result=%d", fd_, strerror(errno), result);
			#endif

			break;
		}else if(static_cast<int>(result) > 0){
			handle_recv(recv_buffer_, static_cast<int>(result), remote_addr_, global_rudp_timer_.get_current_timer());
        }
    }
	add_log(LOG_TYPE_INFO, "recv_thread::execute end");
	current_state_ = tst_stoped;
}

void recv_thread::add_log(const int log_type, const char *context, ...){
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

	if (nullptr != write_log_ptr_)
		write_log_ptr_->write_log3(log_type, log_text);
}
