#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "routing.h"
#include "network.h"
#include <string.h>
#include "rip-protocol-specs.h"
#include "network.h"
#include <unistd.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>

#define CONFIG_FILE "/app/router.conf"

struct routing_table rip_database;

// Helper: Convert subnet mask to CIDR prefix length
static int mask_to_prefix(uint32_t mask)
{
    int prefix = 0;
    uint32_t m = ntohl(mask);
    while (m & 0x80000000)
    {
        prefix++;
        m <<= 1;
    }
    return prefix;
}

struct route_entry *find_route(uint32_t network, uint32_t subnet_mask)
{
    for (int i = 0; i < rip_database.num_entries; i++)
    {
        // Se IP di rete e Subnet Mask coincidono, abbiamo trovato la rotta!
        if (rip_database.entries[i].network == network &&
            rip_database.entries[i].subnet_mask == subnet_mask)
        {
            return &rip_database.entries[i];
        }
    }
    return NULL; // Rotta non trovata
}

static void send_routing_table(int sock, struct sockaddr_in *dest, const char *split_horizon_iface, struct in_addr *out_iface_ip)
{
    if (rip_database.num_entries == 0)
        return;

    struct rip_packet pkt;
    memset(&pkt, 0, sizeof(struct rip_packet));
    pkt.command = 2; // Update / Response
    pkt.version = 2;
    int num_entries_in_pkt = 0;

    for (int i = 0; i < rip_database.num_entries; i++)
    {
        struct route_entry *route = &rip_database.entries[i];
        uint32_t metric = route->metric;

        // Split Horizon con Poisoned Reverse
        if (!route->is_local && strcmp(route->interface_name, split_horizon_iface) == 0)
        {
            metric = 16;
        }

        pkt.entries[num_entries_in_pkt].addr_family = htons(2);
        pkt.entries[num_entries_in_pkt].ip_address = route->network;
        pkt.entries[num_entries_in_pkt].subnet_mask = route->subnet_mask;
        pkt.entries[num_entries_in_pkt].next_hop = inet_addr("0.0.0.0");
        pkt.entries[num_entries_in_pkt].metric = htonl(metric);

        num_entries_in_pkt++;

        // Paginazione: se raggiungiamo 25 entry, inviamo il pacchetto e resettiamo
        if (num_entries_in_pkt == 25)
        {
            send_rip_packet(sock, &pkt, 25, dest, out_iface_ip);

            memset(&pkt, 0, sizeof(struct rip_packet));
            pkt.command = 2;
            pkt.version = 2;
            num_entries_in_pkt = 0;
        }
    }

    // Inviamo le eventuali entry rimanenti
    if (num_entries_in_pkt > 0)
    {
        send_rip_packet(sock, &pkt, num_entries_in_pkt, dest, out_iface_ip);
    }
}

void send_unsolicited_update(int sock)
{
    // destinazione fissa: Multicast RIP
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(RIP_PORT);
    dest.sin_addr.s_addr = inet_addr(RIP_MULTICAST_ADDR);

    // iterazione su ogni interfaccia
    for (int iface_idx = 0; iface_idx < num_networks; iface_idx++)
    {
        if (networks[iface_idx].interface_name[0] == '\0')
            continue;

        struct in_addr iface_ip;
        iface_ip.s_addr = networks[iface_idx].local_ip;

        send_routing_table(sock, &dest, networks[iface_idx].interface_name, &iface_ip);

        printf("[SEND] Periodic Update sent on interface %s\n", networks[iface_idx].interface_name);
    }
}

void send_full_table_unicast(int sock, struct sockaddr_in *requester_addr, const char *request_iface_name)
{
    // Passiamo NULL come out_iface_ip perché in unicast il kernel sa già da dove uscire.
    send_routing_table(sock, requester_addr, request_iface_name, NULL);

    printf("[SEND] Full table Unicast response sent to %s\n", inet_ntoa(requester_addr->sin_addr));
}

