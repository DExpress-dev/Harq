#pragma once

#ifndef HARQ_SERVER_H_
#define HARQ_SERVER_H_

#include "../../header/include/rudp_public.h"

#include <thread>
#include <list>
#include <mutex>
#include <vector>

#include "../../../header/write_log.h"
#include "../../header/group/rudp_group.h"
#include "../../header/channel/recv_channel.h"
#include "../../header/channel/send_channel.h"
#include "../../header/rate/rate_channel.h"
#include "../../header/rate/rate_timer.h"
#include "../../header/thread/recv_thread.h"
#include "../../header/thread/send_thread.h"

//类前置;	
class recv_channel;
class send_channel;
class rate_channel;
class rate_tick_thread;
class rudp_group;
class frames_class;
class recv_thread;
class send_thread;

const std::string SERVER_VERSION = "2.0.1";

namespace ustd{
	namespace harq_server{
		const int DEFAULT_HANDLE			= 1000;
		const int DEFAULT_THREAD_COUNT		= 0;
		const int SIZE_ERROR				= -1;
		const int BIND_ERROR				= -2;
		const int CREATE_FD_ERROR			= -3;

		struct harq_linker_state{
			rudp_state rudp_state_;
			rudptimer state_timer_;
		};
		struct rudp_buffer{
			uint8 message_type_;
			char buffer_[BUFFER_SIZE];
			int size_;
			int linker_handle_;
			struct sockaddr_in remote_addr_;
			rudptimer recv_timer_;
			rudp_buffer *next_;
		};

		struct rudp_thread{
			uint16 postion_;
			uint16 local_port_;
			int thread_fd_;
			recv_thread *recv_thread_;	//接收线程;
		};
		typedef std::vector<std::shared_ptr<rudp_thread>> recv_threads_vector;

		//manager的状态
		enum manager_state{
			MS_NONE,		//初始状态
			MS_CREATED,		//创建状态
			MS_SETUPED,		//启动状态
			MS_DELETED		//删除状态
		};

		//类前置;
		class harq_linker;
		class harq_manager;
		class protocol_thread;
		class check_thread;
		typedef std::map<int, std::shared_ptr<harq_linker>> linker_map;
		typedef std::map<long long, std::shared_ptr<harq_linker>> address_map;
		typedef std::map<uint64, uint64> index_trouble_map;

		//回调函数定义;
		typedef std::function<void (const char* remote_ip, const int &remote_port, const int &linker_handle, const long long &time_stamp)> ON_CONNECT;
		typedef std::function<void (const int &error, const int &linker_handle, const char* remote_ip, const int &remote_port)> ON_ERROR;
		typedef std::function<bool (const char* data, const int &size, const int &linker_handle, const char* remote_ip, const int &remote_port, const int &consume_timer)> ON_RECEIVE;
		typedef std::function<void (const int &linker_handle, const char* remote_ip, const int &remote_port)> ON_DISCONNECT;
		typedef std::function<bool (const char* remote_ip, const int &remote_port)> ON_CHECKIP;
		typedef std::function<void (const char* remote_ip, const int &remote_port, const int &local_rto, const int &remote_rto)> ON_RTO;
		typedef std::function<void (const char* remote_ip, const int &remote_port, const unsigned int &send_rate, const unsigned int &recv_rate)> ON_RATE;

		//协议拆分线程;
		class protocol_thread{
		public:
			harq_linker *harq_linker_ = nullptr;
			protocol_thread(harq_linker *parent);
			~protocol_thread(void);
			void init();
		public:
			std::recursive_mutex rudp_buffer_lock_;
			rudp_buffer *first_ = nullptr, *last_ = nullptr;
			void add_queue(uint8 message_type, char *data, int size, rudptimer recv_timer, struct sockaddr_in remote_addr);
			void free_queue();
		public:
			void protocol_dispense();
			bool handle_ack_protocol(char *data, int size, struct sockaddr_in &remote_addr);
			bool handle_sack_protocol(char *data, int size, struct sockaddr_in &remote_addr);
			bool handle_syn_protocol(char *data, int size, struct sockaddr_in &remote_addr);
			bool handle_null_protocol(char *data, int size, struct sockaddr_in &remote_addr);
			bool handle_rst_protocol(char *data, int size, struct sockaddr_in &remote_addr);
			bool handle_sysc_time_protocol(char *data, int size, struct sockaddr_in &remote_addr);
		public:
			thread_state_type current_state_ = tst_init;
			std::thread thread_ptr_;
			void execute();
		public:
			std::string core_log_ = "";
			ustd::log::write_log* write_log_ptr_ = nullptr;
			void add_log(const int log_type, const char *context, ...);
		};

