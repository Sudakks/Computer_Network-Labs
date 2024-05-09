#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

static struct nat_table nat;

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name) {
    iface_info_t *iface = NULL;
    list_for_each_entry(iface, &instance->iface_list, list) {
        if (strcmp(iface->name, if_name) == 0)
            return iface;
    }

    log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
    return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	//查询路由表，根据地址相应转发条目对应的iface判断地址类别
	//若源地址是internal的，那么是向外发的
	struct iphdr* ip = packet_to_ip_hdr(packet);
	u32 saddr = ntohl(ip->saddr);
	rt_entry_t* rt_entry = longest_prefix_match(saddr);
	if(rt_entry->iface->index == nat.external_iface->index)
		return DIR_IN;
	else if(rt_entry->iface->index == nat.internal_iface->index)
		return DIR_OUT;
	else
		return DIR_INVALID;
	//fprintf(stdout, "TODO: determine the direction of this packet.\n");
}


// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
u8 my_hash(int dir, struct iphdr* ip, struct tcphdr* tcp)
{
	u32 remote_addr;
	u16 remote_port;
	if(dir == DIR_IN)
	{
		remote_addr = ntohl(ip->daddr);
		remote_port = ntohs(tcp->dport);
	}
	else
	{
		remote_addr = ntohl(ip->saddr);
		remote_port = ntohs(tcp->sport);
	}
	char buf[6];
	memset(buf, 0, 6);
	memcpy(buf, (char*)&remote_addr, 4);
	memcpy(buf + 4, (char*)&remote_port, 2);
	return hash8(buf, 6);
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	pthread_mutex_lock(&nat.lock);
	struct iphdr* ip = packet_to_ip_hdr(packet);
	struct tcphdr* tcp = packet_to_tcp_hdr(packet);
	u32 hash_value = my_hash(dir, ip, tcp);
	struct list_head* mapping_list = &nat.nat_mapping_list[hash_value];//然后在这个list上找
	struct nat_mapping* mapping_entry;
	if(dir == DIR_IN)
	{
		//一定有映射
		list_for_each_entry(mapping_entry, mapping_list, list)
		{
			if(ip->daddr == htonl(mapping_entry->external_ip) && tcp->dport == htons(mapping_entry->external_port))
			{
				break;//find mapping
			}
		}
		ip->daddr = htonl(mapping_entry->internal_ip);
		tcp->dport = htons(mapping_entry->internal_port);
		struct nat_connection* conn = &mapping_entry->conn;
		conn->external_fin = (tcp->flags == TCP_FIN);
		conn->external_seq_end = ntohl(tcp_seq_end(ip, tcp));
		if(tcp->flags == TCP_ACK)
			conn->external_ack = tcp->ack;
	}
	else if(dir == DIR_OUT)
	{
		int find = 0;//如果没有找到要自己写个映射
		list_for_each_entry(mapping_entry, mapping_list, list)
		{
			if(ip->daddr == htonl(mapping_entry->internal_ip) && tcp->dport == htons(mapping_entry->internal_port))
			{
				find = 1;
				break;//find mapping
			}
		}
		if(!find)
		{
			//set mapping
			mapping_entry = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
			int available;
			for(available = NAT_PORT_MIN; available < NAT_PORT_MAX; available++)
			{
				if(nat.assigned_ports[available] == 0)
				{
					nat.assigned_ports[available] = 1;
					break;
				}
			}
			mapping_entry->internal_ip = ntohl(ip->saddr);
			mapping_entry->internal_port = ntohs(tcp->sport);
			mapping_entry->external_ip = nat.external_iface->ip;
			mapping_entry->external_port = available;
		}
		//更改ip和tcp
		ip->saddr = htonl(mapping_entry->external_ip);
		tcp->sport = htons(mapping_entry->external_port);
		struct nat_connection* conn = &mapping_entry->conn;
		conn->internal_fin = (tcp->flags == TCP_FIN);
		conn->internal_seq_end = ntohl(tcp_seq_end(ip, tcp));
		if(tcp->flags == TCP_ACK)
			conn->internal_ack = tcp->ack;
	}
	else
		assert(0);
	//checksum和time更新
	mapping_entry->update_time = time(NULL);
	ip->checksum = htons(ip_checksum(ip));
	tcp->checksum = htons(tcp_checksum(ip, tcp));
	pthread_mutex_unlock(&nat.lock);
	ip_send_packet(packet, len);
	//fprintf(stdout, "TODO: do translation for this packet.\n");
}



