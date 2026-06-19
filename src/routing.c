#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "routing.h"
#include "network.h"

// Function called by timer in main
void send_my_routes(int sock)
{
    struct rip_packet pkt;
    pkt.command = 2; // Response
    pkt.version = 2;
    pkt.must_be_zero = 0;

    // Here you would loop through your routing table in memory
    // For now we pretend to send 1 dummy route
    pkt.entries[0].addr_family = htons(2);
    pkt.entries[0].route_tag = 0;
    pkt.entries[0].ip_address = inet_addr("10.10.20.0"); // Destination network
    pkt.entries[0].subnet_mask = inet_addr("255.255.255.0");
    pkt.entries[0].next_hop = inet_addr("0.0.0.0");
    pkt.entries[0].metric = htonl(1); // Cost 1

    // Send packet (4 byte header + 1 entry 20 bytes = 24 bytes)
    send_rip_packet(sock, &pkt, 1);
}

// Function called when a packet is received in main
void process_rip_packet(struct rip_packet *pkt, const char *sender_ip)
{
    if (pkt->command == 1)
    {
        printf("Received a Request from %s. I must respond immediately!\n", sender_ip);
        // TODO: Immediate table response
    }
    else if (pkt->command == 2)
    {
        printf("Received an Update from %s. Checking routes...\n", sender_ip);
        struct in_addr addr;
        addr.s_addr = pkt->entries[0].ip_address;
        printf("Network received: %s\n", inet_ntoa(addr));

        // Example logic for kernel interaction (simplified Bellman-Ford algorithm)
        /*
         * If we find a better route:
         * 1. Update it in our C struct in memory
         * 2. Update it in Linux kernel using system():
         */

        // EXAMPLE SYSTEM COMMAND:
        // char cmd[256];
        // snprintf(cmd, sizeof(cmd), "ip route replace %s/24 via %s", network_received, sender_ip);
        // printf("Executing on kernel: %s\n", cmd);
        // system(cmd);
    }
}