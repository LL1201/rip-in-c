#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "network.h"
#include "rip-protocol-specs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>

#define CONFIG_FILE "/app/router.conf"

// Global network configuration (defined in header, declared here)
struct network_config networks[MAX_NETWORKS];
int num_networks = 0;

// Parse della notazione CIDR nel file di config
static void parse_cidr(const char *cidr, uint32_t *network, uint32_t *netmask)
{
    char *slash = strchr(cidr, '/');
    if (!slash)
    {
        fprintf(stderr, "Invalid CIDR notation: %s\n", cidr);
        return;
    }

    // Extract IP part
    char ip_part[INET_ADDRSTRLEN];
    strncpy(ip_part, cidr, slash - cidr);
    ip_part[slash - cidr] = '\0';

    // Parse IP address
    *network = inet_addr(ip_part);

    // Parse prefix length and convert to netmask
    int prefix = atoi(slash + 1);
    *netmask = htonl(~((1 << (32 - prefix)) - 1));
}

// Check if an IP address belongs to a network
static int ip_in_network(uint32_t ip, uint32_t network, uint32_t netmask)
{
    return (ip & netmask) == (network & netmask);
}

// Load networks from config file
static void load_config()
{
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f)
    {
        fprintf(stderr, "Warning: Could not open config file %s\n", CONFIG_FILE);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f) && num_networks < MAX_NETWORKS)
    {
        // Remove newline
        line[strcspn(line, "\n")] = 0;

        if (strncmp(line, "NET=", 4) == 0)
        {
            parse_cidr(line + 4, &networks[num_networks].network, &networks[num_networks].netmask);
            printf("Loaded network: %s\n", line + 4);
            num_networks++;
        }
    }

    fclose(f);
}

int create_rip_socket()
{
    // Load configuration first
    load_config();

    if (num_networks == 0)
    {
        fprintf(stderr, "Error: No networks configured in %s\n", CONFIG_FILE);
        exit(1);
    }

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
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("bind() failed");
        exit(1);
    }

    // Join RIPv2 Multicast group on interfaces that match configured subnets
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) < 0)
    {
        perror("getifaddrs() failed");
        exit(1);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        // Skip interfaces that are not up or are loopback
        if (ifa->ifa_addr == NULL || !(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        // Only process IPv4 interfaces
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        uint32_t interface_ip = addr->sin_addr.s_addr;

        // Check if this interface IP belongs to any configured network
        for (int i = 0; i < num_networks; i++)
        {
            if (ip_in_network(interface_ip, networks[i].network, networks[i].netmask))
            {
                // Join multicast group on this interface
                struct ip_mreq mreq;
                mreq.imr_multiaddr.s_addr = inet_addr(RIP_MULTICAST_ADDR);
                mreq.imr_interface.s_addr = interface_ip;

                if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
                {
                    fprintf(stderr, "Warning: Could not join multicast on %s\n", ifa->ifa_name);
                }
                else
                {
                    printf("RIP enabled on: %s (%s)\n", ifa->ifa_name, inet_ntoa(addr->sin_addr));
                    strncpy(networks[i].interface_name, ifa->ifa_name, IF_NAMESIZE - 1);
                }
                break;
            }
        }
    }

    freeifaddrs(ifaddr);

    // Disable multicast loopback to avoid receiving our own packets
    int loop = 0;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

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

    // Send RIP packet on all configured network interfaces
    for (int i = 0; i < num_networks; i++)
    {
        if (networks[i].interface_name[0] == '\0')
            continue; // Interface was not successfully enabled

        struct in_addr interface_addr;
        interface_addr.s_addr = 0;

        // Get the actual interface address
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) < 0)
            continue;

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
                continue;

            if (strcmp(ifa->ifa_name, networks[i].interface_name) == 0)
            {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                interface_addr = addr->sin_addr;
                break;
            }
        }

        freeifaddrs(ifaddr);

        if (interface_addr.s_addr == 0)
            continue;

        // Set which interface to send multicast on
        if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) < 0)
        {
            fprintf(stderr, "Warning: Could not set multicast interface for %s\n", networks[i].interface_name);
            continue;
        }

        // Send RIP packet to Multicast group
        ssize_t sent = sendto(sock, packet, sizeof(struct rip_packet), 0,
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));

        if (sent < 0)
        {
            fprintf(stderr, "Error sending RIP packet on %s\n", networks[i].interface_name);
        }
        else
        {
            printf("[SEND] RIP update sent on: %s\n", networks[i].interface_name);
        }
    }
}