#include "arp.h"
#include "base.h"
#include "types.h"
#include "ether.h"
#include "arpcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "log.h"

// send an arp request: encapsulate an arp request packet, send it out through
// iface_send_packet
void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
	/*ARP报文不是直接在网络层上发送的，它还是需要向下传输到数据链路层
	 * 所以当ARP报文传输到数据链路层之后，需要再次进行封装
	 * 以以太网为例，ARP报文传输到以太网数据链路层后会形成ARP帧，就是在ARP报文前面加了一个以太网帧头*/
	char* packet = (char*)malloc(ETHER_HDR_SIZE + sizeof(struct ether_arp));
	/*妈呀一开始mummap_chunk竟然是这里写错了，没有malloc，呃呃呃*/
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp *arp = (struct ether_arp *)(packet + ETHER_HDR_SIZE);

	//设置以太头
	memset(eh->ether_dhost, 0xff, ETH_ALEN);
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);

	//设置arp头
	arp->arp_hrd = htons(ARPHRD_ETHER);
	arp->arp_pro = htons(0x0800);
	arp->arp_hln = ETH_ALEN;//物理地址 
	arp->arp_pln = 4;
	arp->arp_op = htons(ARPOP_REQUEST);
	memcpy(arp->arp_sha, iface->mac, ETH_ALEN);
	arp->arp_spa = htonl(iface->ip);
	memset(arp->arp_tha, 0x0, ETH_ALEN);
	arp->arp_tpa = htonl(dst_ip);

	//准备发送
	iface_send_packet(iface, packet, ETHER_HDR_SIZE + sizeof(struct ether_arp));
	//fprintf(stderr, "TODO: send arp request when lookup failed in arpcache.\n");
}

// send an arp reply packet: encapsulate an arp reply packet, send it out
// through iface_send_packet
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
	char* packet = malloc(ETHER_HDR_SIZE + sizeof(struct ether_arp));
	struct ether_header *eh = (struct ether_header *)packet;
	struct ether_arp *arp = (struct ether_arp *)(packet + ETHER_HDR_SIZE);

	//设置以太头
	memcpy(eh->ether_dhost, req_hdr->arp_sha, ETH_ALEN);
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_ARP);

	//设置arp头
	memcpy(arp, req_hdr, sizeof(struct ether_arp));
	arp->arp_hrd = htons(ARPHRD_ETHER);
	arp->arp_pro = htons(0x0800);
	//好崩溃，原来是这里写错了，写成了htonl……
	arp->arp_hln = ETH_ALEN;//物理地址 
	arp->arp_pln = 4;
	arp->arp_op = htons(ARPOP_REPLY);
	memcpy(arp->arp_sha, iface->mac, ETH_ALEN);
	arp->arp_spa = htonl(iface->ip);
	memcpy(arp->arp_tha, req_hdr->arp_sha, ETH_ALEN);
	arp->arp_tpa = req_hdr->arp_spa;

	//准备发送
	iface_send_packet(iface, packet, ETHER_HDR_SIZE + sizeof(struct ether_arp));


	//fprintf(stderr, "TODO: send arp reply when receiving arp request.\n");
}

//收到的包是ARP，解析ARP首部，如果目的IP不是端口IP,说明源主机要交互的对象不是该端口，将该包丢弃
void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	struct ether_arp *arp = (struct ether_arp *)(packet + ETHER_HDR_SIZE);
	if(ntohl(arp->arp_tpa) != iface->ip)
	{
		free(packet);
		log(DEBUG, "找错了");
		return;//找错地了
	}
	if(ntohs(arp->arp_op) == ARPOP_REQUEST)
	{
		log(DEBUG, "request");
		arp_send_reply(iface, arp);
	}
	else if(ntohs(arp->arp_op) == ARPOP_REPLY)
	{
		//处理回复报文
		log(DEBUG, "reply");
		arpcache_insert(ntohl(arp->arp_spa), arp->arp_sha);
		//iface_send_packet_by_arp(iface, arp->arp_spa, packet, len);
	}
	else
		assert(0);
	//fprintf(stderr, "TODO: process arp packet: arp request & arp reply.\n");
}

// send (IP) packet through arpcache lookup 
//
// Lookup the mac address of dst_ip in arpcache. If it is found, fill the
// ethernet header and emit the packet by iface_send_packet, otherwise, pending 
// this packet into arpcache, and send arp request.
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
	//这个函数是他自己的
	struct ether_header *eh = (struct ether_header *)packet;
	memcpy(eh->ether_shost, iface->mac, ETH_ALEN);
	eh->ether_type = htons(ETH_P_IP);

	u8 dst_mac[ETH_ALEN];
	//即并未初始化dst_mac
	int found = arpcache_lookup(dst_ip, dst_mac);
	if (found) {
		log(DEBUG, "found the mac of %x, send this packet", dst_ip);
		memcpy(eh->ether_dhost, dst_mac, ETH_ALEN);
		iface_send_packet(iface, packet, len);
		//找到映射，转发
	}
	else {
		log(DEBUG, "lookup %x failed, pend this packet", dst_ip);
		//没有找到映射，该数据包缓存在arpcache->req_list中，并发送ARP请求
		arpcache_append_packet(iface, dst_ip, packet, len);
	}
}

