#include "ip.h"
#include "icmp.h"
#include "rtable.h"


#include <stdio.h>
#include <stdlib.h>

void ip_forward_packet(char* packet, int len)
{
	//使用最长前缀匹配找目的IP
	struct iphdr* hdr = packet_to_ip_hdr(packet);
	rt_entry_t * rt_entry = longest_prefix_match(ntohl(hdr->daddr));
	//如果查找到相应条目，则将数据包从该条目对应端口转出，否则回复目的网络不可达(ICMP Dest Network Unreachable)
	if(rt_entry == NULL)
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);//匹配失败
	else
	{
		hdr->ttl--;
		if(hdr->ttl == 0)
			icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
		else
		{
			//此时需要重新计算校验和，写入首部（因为ttl改变了）
			hdr->checksum = ip_checksum(hdr);
			ip_send_packet(packet, len);
		}
	}
}


// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr* ip = packet_to_ip_hdr(packet);
	//packet由ip4头部+ICMP头部+data组成
	//ICMP头部又分成类型（1byte）+代码（1byte）+校验和（2bytes）组成
	char* type = ((char*)ip + IP_HDR_SIZE(ip));
	/*最开始的if条件写错了，是要求两个都要满足才会进这个分支，但是有可能type不满足而daddr对应，此时也不能进到下一个分支里面去
	 * 即不能转发给其他分支，不然肯定找不到dest的
	 * 我感觉是这个原因所以才导致一直host unreachable的
	 *证实了，是这个原因哎哎哎
	 * */
	if(*type == ICMP_ECHOREQUEST && ntohl(ip->daddr) == iface->ip)
	{
		//是发送给该路由器的包，即发送ICMP echo reply，响应
		icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
	}
	else if(ntohl(ip->daddr) != iface->ip)
		//是其他IP包，转发forward pacekt
		ip_forward_packet(packet, len);
	//fprintf(stderr, "TODO: handle ip packet.\n");
}