		//检测线程
		class check_thread{
		public:
			harq_manager *harq_manager_ = nullptr;
			thread_state_type current_state_ = tst_init;
			check_thread(harq_manager *parent);
			~check_thread(void);
			void init();
		public:
			time_t last_check_timer_ = time(nullptr);
			void check();
		public:
			std::thread thread_ptr_;
			void execute();
		public:
			std::string core_log_ = "";
			ustd::log::write_log* write_log_ptr_ = nullptr;
			void add_log(const int log_type, const char *context, ...);
		};

		class harq_linker{
		public:
			void handle_send_nul();
			void handle_send_rst();
			void handle_send_ack_syn(const uint64 &ack_index, const uint64 &segment_index, const rudptimer &client_timer);
			void handle_send_error(const error_buffer &error_buffer_ptr);
			void handle_send_ack(const ack_rudp_header &ack_rudp_header_ptr);
			void handle_send_sack(const sack_rudp_header &sack_rudp_header_ptr);
			void handle_resend(std::shared_ptr<confirm_buffer> confirm_buffer_ptr, bool followed);
			void handle_error(const int &error);
			bool handle_rst_protocol();
		public:
			bool disconnected_ = false;
			void handle_disconnect();
		public:
			void handle_add_packet(uint8 message_type, char *data, int size, rudptimer recv_timer, bool fec_created);
			void handle_add_frames(std::shared_ptr<frames_record> frames_ptr);
			void handle_add_send_queue(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr);
			void handle_add_send_queue_no_feekback(uint64 index, uint8 message_type, char *data, int size, int linker_handle, struct sockaddr_in addr);
			int handle_get_linker_handle();
			uint64 handle_get_index();
			void handle_add_log(const int log_type, const char *context);
		public:
			bool handle_check_linker_state(const struct sockaddr_in &remote_addr);
			void handle_index_state(const int &linker_handle, const uint64 &index);
			bool handle_threshold(const int &linker_handle, const uint64 &size);
		public:
			uint64 null_index_ = 0;
			uint64 null_index();
			uint64 client_null_index_ = 0;
		public:
			void directly_index_ack(const uint64 &ack_index);
		public:
			harq_manager *harq_manager_ = nullptr;
			harq_linker(harq_manager *parent);	
			~harq_linker(void);
		public:
			send_channel *send_channel_ = nullptr;
			recv_channel *recv_channel_ = nullptr;
			rudp_group *rudp_group_ = nullptr;
			frames_class *frames_class_ = nullptr;
			protocol_thread *protocol_thread_ = nullptr;
			rate_channel *recv_rate_ = nullptr;
			rate_channel *send_rate_ = nullptr;
			client_send_thread *send_thread_ = nullptr;
			void kill_class();
		public:
			std::recursive_mutex rudp_socket_record_lock_;
			linker_record linker_record_;
			void init(harq_fd work_fd, int linker_handle);
		public:
			std::recursive_mutex current_send_frames_no_lock_;
			uint64 current_send_frames_no_ =  INIT_FRAMES_NO;
			uint64 inc_current_send_frames_no();
		public:
			void force_close();
		public:
			uint64 get_local_rto();			//得到本地实时rto
			uint64 get_remote_rto();		//得到远端rto（非实时）
		public:
			std::recursive_mutex first_rto_lock_;
			uint64 first_rto_ = 0;
			void set_first_rto(const uint64 &first_rto);
			uint64 get_first_rto();
		public:
			std::recursive_mutex start_threhold_lock_;
			uint32 start_threhold_ = 1 * MB;
			void set_start_threhold(const uint32 &start_threhold);
			uint32 get_start_threhold();
		public:
			//最大带宽限制;
			std::recursive_mutex max_threhold_lock_;
			uint32 max_threhold_ = MAX_BANDWIDTH;
			void set_max_threhold(const uint32 &max_threhold);
			uint32 get_max_threhold();
		public:
			//远端RTO
			std::recursive_mutex remote_rto_lock_;
			uint64 remote_real_timer_rto_ = 0;
			void set_remote_rto(const uint64 &real_timer_rto);
			uint64 get_remote_real_timer_rto();
		public:
			//传输加密;
			bool encrypted_ = false;
			void set_encrypted(bool encrypted);
			const bool encrypted() const							{return encrypted_;}
		public:
			//传输加密使用的aes;
			std::string key_ = "";
			std::string iv_ = "";
			void set_key(const std::string &key, const std::string &iv);
			const std::string get_key() const						{return key_;}
			const std::string get_iv() const						{return iv_;}
		public:
			//计算系统时间差(服务端时间-客户端时间)
			std::recursive_mutex sys_time_stamp_lock_;
			int32 sys_client_time_stamp_ = 0;
			int32 sys_server_time_stamp_ = 0;
			void set_sys_time_stamp(const int32 &sys_server_time_stamp, const int32 &sys_client_time_stamp);
			int32 get_sys_server_time_stamp();
			int32 get_sys_client_time_stamp();
		public:
			//发送数据;
			std::recursive_mutex harq_linker_send_buffer_lock_;
			int send_buffer(char* data, int size);
		public:
			//连接状态接口;
			std::recursive_mutex harq_linker_state_lock_;
			harq_linker_state harq_linker_state_;
			void set_rudp_state(const rudp_state &state);
			rudp_state get_rudp_state();
			rudptimer get_rudp_state_timer();

