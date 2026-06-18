#ifndef ROUTING_H
#define ROUTING_H

#include <sys/types.h>
#include <sys/socket.h>
#include "rip-protocol-structures.h"

void send_my_routes(int sock);
void process_rip_packet(struct rip_packet *pkt, const char *sender_ip);

#endif