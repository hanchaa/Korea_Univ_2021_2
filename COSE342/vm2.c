#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <stdbool.h>
#include <net/ethernet.h>

struct eth_header {
	unsigned char dest[6];
	unsigned char source[6];
	unsigned short eth_type;
};

struct my_ip_header {
	unsigned short dest_len;
	unsigned char * dest;
	unsigned short source_len;
	unsigned char * source;
	unsigned char ttl;
};

struct my_ip_routing_entry {
	unsigned short dest_len;
	unsigned char * dest;
	unsigned char * next_hop;
	unsigned char * output_interface;
	struct my_ip_routing_entry * next;
};

struct my_arp_cache_entry {
	unsigned short dest_len;
	unsigned char * dest;
	unsigned char dest_mac[6];
	struct my_arp_cache_entry * next;
};

struct interface_info {
	unsigned char * name;
	unsigned char mac[6];
	unsigned short my_ip_len;
	unsigned char * my_ip;
	int bound_sock_fd;
	struct my_arp_cache_entry * cache_list;
	struct interface_info * next;
};

struct my_ip_routing_entry * routing_entry_list = NULL;
struct interface_info * interface_list = NULL;

bool is_forward = false;

int makeRawSocket(char * device_name, unsigned short ether_type) {
	struct ifreq if_request;
	struct sockaddr_ll addr;
	int sock_raw = socket(AF_PACKET, SOCK_RAW, htons(ether_type));

	if(sock_raw < 0) {
		return -1;
	}

	if(setsockopt(sock_raw, SOL_SOCKET, SO_BINDTODEVICE, device_name, strlen(device_name)) < 0) {
		printf("Binding Device Error\n");
		close(sock_raw);
		return -1;
	}

	memset(&if_request, 0, sizeof(if_request));

	strncpy(if_request.ifr_name, device_name, strlen(device_name));

	if (ioctl(sock_raw, SIOCGIFINDEX, &if_request) < 0) {
		printf("IOCTL for getting ifindex Error\n");
		close(sock_raw);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sll_ifindex = if_request.ifr_ifindex;
	addr.sll_family = AF_PACKET;

	if(bind(sock_raw, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		printf("Binding Error\n");
		close(sock_raw);
		return -1;
	}

	return sock_raw;
}

int setup_my_interface(char * device_name, char * my_ip) {
	int sock = makeRawSocket(device_name, 0xfffe);
	struct ifreq if_mac;

	if(sock == -1) {
		printf("Interface Setup Error - Creating Socket Failed\n");
		return -1;
	}

	struct interface_info * if_info = (struct interface_info *)malloc(sizeof(struct interface_info));

	memset(&if_mac, 0, sizeof(struct ifreq));
	strncpy(if_mac.ifr_name, device_name, IFNAMSIZ-1);
	if(ioctl(sock, SIOCGIFHWADDR, &if_mac) != 0) {
		printf("Interface Setup Error - Getting MAC Address Failed\n");
		free(if_info);
		close(sock);
		return -1;
	}
	memcpy(if_info->mac, if_mac.ifr_hwaddr.sa_data, 6);

	if_info->name = (unsigned char *)malloc(strlen(device_name) + 1);
	memcpy(if_info->name, device_name, strlen(device_name) + 1);

	if_info->my_ip_len = strlen(my_ip) + 1;
	if_info->my_ip = (unsigned char *)malloc(if_info->my_ip_len);
	memcpy(if_info->my_ip, my_ip, if_info->my_ip_len);
	if_info->bound_sock_fd = sock;
	if_info->cache_list = NULL;
	if_info->next = NULL;

	if(interface_list == NULL) {
		interface_list = if_info;
	} else {
		struct interface_info * ptr = interface_list;
		while(ptr->next != NULL) {
			ptr = ptr->next;
		}

		ptr->next = if_info;
	}

	printf("Interface Setup for %s successful\n", device_name);
	return sock;
}

int add_arp_cache_entry(unsigned char * device_name, unsigned char * dest, unsigned char dest_mac[6]) {
	struct interface_info * list_ptr = interface_list;

	while(list_ptr != NULL) {
		if(strcmp(list_ptr->name, device_name) == 0) {
			struct my_arp_cache_entry * new_entry = (struct my_arp_cache_entry *)malloc(sizeof(struct my_arp_cache_entry));

			new_entry->dest_len = strlen(dest);
			new_entry->dest = (unsigned char*)malloc(new_entry->dest_len + 1);
			memcpy(new_entry->dest, dest, new_entry->dest_len + 1);
			memcpy(new_entry->dest_mac, dest_mac, 6);
			new_entry->next = NULL;

			if(list_ptr->cache_list == NULL) {
				list_ptr->cache_list = new_entry;
			} else {
				struct my_arp_cache_entry * cache_ptr = list_ptr->cache_list;
				while(cache_ptr->next != NULL) {
					cache_ptr = cache_ptr->next;
				}
				cache_ptr->next = new_entry;
			}

			printf("Adding ARP Cache entry to %s successful\n", device_name);
			return 0;
		}
		list_ptr = list_ptr->next;
	}

	printf("Adding ARP Cache entry Error - No interface for %s\n", device_name);
	return -1;
}

int add_routing_entry(unsigned char * dest, unsigned char * next_hop, unsigned char * output_interface) {
	struct my_ip_routing_entry * ptr = routing_entry_list;
	struct my_ip_routing_entry * new_entry = (struct my_ip_routing_entry *)malloc(sizeof(struct my_ip_routing_entry));

	if(dest != NULL) {
		new_entry->dest_len = strlen(dest);
		new_entry->dest = (unsigned char *)malloc(new_entry->dest_len + 1);
		memcpy(new_entry->dest, dest, new_entry->dest_len + 1);
	} else {
		new_entry->dest_len = 0;
		new_entry->dest = NULL;
	}

	if(next_hop != NULL) {
		new_entry->next_hop = (unsigned char *)malloc(strlen(next_hop) + 1);
		memcpy(new_entry->next_hop, next_hop, strlen(next_hop) + 1);
	} else {
		new_entry->next_hop = NULL;
	}

	new_entry->output_interface = (unsigned char *)malloc(strlen(output_interface) + 1);
	memcpy(new_entry->output_interface, output_interface, strlen(output_interface) + 1);

	if(routing_entry_list == NULL) {
		routing_entry_list = new_entry;
	} else {
		while(ptr->next != NULL) {
			ptr = ptr->next;
		}

		ptr->next = new_entry;
	}
 
	return 0;
}

// !!! Explain why this function is needed. //
struct my_ip_routing_entry * route_output(struct my_ip_header my_iph) {
	struct my_ip_routing_entry * ptr = routing_entry_list;

	while(ptr != NULL) {
		if(ptr->dest_len <= my_iph.dest_len) {
			printf("%s %s %d %d\n", ptr->dest, my_iph.dest, ptr->dest_len, my_iph.dest_len);
			if(strncmp(ptr->dest, my_iph.dest, ptr->dest_len) == 0) {
				printf("Routing entry is found (In this assignment, assume that the firstly found routing entry is longest matching)\n");
				return ptr;
			}
		}

		ptr = ptr->next;
	}

	return NULL;
}

int my_ip_output(struct my_ip_header my_iph, struct my_ip_routing_entry * rentry) {
	struct my_arp_cache_entry * cache_ptr;
	struct interface_info * if_ptr = interface_list;
	struct interface_info * output_interface = NULL;

	unsigned char * buffer;
	unsigned char * buffer_data_ptr;
	unsigned char * buffer_ptr;

	unsigned char * next_hop;
	unsigned char * next_hop_mac = NULL;
	unsigned int total_len;
	unsigned int my_ip_header_len;

	while(if_ptr != NULL) {
		if(strcmp(if_ptr->name, rentry->output_interface) == 0) {
			output_interface = if_ptr;
			break;
		}
		if_ptr = if_ptr->next;
	}

	if(output_interface == NULL) {
		printf("Error - No interface\n");
		return -1;
	}

    // If the next hop of found entry is NULL => Direct (next hop is destination IP)
    // Else, use the next hop of found entry
    // !!! Fill the blank (Setup next hop variable)

	if (rentry->next_hop == NULL) {
		next_hop = my_iph.dest;
	}
	else {
		next_hop = rentry->next_hop;
	}

	cache_ptr = output_interface->cache_list;
	while(cache_ptr != NULL) {
        // Retrieve ARP Cache to find the mac address for the next hop
        // !!! Fill the blank (Setup next_hop_mac variable)
		if (strcmp(cache_ptr->dest, next_hop) == 0) {
			next_hop_mac = cache_ptr->dest_mac;
			break;
		}
		
		cache_ptr = cache_ptr->next;
	}

	if(next_hop_mac == NULL) {
		printf("ARP Cache entry is not found, failed to send the packet\n");
		return -1;
	}

	printf("ARP Cache entry is found, send the packet\n");
	my_ip_header_len = sizeof(unsigned short) *2 + my_iph.dest_len + my_iph.source_len + 1;
	total_len = my_ip_header_len + sizeof(struct eth_header);

	buffer = (unsigned char *)malloc(total_len);
	memset(buffer, 0, total_len);

    // Usually, buffer is allocated sufficiently and writting contents to buffer starts at the end of buffer.
	buffer_data_ptr = buffer + total_len; // point buffer end

	buffer_data_ptr -= my_ip_header_len; // pointer pull

	// Adding My IP Header
    // For the example, I leave some of the blank
	buffer_ptr = buffer_data_ptr;

	*((unsigned short *)buffer_ptr) = htons(my_iph.dest_len);
	buffer_ptr += sizeof(unsigned short);

    // !!! Fill the blank (Set dest, source len, source, ttl)
	memcpy(buffer_ptr, my_iph.dest, my_iph.dest_len);
	buffer_ptr += my_iph.dest_len;

	*((unsigned short *)buffer_ptr) = htons(my_iph.source_len);
	buffer_ptr += sizeof(unsigned short);

	memcpy(buffer_ptr, my_iph.source, my_iph.source_len);
	buffer_ptr += my_iph.source_len;

	*buffer_ptr = my_iph.ttl;

	// Adding Ethernet Header
	struct eth_header eh;
	memcpy(eh.dest, next_hop_mac, 6);
	memcpy(eh.source, output_interface->mac, 6);
	eh.eth_type = htons(0xfffe);

	buffer_data_ptr -= sizeof(struct eth_header); // pointer pull

	buffer_ptr = buffer_data_ptr; 
	memcpy(buffer_ptr, (unsigned char *)(&eh), sizeof(eh));
	buffer_ptr += sizeof(eh);

	if(send(output_interface->bound_sock_fd, buffer_data_ptr, total_len, 0) <= 0) {
		printf("Sending Error\n");
	}
	free(buffer);
	return 0;
}

bool local_deliver(unsigned char * dest) {
	struct interface_info * ptr = interface_list;
	int index = 0;
	while(ptr != NULL) {
		if(strcmp(dest, ptr->my_ip) == 0) {
			return true;
		}
		ptr = ptr->next;
	}

	return false;
}

int my_ip_forward(struct my_ip_header my_iph) {
	struct my_ip_routing_entry * rentry = route_output(my_iph); // Finding routing entry

    // TTL Check - If 0, Drop and stop further processing
    // !!! Fill the blank
	my_iph.ttl -= 1;
	if (my_iph.ttl == 0) {
		printf("TTL is 0\n");
		return -1;
	}

	if(rentry == NULL) {
		printf("No Routing Entry\n");
		return -1;
	}

	printf("Do output\n");
	return my_ip_output(my_iph, rentry);
}

int my_ip_send(unsigned char * dest, unsigned char ttl) {
	struct my_ip_header my_iph;
	struct my_ip_routing_entry * rentry;

	my_iph.source_len = 0;
	my_iph.source = NULL;
	my_iph.dest_len = strlen(dest);
	my_iph.dest = (unsigned char *)malloc(my_iph.dest_len + 1);
	memcpy(my_iph.dest, dest, strlen(dest) + 1);

	my_iph.ttl = ttl;

	rentry = route_output(my_iph);
	if(rentry == NULL) {
		printf("Sending Error - No Routing Entry\n");
		return -1;
	}

	struct interface_info * list_ptr = interface_list;

	while(list_ptr != NULL) {
		if(strcmp(list_ptr->name, rentry->output_interface) == 0) {
			my_iph.source_len = strlen(list_ptr->my_ip);
			my_iph.source = (unsigned char *)malloc(my_iph.source_len + 1);
			memcpy(my_iph.source, list_ptr->my_ip, my_iph.source_len + 1);
			break;
		}
		list_ptr = list_ptr->next;
	}

	if(my_iph.source == NULL) {
		printf("Sending Error - Source is NULL?\n");
		return -1;
	}

	return my_ip_output(my_iph, rentry);
}

int my_ip_receive(unsigned char * buf) {
	unsigned char * ptr = buf;
	struct my_ip_header my_iph;

	my_iph.dest_len = htons(*((unsigned short *)ptr));
	ptr += sizeof(unsigned short);

	my_iph.dest = ptr;
	ptr += my_iph.dest_len;

	my_iph.source_len = htons(*((unsigned short *)ptr));
	ptr += sizeof(unsigned short);

	my_iph.source = ptr;
	ptr += my_iph.source_len;

	my_iph.ttl = *ptr;

	if(local_deliver(my_iph.dest)) {
		printf("This packet is sent to me. Do local deliver..\n");
		return 0;
	}

	if(is_forward) {
		printf("This packet is not mine and I'm router. Try to forward this packet..\n");
		if(my_ip_forward(my_iph) == -1) {
			printf("Forwarding Error\n");
			return -1;
		}
	} else {
		printf("This packet is not mine and I'm not router. Drop this packet.\n");
	}

	return 0;
}

void processEthernet(unsigned char * buffer) {
	struct eth_header eh;
	unsigned short ethtype;

	memcpy((unsigned char *)&eh, buffer, sizeof(eh));

    // Get ethertype and dispatch payload part to proper process function
    // Endian translation is needed
    // !!! Fill the blank
	memcpy(eh.dest, buffer, 6);
	buffer += 6;
	
	memcpy(eh.source, buffer, 6);
	buffer +=6;

	eh.eth_type = ethtype = ntohs(*(unsigned short *)buffer);
	buffer += 2;

	struct interface_info * ptr = interface_list;

	if (ethtype == 0xfffe) {
		my_ip_receive(buffer);
	}
}

void environment_setup() {
	unsigned char mac[6];

	sscanf("08:00:27:08:3e:cd", "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
	add_arp_cache_entry("enp0s3", "Dongdaemungu 1", mac);
	sscanf("08:00:27:ab:e0:5c", "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
	add_arp_cache_entry("enp0s8", "Sungbukgu Anamdong 2", mac);
	add_routing_entry("Dongdaemungu", NULL, "enp0s3");
	add_routing_entry("Sungbukgu Anamdong", NULL, "enp0s8");
	add_routing_entry("Sungbukgu Bomundong", NULL, "enp0s9");
}

int main() {
	unsigned char buffer[1500];
	unsigned int received = 0;
	//int sock_enp0s3 = setup_my_interface("enp0s3", "Dongdaemungu 1"); // VM1
	int sock_enp0s3 = setup_my_interface("enp0s3", "Dongdaemungu 2"); // VM2
	int sock_enp0s8 = setup_my_interface("enp0s8", "Sungbukgu Anamdong 1"); // VM2
	int sock_enp0s9 = setup_my_interface("enp0s9", "Sungbukgu Bomundong 1"); // VM2
	//int sock_enp0s3 = setup_my_interface("enp0s3", "Sungbukgu Anamdong 2"); // VM3
	environment_setup();

	is_forward = true;

	received = recv(sock_enp0s3, buffer, 1500, 0);
	printf("Frame received\n");
	processEthernet(buffer);

	return 0;
}
