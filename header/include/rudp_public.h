#pragma once

#ifndef RUDP_PUBLIC_H_
#define RUDP_PUBLIC_H_

#if defined(_WIN32)
	#include <Winsock2.h>

	#include <string.h>
	#include "rudp_def.h"
	#include "rudp_timer.h"

	#define IPTOS_LOWDELAY		0x10
	#define IPTOS_THROUGHPUT	0x08
	#define IPTOS_RELIABILITY	0x04
	#define IPTOS_MINCOST		0x02

#else
	#include <string.h>
	#include "rudp_def.h"
	#include "rudp_timer.h"

	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <unistd.h>
#endif

static rudp_timer global_rudp_timer_;

namespace ustd
{
	class rudp_public
	{
	public:
		rudp_public(void);
		~rudp_public(void);

	private:
		int digits(int num);
		std::string int_2_string(int num);
		
	public:
		static rudp_header *get_header(char *data, const int &size);
		static rudp_header get_header2(char *data, const int &size);

		static ack_rudp_header *get_ack_header(char *data, const int &size);
		static ack_rudp_header get_ack_header2(char *data, const int &size);

		static sack_rudp_header *get_sack_header(char *data, const int &size);
		static sack_rudp_header get_sack_header2(char *data, const int &size);

		static syn_rudp_header *get_syn_header(char *data, const int &size);
		static syn_rudp_header get_syn_header2(char *data, const int &size);
		
		static ack_syn_rudp_header *get_ack_syn_header(char *data, int size);
		static ack_syn_rudp_header get_ack_syn_header2(char *data, int size);

		static null_rudp_header *get_null_header(char *data, const int &size);
		static null_rudp_header get_null_header2(char *data, const int &size);

		static sysc_time_header *get_sysc_time_header(char *data, const int &size);
		static sysc_time_header get_sysc_time_header2(char *data, const int &size);
		
		static bool is_abnormal_header(char* data, int size);
		static bool is_abnormal_header2(char* data, int size);
		
		static bool is_abnormal_header(char* data, int size, uint8 &message_type);
		static bool is_abnormal_header2(char* data, int size, uint8 &message_type);
		
		static bool is_syn_protocol(char* data, int size);
		static bool is_syn_protocol2(char* data, int size);

		static void get_groupid_postion(uint64 segment_index, uint64 &gourp_id, uint64 &postion);
		static void get_group_min_max_index(uint64 group_id, uint64 &min_index, uint64 &max_index);
		static uint64 get_group_id(const uint64 &index);

		static uint16 get_sum(char *data, int size);

		static bool socket_bind_udp_port(const short int &port, const char* bind_ip);
		
		static bool set_socket_send_buffer(const harq_fd &fd, const int &size);
		static bool set_socket_recv_buffer(const harq_fd &fd, const int &size);
		static bool set_socket_ttl(const harq_fd &fd);
		static bool set_socket_tos(const harq_fd &fd);
		static bool set_socket_reuseaddr(const harq_fd &fd);
		static bool	set_socket_dontfragment(const harq_fd &fd);
		static bool	set_socket_multicast_loop(const harq_fd &fd);
		
		
		static int64 get_address(struct sockaddr_in addr_ptr);
		static int64 get_address(const std::string &ip, const int &port);
		static std::string get_remote_ip(struct sockaddr_in addr_ptr);
		static int get_remote_port(struct sockaddr_in addr_ptr);
		static int64_t address_to_int64(sockaddr_in *addr_ptr);

		static std::string get_time_string();
		static void log(std::string Format,...);
		static int get_cpu_cnum();

		static std::string get_random_string(const int length);
		static void hexstr_to_byte(const char* source, unsigned char* dest, int sourceLen);

		static std::string get_speed(const uint64 &flow);
		static rudptimer abs_sub(rudptimer first, rudptimer second);

		static int sleep_delay(const int &delay_timer, const timer_mode &cur_timer_mode);

		#if defined(_WIN32)
			static bool set_socket_connreset(const harq_fd &fd);

		#else

			static bool set_socket_enable_blocking(const harq_fd &fd);
			static bool set_socket_multicastloop(const harq_fd &fd);
			static bool set_socket_addmembership(const harq_fd &fd, const std::string &group_ip);
			static bool set_socket_multicastif(const harq_fd &fd, const std::string &eth_name);
		#endif
	};
}


#endif  // RUDP_PUBLIC_H_