		public:
			uint64 get_send_max_index();

			//流量统计接口;
		public:
			void handle_on_read(char* data, int size, int consume_timer);

		public:
			//接收速率 
			void handle_on_recv_useful(int size);
			
			//发送速率
			void handle_on_send_overall(int size);
			void handle_on_send_useful(int size);

		public:
			//远端IP
			struct sockaddr_in remote_addr_;
			char remote_ip_[16] = { 0 };
			int remote_port_ = -1;
			void set_remote_addr(struct sockaddr_in remote_addr);
			const struct sockaddr_in get_remote_addr() const 	{return remote_addr_;}
			const char* remote_ip() const						{return remote_ip_;}
			const int remote_port() const						{return remote_port_;}

		public:
			//记录远端时间和当前时间;
			std::recursive_mutex remote_timer_lock_;
			rudptimer remote_base_timer_;
			rudptimer local_base_timer_;
			void set_remote_timer(rudptimer remote_timer);
			const rudptimer remote_base_timer() const 		{return remote_base_timer_;}
			const rudptimer local_base_timer() const 		{return local_base_timer_;}
			rudptimer get_base_timer_interval();

		public:
			bool get_cumulative_timer(rudptimer *min_timer, rudptimer *max_timer, rudptimer *average_timer);

		public:	
			void set_recv_timer()							{linker_record_.last_recv_timer_ = global_rudp_timer_.get_current_timer();}
			void set_send_timer()							{linker_record_.last_send_timer_ = global_rudp_timer_.get_current_timer();}
			const rudptimer get_recv_timer() const			{return linker_record_.last_recv_timer_;}
			const rudptimer get_send_timer() const			{return linker_record_.last_send_timer_;}
			const rudptimer get_rudp_state_timer() const	{return harq_linker_state_.state_timer_;}
			const time_t get_close_timer() const			{return linker_record_.close_time_;}
			const time_t get_free_timer() const				{return linker_record_.free_time_;}
			const bool get_encrypt() const 					{return linker_record_.encrypted_;}

			//用于监控使用的一些参数信息;
		private:
			uint64 current_segment_index_ = 0;
			uint64 current_frames_id_ = 0;
			uint64 current_confirm_map_size_ = 0;
			uint64 current_other_group_id_ = 0;
			uint64 current_pop_to_upper_index_ = 0;
			uint64 current_fec_group_map_size_ = 0;
			uint64 current_complete_group_map_size_ = 0;

		public:
			void set_current_segment_index(const uint64 &segment_index);
			void set_current_frames_id(const uint64 &frames_id);
			void set_current_confirm_map_size(const uint64 &current_confirm_map_size);
			void set_current_other_group_id(const uint64 &other_group_id);
			void set_current_pop_to_upper_index(const uint64 &current_pop_to_upper_index);
			void set_current_fec_group_map_size(const uint64 &current_fec_group_map_size);
			void set_current_complete_group_map_size(const uint64 &current_complete_group_map_size);

			const uint64 get_current_segment_index() const				{return current_segment_index_;}
			const uint64 get_current_frames_id() const					{return current_frames_id_;}
			const uint64 get_current_confirm_map_size() const			{return current_confirm_map_size_;}
			const uint64 get_current_other_group_id() const 			{return current_other_group_id_;}
			const uint64 get_current_pop_to_upper_index() const 		{return current_pop_to_upper_index_;}
			const uint64 get_current_fec_group_map_size() const 		{return current_fec_group_map_size_;}
			const uint64 get_current_complete_group_map_size() const 	{return current_complete_group_map_size_;}

		public:
			std::string core_log_ = "";
			ustd::log::write_log* write_log_ptr_ = nullptr;
			void add_log(const int log_type, const char *context, ...);
		};

		class harq_manager 
		{
		public:
			ON_CONNECT on_connect = nullptr;
			ON_RECEIVE on_read = nullptr;
			ON_DISCONNECT on_disconnect = nullptr;
			ON_ERROR on_error = nullptr;
			ON_CHECKIP on_checkip = nullptr;
			ON_RTO on_rto = nullptr;
			ON_RATE on_rate = nullptr;

