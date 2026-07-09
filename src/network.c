#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "network.h"
#include "rip-protocol-specs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define CONFIG_FILE "/app/router.conf"

// Global network configuration
struct network_config networks[MAX_NETWORKS];
int num_networks = 0;

// Parse della notazione CIDR nel file di config e copia dei valori nei campi puntati dai puntatori
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
int ip_in_network(uint32_t ip, uint32_t network, uint32_t netmask)
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

int create_netlink_socket()
{
    struct sockaddr_nl saddr;

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    if (sock < 0)
    {
        perror("Failed to open netlink socket");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.nl_family = AF_NETLINK;
    saddr.nl_pid = (unsigned int)getpid();
    saddr.nl_groups = RTMGRP_LINK;

    if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        perror("Failed to bind netlink socket");
        close(sock);
        return -1;
    }

    return sock;
}

int handle_netlink_link_events(int nl_sock)
{
    char buffer[4096];
    int saw_link_event = 0;

    while (1)
    {
        ssize_t len = recv(nl_sock, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            perror("[NETLINK] recv failed");
            break;
        }

        if (len == 0)
            break;

        for (struct nlmsghdr *nlh = (struct nlmsghdr *)buffer; NLMSG_OK(nlh, (unsigned int)len); nlh = NLMSG_NEXT(nlh, len))
        {
            if (nlh->nlmsg_type == RTM_NEWLINK || nlh->nlmsg_type == RTM_DELLINK || nlh->nlmsg_type == RTM_SETLINK)
                saw_link_event = 1;
        }
    }

    return saw_link_event;
}

int create_rip_socket()
{
    // carica la config dal file
    load_config();

    if (num_networks == 0)
    {
        fprintf(stderr, "Error: No networks configured in RIP Database");
        exit(1);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    // reuse della porta del socket utile se il programma va in crash e riparte subito
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // RIP binding
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
        // skippa le interfacce di loopback oppure spente
        if (ifa->ifa_addr == NULL || !(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
            continue;

        // invia solo su interfacce ipv4
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        uint32_t interface_ip = addr->sin_addr.s_addr;

        // per ogni scheda di rete controlla se il suo ip rientra tra quelle da abilitare
        // in caso fa il join al gruppo multicast
        for (int i = 0; i < num_networks; i++)
        {
            if (ip_in_network(interface_ip, networks[i].network, networks[i].netmask))
            {
                // join al gruppo multicast
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
                    networks[i].local_ip = interface_ip;
                }
                break;
            }
        }
    }

    freeifaddrs(ifaddr);

    // disabilita IP_MULTICAST_LOOP in modo che i pacchetti in uscita non vengano ricevuti da sé stesso
    int loop = 0;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    return sock;
}

void send_rip_packet(int sock, struct rip_packet *pkt, int num_entries, struct sockaddr_in *dest, struct in_addr *out_iface_ip)
{
    int packet_size = 4 + (num_entries * sizeof(struct rip_rte));

    // Se passiamo un IP di interfaccia e l'indirizzo di destinazione è multicast, bindiamo l'uscita
    if (out_iface_ip != NULL && dest->sin_addr.s_addr == inet_addr(RIP_MULTICAST_ADDR))
    {
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, out_iface_ip, sizeof(struct in_addr));
    }

    ssize_t sent = sendto(sock, pkt, packet_size, 0, (struct sockaddr *)dest, sizeof(struct sockaddr_in));
    if (sent < 0)
    {
        perror("[ERROR] sendto failed");
    }
}