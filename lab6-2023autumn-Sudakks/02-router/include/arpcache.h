#ifndef __ARPCACHE_H__
#define __ARPCACHE_H__

#include "base.h"
#include "types.h"
#include "list.h"

#include <pthread.h>

#define MAX_ARP_SIZE 32
#define ARP_ENTRY_TIMEOUT 15
#define ARP_REQUEST_MAX_RETRIES	5

/*存储缓存的数据包*/
struct cached_pkt {
	struct list_head list;
	char *packet;
	int len;
};

struct arp_req {
	/*这是一个请求msg的缓存，可能这同一个端口要发送很多信息，所以用list将缓存信息记录下来，串成链表*/
	struct list_head list;
	iface_info_t *iface;
	u32 ip4;
	time_t sent;
	int retries;
	struct list_head cached_packets;//包含指向缓存数据的链表头
	/*如果设备需要发送一个数据包，但是在ARP缓存表中没有找到目标设备的MAC地址，那么设备将发送一个ARP请求。如果设备在发送ARP请求之前已经缓存了数据包，则可以将该数据包附加到ARP请求中，以便在接收到ARP响应后立即发送数据包*/
};

/*表示ARP缓存中的一项*/
struct arp_cache_entry {
	u32 ip4; 	// stored in host byte order
	u8 mac[ETH_ALEN];
	time_t added;/*the time the entry was added to the cache*/
	int valid;
};

typedef struct {
	struct arp_cache_entry entries[MAX_ARP_SIZE];
	struct list_head req_list;/* a list of pending ARP requests*/
	//When an ARP request is made, the kernel adds it to the req_list and starts a timer
	//If the kernel receives an ARP response before the timer expires, it updates the cache with the response and removes the request from the list
	pthread_mutex_t lock;
	pthread_t thread;
} arpcache_t;

void arpcache_init();
void arpcache_destroy();
void *arpcache_sweep(void *);

int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN]);
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN]);
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len);
/*初始化ARP缓存，销毁ARP缓存，扫描ARP缓存，查找ARP缓存，插入ARP缓存和追加数据包到ARP缓存中*/

#endif