void init_rip_database(void)
{
    // 1. Azzera la tabella rip
    rip_database.num_entries = 0;

    if (num_networks == 0)
    {
        fprintf(stderr, "Error: No networks configured in %s\n", CONFIG_FILE);
        exit(1);
    }

    // 2. Itera sulle reti fisiche che hai configurato
    for (int i = 0; i < num_networks; i++)
    {
        if (networks[i].interface_name[0] == '\0')
            continue; // Interfaccia non configurata/valida

        // 3. Inserisci la rete locale nella Tabella RIP
        add_route(
            networks[i].network,        // Indirizzo IP di rete (in Network Byte Order)
            networks[i].netmask,        // Subnet mask
            inet_addr("0.0.0.0"),       // Next Hop: 0.0.0.0 (significa "sono io, connessione diretta")
            1,                          // Metrica iniziale: 1
            networks[i].interface_name, // Nome dell'interfaccia (es. "eth0")
            1                           // is_local = 1 (FONDAMENTALE!)
        );

        printf("[INIT] Added local interface %s to routing table.\n", networks[i].interface_name);
    }
}

void add_route(uint32_t network, uint32_t subnet_mask, uint32_t next_hop, uint32_t metric, const char *interface_name, int is_local)
{
    if (rip_database.num_entries >= MAX_ROUTING_TABLE_ENTRIES)
    {
        printf("[ERROR] RIP Database is full! Cannot add more routes.\n");
        return;
    }

    // puntatore alla prima entry libera
    struct route_entry *new_route = &rip_database.entries[rip_database.num_entries];

    // popolamento dei campi
    new_route->network = network;
    new_route->subnet_mask = subnet_mask;
    new_route->next_hop = next_hop;
    new_route->metric = metric;

    // copia sicura del nome dell'interfaccia
    strncpy(new_route->interface_name, interface_name, IF_NAMESIZE - 1);
    new_route->interface_name[IF_NAMESIZE - 1] = '\0';

    new_route->is_local = is_local;
    new_route->last_update = time(NULL);

    rip_database.num_entries++;
}

void print_routing_table(void)
{
    printf("\n=========================================================================================\n");
    printf("                                      RIP DATABASE \n");
    printf("=========================================================================================\n");
    printf("%-16s | %-16s | %-16s | %-6s | %-8s | %-5s | %-6s\n",
           "Network", "Netmask", "Next Hop", "Metric", "Iface", "Type", "Age(s)");
    printf("-----------------------------------------------------------------------------------------\n");

    if (rip_database.num_entries == 0)
    {
        printf("  Database is empty.\n");
    }

    time_t now = time(NULL);

    // Iteriamo su tutte le rotte presenti nel database
    for (int i = 0; i < rip_database.num_entries; i++)
    {
        struct route_entry *entry = &rip_database.entries[i];

        // Strutture d'appoggio per convertire gli IP in stringhe
        struct in_addr net, mask, hop;
        char net_str[INET_ADDRSTRLEN];
        char mask_str[INET_ADDRSTRLEN];
        char hop_str[INET_ADDRSTRLEN];

        net.s_addr = entry->network;
        mask.s_addr = entry->subnet_mask;
        hop.s_addr = entry->next_hop;

        // inet_ntop è thread-safe e moderno, perfetto per i demone in background
        inet_ntop(AF_INET, &net, net_str, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &mask, mask_str, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, &hop, hop_str, INET_ADDRSTRLEN);

        // Calcoliamo da quanti secondi la rotta non viene aggiornata
        int age = (int)difftime(now, entry->last_update);

        // Etichetta visiva per distinguere subito le nostre interfacce da quelle imparate
        char *type = entry->is_local ? "Local" : "RIP";

        // Stampiamo la riga tabellare (i numeri dopo % indicano la spaziatura fissa)
        printf("%-16s | %-16s | %-16s | %-6u | %-8s | %-5s | %-6d\n",
               net_str, mask_str, hop_str, entry->metric, entry->interface_name, type, age);
    }
    printf("=========================================================================================\n\n");
}

