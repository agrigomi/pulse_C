#ifndef __I_NET_H__
#define __I_NET_H__

#include "iIO.h"

#define I_SOCKET_IO	"iSocketIO"
#define I_NET		"iNet"
#define I_TCP_SERVER	"iTCPServer"

class iSocketIO: public iIO {
public:
	INTERFACE(iSocketIO, I_SOCKET_IO);
	virtual void blocking(bool)=0; /* blocking or nonblocking IO */
};

class iTCPServer: public iBase {
public:
	INTERFACE(iTCPServer, I_TCP_SERVER);
	virtual iSocketIO *listen(void)=0;
	virtual void blocking(bool)=0; /* blocking or nonblocking IO */
	virtual void close(iSocketIO *p_io)=0;
};

class iNet: public iBase {
public:
	INTERFACE(iNet, I_NET);
	virtual iSocketIO *create_udp_server(_u32 port)=0;
	virtual iSocketIO *create_udp_client(_str_t dst_ip, _u32 port)=0;
	virtual void close_socket(iSocketIO *p_sio)=0;
	virtual iTCPServer *create_tcp_server(_u32 port)=0;
	virtual iSocketIO *create_tcp_client(_str_t host, _u32 port)=0;
};

#endif
