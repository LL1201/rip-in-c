#ifndef PROTOCOL_RIP_H
#define PROTOCOL_RIP_H

#include <stdint.h>

#define RIP_PORT 520
#define RIP_MULTICAST_ADDR "224.0.0.9"

struct rip_rte
{
    uint16_t addr_family;
    uint16_t route_tag;
    uint32_t ip_address;
    uint32_t subnet_mask;
    uint32_t next_hop;
    uint32_t metric;
} __attribute__((packed));

struct rip_packet
{
    uint8_t command; // 1=Request, 2=Response
    uint8_t version; // 2
    uint16_t must_be_zero;
    struct rip_rte entries[25];
} __attribute__((packed));

#endif