void process_route(uint32_t network, uint32_t netmask, uint32_t pkt_next_hop, uint32_t received_metric, const char *sender_ip)
{
    // calcolo della nuova metrica (costo + 1)
    uint32_t new_metric = received_metric + 1;
    if (new_metric > 16)
        new_metric = 16;

    // Gestione del Next Hop secondo lo standard RIPv2 (RFC 2453)
    // Se il campo next_hop nel pacchetto è 0.0.0.0, il traffico va inviato al mittente del pacchetto.
    // Altrimenti, si usa l'IP specificato nel pacchetto.
    uint32_t actual_next_hop = pkt_next_hop;
    if (actual_next_hop == 0 || actual_next_hop == inet_addr("0.0.0.0"))
        actual_next_hop = inet_addr(sender_ip);

    // ricerca della rete nel database
    struct route_entry *existing_route = find_route(network, netmask);

    // caso 1: rotta nuova
    if (existing_route == NULL)
    {
        if (new_metric < 16)
        {
            // interface_name "RIP" per capire che è dinamica. is_local = 0.
            add_route(network, netmask, actual_next_hop, new_metric, "RIP", 0);

            struct in_addr net_addr;
            net_addr.s_addr = network;
            printf("[RIP] Learned NEW route: %s via %s (Metric: %u)\n", inet_ntoa(net_addr), sender_ip, new_metric);

            // aggiornamento routing table dell'host
            char cmd[256];
            int prefix = mask_to_prefix(netmask);
            snprintf(cmd, sizeof(cmd), "ip route add %s/%d via %s metric %u", inet_ntoa(net_addr), prefix, sender_ip, new_metric);
            printf("[KERNEL] Executing: %s\n", cmd);
            int ret = system(cmd);
            if (ret != 0)
                printf("[KERNEL] Command failed with code %d\n", ret);
        }
    }
    // caso 2: la rotta esiste già nel database
    else
    {
        // se locale non va toccata
        if (existing_route->is_local == 1)
            return;

        // controllo se arriva dallo stesso router da cui l'avevamo imparata
        // si aggiorna sempre (reset timer e metrica) anche se la rotta è peggiorata
        if (existing_route->next_hop == actual_next_hop)
        {
            if (existing_route->metric != new_metric)
            {
                struct in_addr net_addr;
                net_addr.s_addr = network;
                printf("[RIP] Route CHANGED for %s: old metric %u, new metric %u\n",
                       inet_ntoa(net_addr), existing_route->metric, new_metric);

                // aggiornamento routing table dell'host
                if (new_metric == 16)
                {
                    char cmd[256];
                    int prefix = mask_to_prefix(netmask);
                    snprintf(cmd, sizeof(cmd), "ip route del %s/%d", inet_ntoa(net_addr), prefix);
                    printf("[KERNEL] Route unreachable, executing: %s\n", cmd);
                    system(cmd);
                }
                else
                {
                    char cmd[256];
                    int prefix = mask_to_prefix(netmask);
                    snprintf(cmd, sizeof(cmd), "ip route replace %s/%d via %s metric %u",
                             inet_ntoa(net_addr), prefix, sender_ip, new_metric);
                    printf("[KERNEL] Executing: %s\n", cmd);
                    system(cmd);
                }
            }

            existing_route->metric = new_metric;
            existing_route->last_update = time(NULL); // Azzera il timer di scadenza (Garbage Collector)
        }
        // la rotta arriva da un router diverso ma offre un percorso migliore
        else if (new_metric < existing_route->metric)
        {
            struct in_addr net_addr;
            net_addr.s_addr = network;
            printf("[RIP] Route IMPROVED for %s via %s (Metric: %u -> %u)\n",
                   inet_ntoa(net_addr), sender_ip, existing_route->metric, new_metric);

            existing_route->next_hop = actual_next_hop;
            existing_route->metric = new_metric;
            existing_route->last_update = time(NULL);

            // aggiornamento routing table dell'host
            char cmd[256];
            int prefix = mask_to_prefix(netmask);
            snprintf(cmd, sizeof(cmd), "ip route replace %s/%d via %s metric %u",
                     inet_ntoa(net_addr), prefix, sender_ip, new_metric);
            printf("[KERNEL] Executing: %s\n", cmd);
            int ret = system(cmd);
            if (ret != 0)
                printf("[KERNEL] Command failed with code %d\n", ret);
        }

        print_routing_table();
        // se la rotta arriva da un router diverso ed ha un costo uguale o peggiore viene ignorata
    }
}