		private:
			void handle_error(const int &error, const int &linker_handle);
			bool handle_check_linker_state(const struct sockaddr_in &remote_addr);
			void handle_index_state(const int &linker_handle, const uint64 &index);
			bool handle_threshold(const int &linker_handle, const uint64 &size);
			bool handle_syn(uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer);
			bool handle_data(uint8 message_type, char *data, int size, struct sockaddr_in remote_addr, rudptimer recv_timer);		

		public:
			std::string version_string_ = "";
			harq_manager(const uint16 &thread_count = DEFAULT_THREAD_COUNT, const bool &showed = true, const std::string &log = "server_log");
			~harq_manager(void);
			const char* version() const 	{return version_string_.c_str();}

		private:
			//句柄管理;
			int current_socket_handle_ = INIT_SOCKET_HANDLE;
			int get_socket_handle();

		private:
			//对象管理;
			std::recursive_mutex linker_lock_;
			linker_map linker_postion_map_;
			address_map address_postion_map_;
			void add_harq_linker(std::shared_ptr<harq_linker> harq_linker_ptr);
			void delete_harq_linker(std::shared_ptr<harq_linker> harq_linker_ptr);
			std::shared_ptr<harq_linker> find_harq_linker(const int &linker_handle);
			std::shared_ptr<harq_linker> find_harq_linker(struct sockaddr_in addr_ptr);
			void free_harq_linker();

		public:
			//监测管理;
			void check_timeout();
			void check_rto();

		private:
			//属性设置;
			unsigned int last_rand_ = 0;
			rudp_state get_rudp_state(const int &linker_handle);

		private:
			//管理类状态
			manager_state cur_manager_state_ = MS_NONE;
			void set_manager_state(const manager_state &state);
			manager_state get_manager_state() const		{return cur_manager_state_;}

		public:
			/****透出函数****/
			bool begin_server();
			void set_option(const std::string &attribute, const std::string &value);
			void set_option(const std::string &attribute, const int &value);
			void set_option(const std::string &attribute, const bool &value);
			
			//请求发送数据;
			int send_buffer(char *data, int size, int linker_handle);
			int send_buffer(char *data, int size, struct sockaddr_in addr_ptr);
			//关闭连接;
			void close_linker(const int &linker_handle);
			void close_linker(const struct sockaddr_in &addr_ptr);

		public:
			uint32 start_threhold_ = START_BANDWIDTH;
			void set_start_threhold(const uint32 &start_threhold);
			uint32 get_start_threhold();

		public:
			uint32 max_threhold_ = MAX_BANDWIDTH;
			void set_max_threhold(const uint32 &max_threhold);
			uint32 get_max_threhold();
			
		private:
			check_thread *check_thread_ = nullptr;
			rate_tick_thread *tick_thread_ = nullptr;
			void handle_rate_tick(const rudptimer &current_tick, const rudptimer &interval);
			void stop_tick_thread();
			void stop_check_thread();

		public:
			//工作者线程管理;
			uint16 thread_count_ = 0;
			recv_threads_vector recv_threads_vector_;
			bool create_recv_threads(const int &port, const std::string &bind_ip);
			void free_recv_threads();

		private:
			//套接字管理
			bool create_fd(int port, harq_fd *fd);
			bool bind_fd(const harq_fd &fd, const std::string &bind_ip, const int &port);
			void free_fd(harq_fd fd);

		private:
			harq_fd work_fd_;

		public:
			//监听端口;
			int port_ = 41002;
			void set_port(int port);
			const int port() const 		{return port_;}

		private:
			//绑定的IP
			std::string bind_ip_ = "0.0.0.0";
			void set_bind_ip(const std::string &bind_ip);
			std::string bind_ip() const 	{return bind_ip_;}

		public:
			//本地地址;
			struct sockaddr_in local_addr4_;
			void set_local_addr();
			const struct sockaddr_in get_local_addr4() const		{return local_addr4_;}

		public:
			//是否人为延迟;
			bool delay_ = false;
			void set_delay(bool delay);
			const bool delay() const								{return delay_;}

			//延迟毫秒数;
			int delay_millisecond_ = 2000;
			void set_delay_millisecond(int delay_millisecond);
			const int delay_interval() const						{return delay_millisecond_;}

		public:
			void check_log(std::shared_ptr<harq_linker> linker_ptr, std::string linker_send_overall_speed, std::string linker_recv_overall_speed, std::string cur_bandwidth);

		public:
			//日志
			std::string core_log_ = "";
			ustd::log::write_log* write_log_ptr_ = nullptr;
			void add_log(const int log_type, const char *context, ...);

		public:
			const int get_client_count() const		{return static_cast<int>(linker_postion_map_.size());};

		public:
			static harq_manager *get_instance()
			{
				static harq_manager *m_pInstance = NULL;
				if (m_pInstance == NULL)  
				{
					m_pInstance = new harq_manager();
				}
				return m_pInstance; 
			}
		};
	}
}

#endif  // HARQ_SERVER_H_

