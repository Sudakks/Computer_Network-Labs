#include "arpcache.h"
#include "arp.h"
#include "ether.h"
#include "icmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include "log.h"

static arpcache_t arpcache;

// initialize IP->mac mapping, request list, lock and sweeping thread
void arpcache_init()
{
	bzero(&arpcache, sizeof(arpcache_t));

	init_list_head(&(arpcache.req_list));

	pthread_mutex_init(&arpcache.lock, NULL);

	pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
			list_delete_entry(&(pkt_entry->list));
			free(pkt_entry->packet);
			free(pkt_entry);
		}

		list_delete_entry(&(req_entry->list));
		free(req_entry);
	}

	pthread_kill(arpcache.thread, SIGTERM);

	pthread_mutex_unlock(&arpcache.lock);
}

// lookup the IP->mac mapping
//
// traverse the table to find whether there is an entry with the same IP
// and mac address with the given arguments
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
	//填充数据包的目的MAC地址，并转发该数据包
	pthread_mutex_lock(&arpcache.lock);
	for(int i = 0; i < MAX_ARP_SIZE; i++)
	{
		if(arpcache.entries[i].valid && arpcache.entries[i].ip4 == ip4)
		{
			if(mac == NULL || memcmp(arpcache.entries[i].mac, mac, ETH_ALEN) == 0)
			{
				memcpy(mac, arpcache.entries[i].mac, ETH_ALEN);
				pthread_mutex_unlock(&arpcache.lock);
				//说明找到了
				return 1;
			}
		}	
	}
	pthread_mutex_unlock(&arpcache.lock);
	return 0;
	//fprintf(stderr, "TODO: lookup ip address in arp cache.\n");
}

// append the packet to arpcache
//
// Lookup in the list which stores pending packets, if there is already an
// entry with the same IP address and iface (which means the corresponding arp
// request has been sent out), just append this packet at the tail of that entry
// (the entry may contain more than one packet); otherwise, malloc a new entry
// with the given IP address and iface, append the packet, and send arp request.
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	pthread_mutex_lock(&arpcache.lock);
	struct arp_req *req_entry = NULL;
	list_for_each_entry(req_entry, &arpcache.req_list, list) {
		if(req_entry->iface == iface && req_entry->ip4 == ip4)
		{
			//already an entry with the same IP address and iface, corresponding arp request has been sent out
			struct cached_pkt *pkt = malloc(sizeof(struct cached_pkt));
			if(!pkt)
			{
				pthread_mutex_unlock(&arpcache.lock);
				assert("pkt malloc failed\n");
				return;
			}
			pkt->packet = packet;
			pkt->len = len;
			list_add_tail(&pkt->list, &req_entry->cached_packets);
			pthread_mutex_unlock(&arpcache.lock);
			return;
		}
	}
	req_entry = malloc(sizeof(struct arp_req));
	req_entry->iface = iface;
	req_entry->ip4 = ip4;
	req_entry->sent = time(NULL);
	req_entry->retries = 1;//表示 ARP 请求的尝试次数，所以在第一次发送时为1
						   //当 ARP 请求超时时，retries 会增加 1
						   //如果 retries 达到最大值，则 ARP 请求将被丢弃，防止发送过多ARP
	init_list_head(&req_entry->cached_packets);
	struct cached_pkt *pkt = malloc(sizeof(struct cached_pkt));
	if(!pkt)
	{
		free(req_entry);
		pthread_mutex_unlock(&arpcache.lock);
		assert("pkt malloc failed\n");
		return;
	}
	pkt->packet = packet;
	pkt->len = len;
	list_add_tail(&pkt->list, &(req_entry->cached_packets));
	list_add_tail(&req_entry->list, &(arpcache.req_list));
	arp_send_request(iface, ip4);
	pthread_mutex_unlock(&arpcache.lock);

	//fprintf(stderr, "TODO: append the ip address if lookup failed, and send arp request if necessary.\n");
}

