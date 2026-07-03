#ifndef ROUTING_H
#define ROUTING_H

#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <net/if.h>
#include "rip-protocol-specs.h"

#define MAX_ROUTING_TABLE_ENTRIES 100
#define ROUTE_TIMEOUT 30            // per dimostrazione lo abbasso
#define GARBAGE_COLLECTION_TIMER 20 // Tempo di garbage collection dopo poison della metrica a 16

// Internal routing table entry for RIP
struct route_entry
{
    uint32_t network;                 // Network address
    uint32_t subnet_mask;             // Network mask
    uint32_t next_hop;                // Next hop IP address
    uint32_t metric;                  // Cost (RIP metric)
    char interface_name[IF_NAMESIZE]; // Outgoing interface
    time_t last_update;               // Timestamp of last update (for expiration)
    time_t invalid_since;             // Timestamp when metric became 16
    int is_local;                     // 1 if directly connected, 0 if learned from RIP
};

// Routing table structure
struct routing_table
{
    struct route_entry entries[MAX_ROUTING_TABLE_ENTRIES];
    int num_entries;
};

// Rip routing table
extern struct routing_table rip_database;

void send_unsolicited_update(int sock);
int expire_timed_out_routes(int sock);
int refresh_local_interface_routes(int sock);
void graceful_shutdown(int sock);

void init_rip_database(void);
void send_full_table_unicast(int sock, struct sockaddr_in *requester_addr, const char *request_iface_name);
void process_rip_packet(int sock, struct rip_packet *pkt, int bytes_received, struct sockaddr_in *sender_addr);

// Routing table management functions
void init_rip_database(void);
void add_route(uint32_t network, uint32_t subnet_mask, uint32_t next_hop, uint32_t metric, const char *interface_name, int is_local);
// void update_route(uint32_t network, uint32_t subnet_mask, uint32_t next_hop, uint32_t metric);
// void remove_route(uint32_t network, uint32_t subnet_mask);
struct route_entry *find_route(uint32_t network, uint32_t subnet_mask);
void print_routing_table(void);

#endif