#pragma once

#ifndef RUDP_LINUX_H_
#define RUDP_LINUX_H_

#if defined(_WIN32)

	#include <Winsock2.h>
#else

	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <unistd.h>
#endif

namespace ustd
{
	class rudp_linux
	{
	public:
		rudp_linux(void);
		~rudp_linux(void);

	public:
		static bool socket_bind_port(const int &port, const char* bind_ip);
		
		static bool set_fd_attribute(const harq_fd &fd, const int &send_buffer, const int &recv_buffer);
		static int rand_local_port(const std::string &bind_ip, const int &begin_port = 10000, const int &end_port = 60000);

		#if defined(_WIN32)

		#else
			static bool set_socket_reuseport(const harq_fd &fd);
		#endif
	};
}


#endif  // RUDP_LINUX_H_