// insert the IP->mac mapping into arpcache, if there are pending packets
// waiting for this mapping, fill the ethernet header for each of them, and send
// them out
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	//收到新的IP->MAC映射,将该映射写入arp缓存中 ，如果缓存已满（最多32条），则随机替换掉其中一个
	pthread_mutex_lock(&arpcache.lock);
	int i;
	for(i = 0; i < MAX_ARP_SIZE; i++)
		if(arpcache.entries[i].valid == 0)
			break;
	if(i == MAX_ARP_SIZE)
		i = 0;
	arpcache.entries[i].ip4 = ip4;
	memcpy(arpcache.entries[i].mac, mac, ETH_ALEN);
	arpcache.entries[i].added = time(NULL);
	arpcache.entries[i].valid = 1;


	//检查是否有pkt在等待这个mapping
	//将在缓存中等待该映射的数据包，依次填写目的MAC地址，转发出去，并删除掉相应缓存数据包
	struct arp_req *arp_req, *arp_q;
	struct cached_pkt *pkt_entry, *pkt_q;
	list_for_each_entry_safe(arp_req, arp_q, &arpcache.req_list, list) 
	{
		if(arp_req->ip4 == ip4)
		{
			//将pkt全部发出去
			list_for_each_entry_safe(pkt_entry, pkt_q, &(arp_req->cached_packets), list)
			{
				struct ether_header *eh = (struct ether_header *)pkt_entry->packet;
				memcpy(eh->ether_dhost, mac, ETH_ALEN);
		        memcpy(eh->ether_shost, arp_req->iface->mac, ETH_ALEN);
				eh->ether_type = htons(ETH_P_IP);
				//注意这里的type不同
				iface_send_packet(arp_req->iface, pkt_entry->packet, pkt_entry->len);

				list_delete_entry(&(pkt_entry->list));
				free(pkt_entry);
			}
			//删除请求的这个表项（因为已经回应了这个request msg）
			list_delete_entry(&arp_req->list);
			free(arp_req);
		}
	}
	pthread_mutex_unlock(&arpcache.lock);

	//fprintf(stderr, "TODO: insert ip->mac entry, and send all the pending packets.\n");
}

// sweep arpcache periodically
//
// For the IP->mac entry, if the entry has been in the table for more than 15
// seconds, remove it from the table.
// For the pending packets, if the arp request is sent out 1 second ago, while 
// the reply has not been received, retransmit the arp request. If the arp
// request has been sent 5 times without receiving arp reply, for each
// pending packet, send icmp packet (DEST_HOST_UNREACHABLE), and drop these
// packets.
void *arpcache_sweep(void *arg) 
{
	while (1) {
		sleep(1);
		time_t now = time(NULL);
		pthread_mutex_lock(&arpcache.lock);
		for(int i = 0; i < MAX_ARP_SIZE; i++)
		{
			if(arpcache.entries[i].valid && (now - arpcache.entries[i].added > 15))
				arpcache.entries[i].valid = 0;
		}
		//如果arp request 超过1s，且没有收到回复，那么重发 TODO
		struct arp_req* req_entry, *req_q;
		list_for_each_entry_safe(req_entry, req_q, &arpcache.req_list, list) 
		{
			/*刚开始一直无法输出ICMP_HOST_UNREACH的信息，逐步试了一下才发现是这里每次先
			 * 判断的是不是发送超过1s，如果是的话重发，那么一直不能判断retries次数
			 * 故发不出去这类icmp信息
			 * 所以应该先判定重传才看能不能重发（因为重传超过太多次数之后就不能重发了）
			 * 并且每次test的时候要等待一会，因为要重传嘛，需要一定判断时间
			 */
			if(req_entry->retries > 5)
			{
				struct cached_pkt *pkt_entry = NULL, *pkt_q;
				list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) 
				{
					pthread_mutex_unlock(&arpcache.lock);
					icmp_send_packet(pkt_entry->packet, pkt_entry->len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
					pthread_mutex_lock(&arpcache.lock);
					list_delete_entry(&(pkt_entry->list));
					free(pkt_entry);
					//drop这个arp_request上面的packet
				}
				//删除这个arp_request
				list_delete_entry(&(req_entry->list));
				free(req_entry);
				continue;
			}
			if(now - req_entry->sent > 1)
			{
				arp_send_request(req_entry->iface, req_entry->ip4);
				req_entry->sent = time(NULL);
				req_entry->retries++;
			}

		//fprintf(stderr, "TODO: sweep arpcache periodically: remove old entries, resend arp requests .\n");
		}
		pthread_mutex_unlock(&arpcache.lock);
	}
	return NULL;
}

