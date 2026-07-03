#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "network.h"
#include "rip-protocol-specs.h"
#include "routing.h"

#define BASE_UPDATE_TIMER 5

static volatile sig_atomic_t shutdown_requested = 0;

static void handle_shutdown_signal(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

// funzione helper per calcolare il timer con jitter (30 sec +/- 5)
int get_jittered_timer()
{
    // rand() % 11 genera un numero tra 0 e 10.
    // sottraendo 5, otteniamo un numero tra -5 e +5.
    int offset = (rand() % 11) - 5;
    return BASE_UPDATE_TIMER + offset;
}

int main()
{
    // buffering disabilitato per leggere i log tramite docker logs
    setvbuf(stdout, NULL, _IONBF, 0);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_shutdown_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    printf("RIPinC Daemon starting...\n");

    // Create UDP Multicast socket
    int sock = create_rip_socket();
    printf("Socket created. Listening on %s:%d\n", RIP_MULTICAST_ADDR, RIP_PORT);

    srand(time(NULL) ^ getpid());

    // Inizializzazione del rip database con le reti e interfacce individuate nella create socket
    init_rip_database();
    print_routing_table();

    // Variables for select()
    fd_set readfds;
    struct timeval tv, now, next_update;

    // Buffer to receive packets
    struct rip_packet incoming_packet;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    // next update contiene il timestamp del momento in cui l'update è da inviare
    gettimeofday(&now, NULL);
    next_update = now;
    next_update.tv_sec += get_jittered_timer();

    // TODO vedere il multithreading
    while (1)
    {
        if (shutdown_requested)
            break;

        expire_timed_out_routes(sock);
        refresh_local_interface_routes(sock);

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        gettimeofday(&now, NULL);

        // Invia l'unsolicited update solo se
        // l'attuale timestamp è maggiore di quello del next update (sono passati i secondi necessari)
        // oppure i secondi sono uguali e i microsecondi maggiori o uguali (per essere precisi)
        if (now.tv_sec > next_update.tv_sec ||
            (now.tv_sec == next_update.tv_sec && now.tv_usec >= next_update.tv_usec))
        {
            printf("[TIMER] Sending RIP update...\n");
            send_unsolicited_update(sock);

            // ricalcolo timer
            gettimeofday(&now, NULL);
            next_update = now;

            next_update.tv_sec += get_jittered_timer();
        }

        // calcola il tempo rimanente effettivo per la select
        // timersub fa tv = next_update - now
        timersub(&next_update, &now, &tv);

        // se prima della select next_update è già stato superato evitiamo che la select tuoni avendo valori negativi
        if (tv.tv_sec < 0)
        {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
        }

        int activity = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0)
        {
            if (errno == EINTR && shutdown_requested)
                break;

            perror("Error in select");
            break;
        }
        else if (activity > 0 && FD_ISSET(sock, &readfds))
        {
            // è arrivato un pacchetto
            // Il timer in next_update non viene toccato
            // la select aspetterà per i secondi rimanenti, non per altri 30 secondi interi.

            int bytes_received = recvfrom(sock, &incoming_packet, sizeof(struct rip_packet), 0,
                                          (struct sockaddr *)&sender_addr, &sender_len);
            if (bytes_received > 0)
            {
                char sender_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN);

                printf("[RECV] Received %d bytes from %s\n", bytes_received, sender_ip);

                process_rip_packet(sock, &incoming_packet, bytes_received, &sender_addr);
            }
        }
        // if (activity == 0) il loop si riavvia
    }

    graceful_shutdown(sock);

    close(sock);
    return 0;
}