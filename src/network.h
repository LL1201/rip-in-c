#ifndef NETWORK_H
#define NETWORK_H

#include "rip-protocol-specs.h"
#include <net/if.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_NETWORKS 10

// Structure to hold network CIDR
struct network_config
{
    uint32_t network; // Network address
    uint32_t netmask; // Network mask
    char interface_name[IF_NAMESIZE];
    uint32_t local_ip; // L'IP fisico della scheda
};

// Exposed network configuration
extern struct network_config networks[MAX_NETWORKS];
extern int num_networks;

int create_rip_socket();
int ip_in_network(uint32_t ip, uint32_t network, uint32_t mask);
void send_rip_packet(int sock, struct rip_packet *pkt, int num_entries, struct sockaddr_in *dest, struct in_addr *out_iface_ip);

#endif