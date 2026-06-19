#ifndef NETWORK_H
#define NETWORK_H

#include "rip-protocol-structures.h"

int create_rip_socket();
void send_rip_packet(int sock, struct rip_packet *packet, int num_entries);

#endif