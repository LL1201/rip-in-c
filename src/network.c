#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "network.h"
#include "rip-protocol-structures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int create_rip_socket()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    // Reuse address/port
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Bind to RIP port
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(RIP_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("Bind failed");
        exit(1);
    }

    // Join RIPv2 Multicast group (224.0.0.9)
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(RIP_MULTICAST_ADDR);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("Multicast join failed");
    }

    return sock;
}

void send_rip_packet(int sock, struct rip_packet *packet, int num_entries)
{
    if (num_entries > 25)
    {
        fprintf(stderr, "Error: too many entries (%d > 25)\n", num_entries);
        return;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(RIP_PORT);
    dest_addr.sin_addr.s_addr = inet_addr(RIP_MULTICAST_ADDR);

    // Send RIP packet to Multicast group
    ssize_t sent = sendto(sock, packet, sizeof(struct rip_packet), 0,
                          (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent < 0)
    {
        perror("Error sending RIP packet");
    }
}