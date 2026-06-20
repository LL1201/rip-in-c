#ifndef ROUTING_H
#define ROUTING_H

#include <sys/types.h>
#include <sys/socket.h>
#include "rip-protocol-specs.h"

void send_routes(int sock);
void process_rip_packet(struct rip_packet *pkt, int bytes_received, const char *sender_ip);

#endif