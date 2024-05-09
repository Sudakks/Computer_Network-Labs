#ifndef __BASE_H__
#define __BASE_H__

#include "types.h"
#include "ether.h"
#include "list.h"

#include <arpa/inet.h>

typedef struct {
	struct list_head iface_list;
	int nifs;
	struct pollfd *fds;
} ustack_t;

extern ustack_t *instance;

//表示网络接口的信息
typedef struct {
	struct list_head list;//用于连接网络接口信息的链表节点

	int fd;//网络接口对应的文件描述符
	int index;//网络接口的索引（用于标识某个特定的网络接口）
	u8	mac[ETH_ALEN];//存储网络接口的 MAC 地址，MAC地址是一个标识网络设备的唯一地址
	char name[16];//存储网络接口的名称
} iface_info_t;

void init_ustack();
iface_info_t *fd_to_iface(int fd);
void iface_send_packet(iface_info_t *iface, const char *packet, int len);

void broadcast_packet(iface_info_t *iface, const char *packet, int len);

#endif