void nat_translate_packet(iface_info_t *iface, char *packet, int len) {
    int dir = get_packet_direction(packet);
    if (dir == DIR_INVALID) {
        log(ERROR, "invalid packet direction, drop it.");
        icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
        free(packet);
        return;
    }

    struct iphdr *ip = packet_to_ip_hdr(packet);
    if (ip->protocol != IPPROTO_TCP) {
        log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
        free(packet);
        return;
    }

    do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn) {
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}



// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		time_t now = time(NULL);
		pthread_mutex_lock(&nat.lock);
		for(int i = 0; i < HASH_8BITS; i++)
		{
			struct list_head* head = &nat.nat_mapping_list[i];
			struct nat_mapping* mapping_entry, *q_entry;
			list_for_each_entry_safe(mapping_entry, q_entry, head, list)
			{
				if(now - mapping_entry->update_time > TCP_ESTABLISHED_TIMEOUT || is_flow_finished(&mapping_entry->conn))
				{
					nat.assigned_ports[mapping_entry->external_port] = 0;
					list_delete_entry(&mapping_entry->list);
					free(mapping_entry);
				}
			}
		}
		//fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		pthread_mutex_unlock(&nat.lock);
		sleep(1);
	}
	return NULL;
}


int parse_config(const char *filename) {
    // TODO: parse config file, including i-iface, e-iface (and dnat-rules if existing).
    char line[256];
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Open fail errno = %d. reason = %s \n", errno, strerror(errno));
        char buf[1024];
        printf("Working path : %s\n", getcwd(buf, 1024));
    }
    char type[128], name[128], exter[64], inter[64];
    while (!feof(fp) && !ferror(fp)) {
        strcpy(line, "\n");
        fgets(line, sizeof(line), fp);
        if (line[0] == '\n') break;
        sscanf(line, "%s %s", type, name);
        type[14] = '\0';
        if (strcmp(type, "internal-iface") == 0) {
            printf("[Internal] Loading iface item : %s .\n", name);
            nat.internal_iface = if_name_to_iface(name);
        } else if (strcmp(type, "external-iface") == 0) {
            printf("[External] Loading iface item : %s .\n", name);
            nat.external_iface = if_name_to_iface(name);
        } else printf("[Unknown] Loading failed : %s .\n", type);
    }
    u32 ip4, ip3, ip2, ip1, ip;
    u16 port;
    while (!feof(fp) && !ferror(fp)) {
        strcpy(line, "\n");
        fgets(line, sizeof(line), fp);
        if (line[0] == '\n') break;
        sscanf(line, "%s %s %s %s", type, exter, name, inter);
        type[10] = '\0';
        if (strcmp(type, "dnat-rules") == 0) {
            printf("[Dnat] Loading rule item : %s to %s.\n", exter, inter);
            struct dnat_rule *rule = (struct dnat_rule*)malloc(sizeof(struct dnat_rule));
            list_add_tail(&rule->list, &nat.rules);

            sscanf(exter, "%[^:]:%hu", name, &port);
            sscanf(name, "%u.%u.%u.%u", &ip4, &ip3, &ip2, &ip1);
            ip = (ip4 << 24) | (ip3 << 16) | (ip2 << 8) | (ip1);
            rule->external_ip = ip;
            rule->external_port = port;
            printf("   |---[External] ip : %08x ; port : %hu\n", ip, port);

            sscanf(inter, "%[^:]:%hu", name, &port);
            sscanf(name, "%u.%u.%u.%u", &ip4, &ip3, &ip2, &ip1);
            ip = (ip4 << 24) | (ip3 << 16) | (ip2 << 8) | (ip1);
            rule->internal_ip = ip;
            rule->internal_port = port;
            printf("   |---[Internal] ip : %08x ; port : %hu\n", ip, port);
        }
        else printf("[Unknown] Loading failed : %s .\n", type);
    }
    return 0;
}

// initialize
void nat_init(const char *config_file) {
    memset(&nat, 0, sizeof(nat));

    for (int i = 0; i < HASH_8BITS; i++)
        init_list_head(&nat.nat_mapping_list[i]);

    init_list_head(&nat.rules);

    // seems unnecessary
    memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

    parse_config(config_file);

    pthread_mutex_init(&nat.lock, NULL);

    pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	pthread_mutex_lock(&nat.lock);
	for(int i = 0; i < HASH_8BITS; i++)
	{
		struct list_head* head = &nat.nat_mapping_list[i];
		struct nat_mapping* mapping_entry, *q_entry;
		list_for_each_entry_safe(mapping_entry, q_entry, head, list)
		{
			list_delete_entry(&mapping_entry->list);
			free(mapping_entry);
		}
	}
	pthread_mutex_unlock(&nat.lock);
	//fprintf(stdout, "TODO: release all resources allocated.\n");
}

