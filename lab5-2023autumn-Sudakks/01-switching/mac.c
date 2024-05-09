#include "mac.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

mac_port_map_t mac_port_map;

// initialize mac_port table
void init_mac_port_table()
{
	bzero(&mac_port_map, sizeof(mac_port_map_t));

	for (int i = 0; i < HASH_8BITS; i++) {
		init_list_head(&mac_port_map.hash_table[i]);
	}

	pthread_mutex_init(&mac_port_map.lock, NULL);

	pthread_create(&mac_port_map.thread, NULL, sweeping_mac_port_thread, NULL);
}

// destroy mac_port table
void destory_mac_port_table()
{
	pthread_mutex_lock(&mac_port_map.lock);
	mac_port_entry_t *entry, *q;
	for (int i = 0; i < HASH_8BITS; i++) {
		list_for_each_entry_safe(entry, q, &mac_port_map.hash_table[i], list) {
			list_delete_entry(&entry->list);
			free(entry);
		}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
}


//通过输⼊⼤⼩为ETH_ALEN的8bit数数组（即48bits，MAC地址的⻓度），通过查找mac_port_map完成MAC地址到接⼝的查询
// lookup the mac address in mac_port table
iface_info_t *lookup_port(u8 mac[ETH_ALEN])
{
	mac_port_entry_t *entry;
	u8 hash_value = hash8((char*)mac, ETH_ALEN);
	//计算得到了这个mac地址的哈希值，此时在map中一一寻找
	pthread_mutex_lock(&mac_port_map.lock);
	list_for_each_entry(entry, &mac_port_map.hash_table[hash_value], list)
	{
		for(int i = 0; i < ETH_ALEN; i++)
		{
			if(entry->mac[i] != mac[i])
				break;
			if(i == ETH_ALEN - 1)
			{
				pthread_mutex_unlock(&mac_port_map.lock);
				return entry->iface;
			}
		}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
	return NULL;//表示没有找到
}

// insert the mac -> iface mapping into mac_port table
//要插⼊⼀个MAC地址到接⼝的映射表项到mac_port_map
void insert_mac_port(u8 mac[ETH_ALEN], iface_info_t *iface)
{
	u8 hash_value = hash8((char*)mac, ETH_ALEN);
	if(lookup_port(mac) != NULL)
	{
		mac_port_entry_t *entry;
		pthread_mutex_lock(&mac_port_map.lock);
		list_for_each_entry(entry, &mac_port_map.hash_table[hash_value], list)
		{
			for(int i = 0; i < ETH_ALEN; i++)
			{
				if(entry->mac[i] != mac[i])
					break;
				if(i == ETH_ALEN - 1)
				{
					entry->visited = time(NULL);	
					entry->iface = iface;
					pthread_mutex_unlock(&mac_port_map.lock);
					return;
				}
			}
		}
	}
	
	mac_port_entry_t *new_entry = (mac_port_entry_t *)malloc(sizeof(mac_port_entry_t));
	assert(new_entry != NULL);
	new_entry->iface = iface;
	new_entry->visited = time(NULL);
	for(int i = 0; i < ETH_ALEN; i++)
		new_entry->mac[i] = mac[i];
	pthread_mutex_lock(&mac_port_map.lock);
	list_add_head(&new_entry->list, &mac_port_map.hash_table[hash_value]);
	pthread_mutex_unlock(&mac_port_map.lock);

	// TODO: implement the insertion process here
}

// dumping mac_port table
void dump_mac_port_table()
{
	mac_port_entry_t *entry = NULL;
	time_t now = time(NULL);

	fprintf(stdout, "dumping the mac_port table:\n");
	pthread_mutex_lock(&mac_port_map.lock);
	for (int i = 0; i < HASH_8BITS; i++) {
		list_for_each_entry(entry, &mac_port_map.hash_table[i], list) {
			fprintf(stdout, ETHER_STRING " -> %s, %d\n", ETHER_FMT(entry->mac), \
					entry->iface->name, (int)(now - entry->visited));
		}
	}

	pthread_mutex_unlock(&mac_port_map.lock);
}

// sweeping mac_port table, remove the entry which has not been visited in the last 30 seconds.
int sweep_aged_mac_port_entry()
{
	time_t now = time(NULL);
	mac_port_entry_t *entry, *q;
	int ret = 0;
	pthread_mutex_lock(&mac_port_map.lock);
	for (int i = 0; i < HASH_8BITS; i++) 
	{
		list_for_each_entry_safe(entry, q, &mac_port_map.hash_table[i], list) {
		{
			if(now - entry->visited >= MAC_PORT_TIMEOUT)
			{
				//执行删除表项的操作
				list_delete_entry(&entry->list);
				free(entry);
				ret++;
			}
		}
	}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
	return ret;

	// TODO: implement the sweeping process here
}

// sweeping mac_port table periodically, by calling sweep_aged_mac_port_entry
void *sweeping_mac_port_thread(void *nil)
{
	while (1) {
		sleep(1);
		int n = sweep_aged_mac_port_entry();

		if (n > 0)
			log(DEBUG, "%d aged entries in mac_port table are removed.", n);
	}

	return NULL;
}
