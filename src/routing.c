#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "routing.h"
#include "network.h"

void send_routes(int sock)
{
    struct rip_packet pkt;
    pkt.command = 2; // Response
    pkt.version = 2;
    pkt.must_be_zero = 0;

    // Send all configured networks as RIP entries
    int num_entries = 0;
    for (int i = 0; i < num_networks && num_entries < 25; i++)
    {
        if (networks[i].interface_name[0] == '\0')
            continue; // Interface not enabled

        pkt.entries[num_entries].addr_family = htons(2);
        pkt.entries[num_entries].route_tag = 0;
        pkt.entries[num_entries].ip_address = networks[i].network;
        pkt.entries[num_entries].subnet_mask = networks[i].netmask;
        pkt.entries[num_entries].next_hop = inet_addr("0.0.0.0");
        pkt.entries[num_entries].metric = htonl(1); // Cost 1

        num_entries++;
    }

    if (num_entries > 0)
    {
        printf("[SEND] Sending %d routes\n", num_entries);
        send_rip_packet(sock, &pkt, num_entries);
    }
    else
    {
        printf("[SEND] Warning: No routes to send\n");
    }
}

void process_rip_packet(struct rip_packet *pkt, int bytes_received, const char *sender_ip)
{
    if (pkt->command == 1)
    {
        printf("Received a Request from %s. I must respond immediately!\n", sender_ip);
        // TODO: Inviare la tabella di routing locale al mittente
    }
    else if (pkt->command == 2)
    {
        // 1. Calcoliamo quante rotte ci sono in QUESTO pacchetto
        int num_entries = (bytes_received - 4) / sizeof(struct rip_rte);

        // Limite di sicurezza imposto dallo standard RIP
        if (num_entries > 25)
        {
            num_entries = 25;
        }

        printf("Received an Update from %s with %d routes. Checking routes...\n", sender_ip, num_entries);
        struct in_addr addr;

        // 2. Il ciclo DEVE partire da 0 e arrivare a num_entries
        for (int i = 0; i < num_entries; i++)
        {
            // 3. Controllo opzionale ma consigliato: assicuriamoci che sia una rotta IPv4 (Family = 2)
            uint16_t family = ntohs(pkt->entries[i].addr_family);

            if (family == 2 || family == 0)
            {
                addr.s_addr = pkt->entries[i].ip_address;

                // Nota: la metrica viaggia in formato Network, la convertiamo in Host per leggerla
                uint32_t metric = ntohl(pkt->entries[i].metric);

                printf("Network received [%d]: %s (Metric: %u)\n", i, inet_ntoa(addr), metric);

                // --- LOGICA BELLMAN-FORD QUI ---
                // Se (metric + 1 < metrica_attuale_in_tabella) {
                //     aggiorna_struttura_dati_c();
                //     aggiorna_kernel_linux();
                // }
            }
        }
    }
}