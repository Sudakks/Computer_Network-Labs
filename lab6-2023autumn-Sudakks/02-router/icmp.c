#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// send icmp packet
// icmp包由：IP包首部+ICMP包首部+ICMP数据包组成
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	//不太清楚要不要自己加IP包首部
	struct ether_header* old_ehdr = (struct ether_header*)in_pkt;
	struct iphdr* old_iphdr = packet_to_ip_hdr(in_pkt);
	char* packet;
	int pkt_len = 0;
	//会根据这个pkt的长度改变
	if(type == ICMP_ECHOREPLY)
		pkt_len = (IP_BASE_HDR_SIZE + len - IP_HDR_SIZE(old_iphdr)); 
	else
		pkt_len = (IP_HDR_SIZE(old_iphdr) + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ICMP_HDR_SIZE + ICMP_COPIED_DATA_LEN);//不理解为什么这里要加上IP_HDR_SIZE
	packet = (char*)malloc(pkt_len);
	//开始设定pkt内容
	//设定ether头
	struct ether_header* new_ehdr = (struct ether_header*)packet;
	new_ehdr->ether_type = htons(ETH_P_IP);
	memcpy(new_ehdr->ether_dhost, old_ehdr->ether_dhost, ETH_ALEN);
	memcpy(new_ehdr->ether_shost, old_ehdr->ether_shost, ETH_ALEN);


	//设定ip头
	////ICMP packet's IP head's size will always be IP_BASE_HDR_SIZE
	struct iphdr* new_iphdr = packet_to_ip_hdr(packet);
	rt_entry_t *rt_entry = longest_prefix_match(ntohl(old_iphdr->saddr));
	ip_init_hdr(new_iphdr, rt_entry->iface->ip, ntohl(old_iphdr->saddr), pkt_len - ETHER_HDR_SIZE, IPPROTO_ICMP);
	//这个icmp信息是被这个router的iface端口发送的


	//设定icmp部分
	//struct icmphdr* new_icmphdr = (struct icmphdr*)(new_iphdr + IP_BASE_HDR_SIZE);
	struct icmphdr* new_icmphdr = (struct icmphdr*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
	new_icmphdr->type = type;
	new_icmphdr->code = code;
	int rest = IP_HDR_SIZE(new_iphdr) + ETHER_HDR_SIZE + 4;//已经略过了icmp头中code+type+checksum
	//前面这三个变量加一起的大小是4,那么只需要加4即可，否则可能会导致段错误
	if(type == ICMP_ECHOREPLY)
	{
		//回应ping
		memcpy(packet + rest, in_pkt + rest, pkt_len - rest);
	}
	else
	{
		memset(packet + rest, 0, 4);
		memcpy(packet + rest + 4, in_pkt + ETHER_HDR_SIZE, IP_HDR_SIZE(old_iphdr) + 8);
	}
	new_icmphdr->checksum = icmp_checksum(new_icmphdr, pkt_len - ETHER_HDR_SIZE - IP_HDR_SIZE(new_iphdr));
	ip_send_packet(packet, pkt_len);

	//fprintf(stderr, "TODO: malloc and send icmp packet.\n");
}

