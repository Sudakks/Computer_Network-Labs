#include "ip.h"
#include "icmp.h"
#include "arpcache.h"
#include "rtable.h"
#include "arp.h"

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// initialize ip header 
void ip_init_hdr(struct iphdr *ip, u32 saddr, u32 daddr, u16 len, u8 proto)
{
	ip->version = 4;
	ip->ihl = 5;
	ip->tos = 0;
	ip->tot_len = htons(len);
	ip->id = rand();
	ip->frag_off = htons(IP_DF);
	ip->ttl = DEFAULT_TTL;
	ip->protocol = proto;
	ip->saddr = htonl(saddr);
	ip->daddr = htonl(daddr);
	ip->checksum = ip_checksum(ip);
}

// lookup in the routing table, to find the entry with the same and longest prefix.
// the input address is in host byte order
rt_entry_t *longest_prefix_match(u32 dst)
{
	//rtable里的内容是以host字节存储的
	//这里不应写成struct
	rt_entry_t* max_entry = NULL;
	/*这里非常注意！！！应该将max_entry初始化为NULL
	* 否则如果没有找到，并且返回max_entry，会返回一个未定义的值导致段错误！
	* 初始化！！！
	*/
	
	int max_len = 0;
	rt_entry_t* rt_entry;
	list_for_each_entry(rt_entry, &rtable, list) 
	{
		u32 rt_dest_masked = rt_entry->dest & rt_entry->mask;
		u32 dst_masked = (rt_entry->mask & dst);
		if(dst_masked == rt_dest_masked)
		{
			int now_len = 0;
			//calculate
			u32 tmp = rt_entry->mask;
			while(tmp)
			{
				now_len += tmp & 1;
				tmp >>= 1;
			}
			if(now_len > max_len)
			{
				max_len = now_len;
				max_entry = rt_entry;
			}
		}
	}
	return max_entry;

	//fprintf(stderr, "TODO: longest prefix match for the packet.\n");
	//return NULL;
}

// send IP packet
//
// Different from forwarding packet, ip_send_packet sends packet generated by
// router itself. This function is used to send ICMP packets.
void ip_send_packet(char *packet, int len)
{
	//fprintf(stderr, "TODO: send ip packet.\n");
	struct iphdr* hdr = packet_to_ip_hdr(packet);
    rt_entry_t * rt_entry = longest_prefix_match(ntohl(hdr->daddr));
	//查看表项中记录的下一跳网关IP
    if(rt_entry == NULL)
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
    else
	{
		if(rt_entry->gw == 0)
			//说明在同一network中
			iface_send_packet_by_arp(rt_entry->iface, ntohl(hdr->daddr), packet, len);
		else
			iface_send_packet_by_arp(rt_entry->iface, rt_entry->gw, packet, len);
	}
}

