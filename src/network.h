#ifndef NETWORK_H
#define NETWORK_H

#include "rip-protocol-specs.h"
#include <net/if.h>

#define MAX_NETWORKS 10

// Structure to hold network CIDR
struct network_config
{
    uint32_t network; // Network address
    uint32_t netmask; // Network mask
    char interface_name[IF_NAMESIZE];
};

// Exposed network configuration
extern struct network_config networks[MAX_NETWORKS];
extern int num_networks;

int create_rip_socket();
void send_rip_packet(int sock, struct rip_packet *packet, int num_entries);

#endif