void process_rip_packet(int sock, struct rip_packet *pkt, int bytes_received, struct sockaddr_in *sender_addr)
{
    // calcolo del numero di entry nel pacchetto
    int num_entries = (bytes_received - 4) / sizeof(struct rip_rte);
    if (num_entries > 25)
    {
        num_entries = 25;
    }

    // Estraiamo l'IP per i log
    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sender_addr->sin_addr), sender_ip, INET_ADDRSTRLEN);

    // command = 1; REQUEST
    if (pkt->command == 1)
    {
        if (num_entries == 0)
            return;

        // gestione del caso di richiesta dell'intera tabella
        if (num_entries == 1 &&
            ntohs(pkt->entries[0].addr_family) == 0 &&
            ntohl(pkt->entries[0].metric) == 16)
        {
            printf("[RIP] Full routing table requested by %s. Sending database...\n", sender_ip);

            // scopro da quale interfaccia è arrivata la richiesta
            char request_iface[IF_NAMESIZE] = "";
            for (int i = 0; i < num_networks; i++)
            {
                if (ip_in_network(sender_addr->sin_addr.s_addr, networks[i].network, networks[i].netmask))
                {
                    strncpy(request_iface, networks[i].interface_name, IF_NAMESIZE - 1);
                    break;
                }
            }

            send_full_table_unicast(sock, sender_addr, request_iface);

            return;
        }

        // richiesta per rotte specifiche
        printf("[RIP] Specific route request from %s for %d networks. Processing...\n", sender_ip, num_entries);

        for (int i = 0; i < num_entries; i++)
        {
            uint32_t network = pkt->entries[i].ip_address;
            uint32_t netmask = pkt->entries[i].subnet_mask;

            struct route_entry *route = find_route(network, netmask);

            if (route != NULL)
            {
                pkt->entries[i].metric = htonl(route->metric);
            }
            else
            {
                pkt->entries[i].metric = htonl(16);
            }
        }

        pkt->command = 2; // da Request diventa response (come suggerisce RFC)
        int packet_size = 4 + (num_entries * sizeof(struct rip_rte));

        int sent = sendto(sock, pkt, packet_size, 0, (struct sockaddr *)sender_addr, sizeof(struct sockaddr_in));

        if (sent > 0)
        {
            printf("[SEND] Sent specific Response back to %s\n", sender_ip);
        }
        else
        {
            perror("[ERROR] Failed to reply to Request");
        }
    }

    // command = 2; UPDATE
    else if (pkt->command == 2)
    {
        printf("Received an Update from %s with %d routes. Checking routes...\n", sender_ip, num_entries);

        for (int i = 0; i < num_entries; i++)
        {
            uint16_t family = ntohs(pkt->entries[i].addr_family);

            if (family == 2)
            {
                uint32_t network = pkt->entries[i].ip_address;
                uint32_t netmask = pkt->entries[i].subnet_mask;
                uint32_t next_hop = pkt->entries[i].next_hop;
                uint32_t metric = ntohl(pkt->entries[i].metric);

                if (metric > 0 && metric <= 16)
                {
                    process_route(network, netmask, next_hop, metric, sender_ip);
                }
            }
        }
    }
}