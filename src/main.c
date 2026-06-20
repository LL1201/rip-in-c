#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "network.h"
#include "rip-protocol-specs.h"
#include "routing.h"

#define UPDATE_TIMER 30 // Seconds between each update

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout to show logs with docker

    printf("RIPinC Daemon starting...\n");

    // 1. Initialization (config parsing will be added later)
    // init_routing_table();

    // 2. Create UDP Multicast socket
    int sock = create_rip_socket();
    printf("Socket created. Listening on %s:%d\n", RIP_MULTICAST_ADDR, RIP_PORT);

    // Variables for select()
    fd_set readfds;
    struct timeval tv, now, next_update;

    // Buffer to receive packets
    struct rip_packet incoming_packet;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    gettimeofday(&now, NULL);
    next_update = now;
    next_update.tv_sec += UPDATE_TIMER;

    // 3. Daemon infinite loop
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        gettimeofday(&now, NULL);

        // 2. Controllo primario: è arrivato il momento di aggiornare?
        // Se il tempo attuale ha superato o raggiunto il 'next_update'
        if (now.tv_sec > next_update.tv_sec ||
            (now.tv_sec == next_update.tv_sec && now.tv_usec >= next_update.tv_usec))
        {
            printf("[TIMER] 30 seconds elapsed. Sending RIP update...\n");
            send_routes(sock); // Invia l'update

            // Ricalcola il prossimo traguardo tra 30 secondi a partire da ORA
            gettimeofday(&now, NULL);
            next_update = now;
            next_update.tv_sec += UPDATE_TIMER;
        }

        // 3. Calcola il tempo rimanente effettivo per la select
        tv.tv_sec = next_update.tv_sec - now.tv_sec;
        tv.tv_usec = next_update.tv_usec - now.tv_usec;

        // Gestione del prestito (se i microsecondi sono negativi)
        if (tv.tv_usec < 0)
        {
            tv.tv_sec -= 1;
            tv.tv_usec += 1000000;
        }

        // Se per qualche motivo strano il timer è negativo, mettilo a 0 per non far crashare select
        if (tv.tv_sec < 0)
        {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
        }

        // select() aspetta: o arriva un pacchetto o finisce il TEMPO RIMANENTE
        int activity = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0)
        {
            perror("Error in select");
            break;
        }
        else if (activity > 0 && FD_ISSET(sock, &readfds))
        {
            // UN PACCHETTO È ARRIVATO!
            // Il timer in 'next_update' non viene toccato, quindi al prossimo giro
            // la select aspetterà per i secondi RIMANENTI, non per altri 30 secondi interi.

            int bytes_received = recvfrom(sock, &incoming_packet, sizeof(struct rip_packet), 0,
                                          (struct sockaddr *)&sender_addr, &sender_len);
            if (bytes_received > 0)
            {
                char sender_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

                printf("[RECV] Received %d bytes from %s\n", bytes_received, sender_ip);

                process_rip_packet(&incoming_packet, bytes_received, sender_ip);
            }
        }
        // Nota: Non serve più gestire "else if (activity == 0)" qui!
        // Se la select scade (0), il loop si riavvia semplicemente.
        // L'if all'inizio del loop (now > next_update) se ne accorgerà e farà scattare send_routes().
    }

    close(sock);
    return 0;
}