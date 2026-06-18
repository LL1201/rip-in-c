#ifndef NETWORK_H
#define NETWORK_H

int create_rip_socket();
void send_rip_packet(int sock, struct rip_packet *packet, int num_entries);

#endif