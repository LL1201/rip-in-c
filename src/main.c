#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "network.h"
#include "rip-protocol-structures.h"
#include "routing.h"

#define UPDATE_TIMER 30 // Seconds between each update

int main()
{
    printf("RIPinC Daemon starting...\n");

    // 1. Initialization (config parsing will be added later)
    // init_routing_table();

    // 2. Create UDP Multicast socket
    int sock = create_rip_socket();
    printf("Socket created. Listening on %s:%d\n", RIP_MULTICAST_ADDR, RIP_PORT);

    // Variables for select()
    fd_set readfds;
    struct timeval tv;

    // Buffer to receive packets
    struct rip_packet incoming_packet;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // 3. Daemon infinite loop
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Set timer for next update
        tv.tv_sec = UPDATE_TIMER;
        tv.tv_usec = 0;

        // select() waits: either a packet arrives or the timer expires
        int activity = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0)
        {
            perror("Error in select");
            break;
        }
        else if (activity == 0)
        {
            // TIMER EXPIRED: Time to send our routing table to neighbors!
            printf("[TIMER] 30 seconds elapsed. Sending RIP update...\n");
            send_my_routes(sock); // Function defined in routing.c
        }
        else
        {
            // A PACKET ARRIVED: Someone sent us their routes
            if (FD_ISSET(sock, &readfds))
            {
                int bytes_received = recvfrom(sock, &incoming_packet, sizeof(struct rip_packet), 0,
                                              (struct sockaddr *)&sender_addr, &sender_len);
                if (bytes_received > 0)
                {
                    char sender_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

                    printf("[RECV] Received %d bytes from %s\n", bytes_received, sender_ip);

                    // Pass packet to routing logic for analysis
                    process_rip_packet(&incoming_packet, sender_ip);
                }
            }
        }
    }

    close(sock);
    return 0;
}