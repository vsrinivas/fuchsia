// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inet6/inet6.h>
#include <pretty/hexdump.h>
#include <zircon/assert.h>
#include <zircon/boot/netboot.h>
#include <zircon/device/ethernet.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 2048
#define ROUNDUP(a, b)   (((a) + ((b)-1)) & ~((b)-1))

typedef struct {
    const char* device;
    bool raw;
    bool link_level;
    bool promisc;
    size_t packet_count;
    size_t verbose_level;
    int dumpfile;
} netdump_options_t;

typedef struct {
    uint32_t type;
    uint32_t blk_tot_len;
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint64_t section_len;
    // TODO(smklein): Add options here
    uint32_t blk_tot_len2;
} __attribute__((packed)) pcap_shb_t;

typedef struct {
    uint32_t type;
    uint32_t blk_tot_len;
    uint16_t linktype;
    uint16_t reserved;
    uint32_t snaplen;
    uint32_t blk_tot_len2;
} __attribute__((packed)) pcap_idb_t;

typedef struct {
    uint32_t type;
    uint32_t blk_tot_len;
    uint32_t pkt_len;
} __attribute__((packed)) simple_pkt_t;

#define SIMPLE_PKT_MIN_SIZE (sizeof(simple_pkt_t) + sizeof(uint32_t))

static void print_mac(const uint8_t mac[ETH_ALEN]) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

static const char* ethtype_to_string(uint16_t ethtype) {
    switch (ethtype) {
    case ETH_P_IP: return "IPv4";
    case ETH_P_ARP: return "ARP";
    case ETH_P_IPV6: return "IPV6";
    case ETH_P_8021Q: return "802.1Q";
    default: return "Unknown";
    }
}

static const char* protocol_to_string(uint8_t protocol) {
    switch (protocol) {
    case IPPROTO_HOPOPTS: return "HOPOPTS";
    case IPPROTO_TCP: return "TCP";
    case IPPROTO_UDP: return "UDP";
    case IPPROTO_ICMP: return "ICMP";
    case IPPROTO_ROUTING: return "ROUTING";
    case IPPROTO_FRAGMENT: return "FRAGMENT";
    case IPPROTO_ICMPV6: return "ICMPV6";
    case IPPROTO_NONE: return "NONE";
    default: return "Transport Unknown";
    }
}

static const char* port_to_string(uint16_t port) {
    switch (port) {
    case 7: return "Echo";
    case 20: return "FTP xfer";
    case 21: return "FTP ctl";
    case 22: return "SSH";
    case 23: return "Telnet";
    case 53: return "DNS";
    case 69: return "TFTP";
    case 80: return "HTTP";
    case 115: return "SFTP";
    case 123: return "NTP";
    case 194: return "IRC";
    case 443: return "HTTPS";
    case DEBUGLOG_PORT: return "Netboot Debug";
    case DEBUGLOG_ACK_PORT: return "Netboot Debug ack";
    default: return "";
    }
}

static void print_port(uint16_t port, size_t verbosity) {
    const char* str = port_to_string(port);
    if (verbosity && strcmp(str, "")) {
        printf(":%u (%s) ", port, str);
    } else {
        printf(":%u ", port);
    }
}

void parse_packet(void* packet, size_t length, netdump_options_t* options) {
    struct ethhdr* frame = (struct ethhdr*)(packet);
    if (length < ETH_ZLEN) {
        printf("Packet size (%lu) too small for ethernet frame\n", length);
        if (options->verbose_level == 2) {
            hexdump8_ex(packet, length, 0);
        }
        return;
    }
    uint16_t ethtype = htons(frame->h_proto);

    if (options->link_level) {
        print_mac(frame->h_source);
        printf(" > ");
        print_mac(frame->h_dest);
        printf(", ethertype %s (0x%x), ", ethtype_to_string(ethtype), ethtype);
    }

    struct iphdr* ipv4 = (struct iphdr*)(packet + sizeof(struct ethhdr));
    char buf[256];

    void* transport_packet = NULL;
    uint8_t transport_protocol;

    if (ipv4->version == 4) {
        printf("IP4 ");
        printf("%s > ", inet_ntop(AF_INET, &ipv4->saddr, buf, sizeof(buf)));
        printf("%s: ", inet_ntop(AF_INET, &ipv4->daddr, buf, sizeof(buf)));
        printf("%s, ", protocol_to_string(ipv4->protocol));
        printf("length %u, ", ntohs(ipv4->tot_len));
        transport_packet = (void*)((uintptr_t) ipv4 + sizeof(struct iphdr) +
                                   (ipv4->ihl > 5 ? ipv4->ihl * 4 : 0));
        transport_protocol = ipv4->protocol;
    } else if (ipv4->version == 6) {
        ip6_hdr_t* ipv6 = (ip6_hdr_t*) ipv4;
        printf("IP6 ");
        printf("%s > ", inet_ntop(AF_INET6, &ipv6->src.u8, buf, sizeof(buf)));
        printf("%s: ", inet_ntop(AF_INET6, &ipv6->dst.u8, buf, sizeof(buf)));
        printf("%s, ", protocol_to_string(ipv6->next_header));
        printf("length %u, ", ntohs(ipv6->length));
        transport_packet = (void*)((uintptr_t) ipv6 + sizeof(ip6_hdr_t));
        transport_protocol = ipv6->next_header;
    } else {
        printf("IP Version Unknown (or unhandled)");
    }

    if (transport_packet != NULL) {
        if (transport_protocol == IPPROTO_TCP) {
            struct tcphdr* tcp = (struct tcphdr*) transport_packet;
            printf("Ports ");
            print_port(ntohs(tcp->source), options->verbose_level);
            printf("> ");
            print_port(ntohs(tcp->dest), options->verbose_level);
        } else if (transport_protocol == IPPROTO_UDP) {
            struct udphdr* udp = (struct udphdr*) transport_packet;
            printf("Ports ");
            print_port(ntohs(udp->uh_sport), options->verbose_level);
            printf("> ");
            print_port(ntohs(udp->uh_dport), options->verbose_level);
        } else {
            printf("Transport Version Unknown (or unhandled)");
        }
    }

    printf("\n");
}

int write_shb(int fd) {
    if (fd == -1) {
        return 0;
    }
    pcap_shb_t shb = {
        .type = 0x0A0D0D0A,
        .blk_tot_len = sizeof(pcap_shb_t),
        .magic = 0x1A2B3C4D,
        .major = 1,
        .minor = 0,
        .section_len = 0xFFFFFFFFFFFFFFFF,
        .blk_tot_len2 = sizeof(pcap_shb_t),
    };

    if (write(fd, &shb, sizeof(shb)) != sizeof(shb)) {
        fprintf(stderr, "Couldn't write PCAP Section Header block\n");
        return -1;
    }
    return 0;
}

int write_idb(int fd) {
    if (fd == -1) {
        return 0;
    }
    pcap_idb_t idb = {
        .type = 0x00000001,
        .blk_tot_len = sizeof(pcap_idb_t),
        .linktype = 1,
        .reserved = 0,
        // We can't use a zero here, but tcpdump also rejects 2^32 - 1. Try 2^16 - 1.
        // See http://seclists.org/tcpdump/2012/q2/8.
        .snaplen = 0xFFFF,
        .blk_tot_len2 = sizeof(pcap_idb_t),
    };

    if (write(fd, &idb, sizeof(idb)) != sizeof(idb)) {
        fprintf(stderr, "Couldn't write PCAP Interface Description Block\n");
        return -1;
    }

    return 0;
}

int write_packet(int fd, void* data, size_t len) {
    if (fd == -1) {
        return 0;
    }

    size_t padded_len = ROUNDUP(len, 4);
    simple_pkt_t pkt = {
        .type = 0x00000003,
        .blk_tot_len = SIMPLE_PKT_MIN_SIZE + padded_len,
        .pkt_len = len,
    };

    // TODO(tkilbourn): rewrite this to offload writing to another thread, and also deal with
    // partial writes
    if (write(fd, &pkt, sizeof(pkt)) != sizeof(pkt)) {
        fprintf(stderr, "Couldn't write packet header\n");
        return -1;
    }
    if (write(fd, data, len) != (ssize_t) len) {
        fprintf(stderr, "Couldn't write packet\n");
        return -1;
    }
    if (padded_len > len) {
        size_t padding = padded_len - len;
        ZX_DEBUG_ASSERT(padding <= 3);
        static const uint32_t zero = 0;
        if (write(fd, &zero, padding) != (ssize_t) padding) {
            fprintf(stderr, "Couldn't write padding\n");
            return -1;
        }
    }
    if (write(fd, &pkt.blk_tot_len, sizeof(pkt.blk_tot_len)) != sizeof(pkt.blk_tot_len)) {
        fprintf(stderr, "Couldn't write packet footer\n");
        return -1;
    }

    return 0;
}

void handle_rx(zx_handle_t rx_fifo, char* iobuf, unsigned count, netdump_options_t* options) {
    eth_fifo_entry_t entries[count];

    if (write_shb(options->dumpfile)) {
        return;
    }
    if (write_idb(options->dumpfile)) {
        return;
    }

    for (;;) {
        uint32_t n;
        zx_status_t status;
        if ((status = zx_fifo_read_old(rx_fifo, entries, sizeof(entries), &n)) < 0) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                zx_object_wait_one(rx_fifo, ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, ZX_TIME_INFINITE, NULL);
                continue;
            }
            fprintf(stderr, "netdump: failed to read rx packets: %d\n", status);
            return;
        }

        eth_fifo_entry_t* e = entries;
        for (uint32_t i = 0; i < n; i++, e++) {
            if (e->flags & ETH_FIFO_RX_OK) {
                if (options->raw) {
                    printf("---\n");
                    hexdump8_ex(iobuf + e->offset, e->length, 0);
                } else {
                    parse_packet(iobuf + e->offset, e->length, options);
                }

                if (write_packet(options->dumpfile, iobuf + e->offset, e->length)) {
                    return;
                }

                options->packet_count--;
                if (options->packet_count == 0) {
                    return;
                }
            }

            e->length = BUFSIZE;
            e->flags = 0;
            uint32_t actual;
            if ((status = zx_fifo_write_old(rx_fifo, e, sizeof(*e), &actual)) < 0) {
                fprintf(stderr, "netdump: failed to queue rx packet: %d\n", status);
                break;
            }
        }
    }
}

int usage(void) {
    fprintf(stderr, "usage: netdump [ <option>* ] <network-device>\n");
    fprintf(stderr, " -w file : Write packet output to file in pcapng format\n");
    fprintf(stderr, " -c count: Exit after receiving count packets\n");
    fprintf(stderr, " -e      : Print link-level header information\n");
    fprintf(stderr, " -p      : Use promiscuous mode\n");
    fprintf(stderr, " -v      : Print verbose output\n");
    fprintf(stderr, " -vv     : Print extra verbose output\n");
    fprintf(stderr, " --raw   : Print raw bytes of all incoming packets\n");
    fprintf(stderr, " --help  : Show this help message\n");
    return -1;
}

int parse_args(int argc, const char** argv, netdump_options_t* options) {
    while (argc > 1) {
        if (!strncmp(argv[0], "-c", strlen("-c"))) {
            argv++;
            argc--;
            if (argc < 1) {
                return usage();
            }
            char* endptr;
            options->packet_count = strtol(argv[0], &endptr, 10);
            if (*endptr != '\0') {
                return usage();
            }
            argv++;
            argc--;
        } else if (!strcmp(argv[0], "-e")) {
            argv++;
            argc--;
            options->link_level = true;
        } else if (!strcmp(argv[0], "-p")) {
            argv++;
            argc--;
            options->promisc = true;
        } else if (!strcmp(argv[0], "-w")) {
            argv++;
            argc--;
            if (argc < 1 || options->dumpfile != -1) {
                return usage();
            }
            options->dumpfile = open(argv[0], O_WRONLY | O_CREAT);
            if (options->dumpfile < 0) {
                fprintf(stderr, "Error: Could not output to file: %s\n", argv[0]);
                return usage();
            }
            argv++;
            argc--;
        } else if (!strcmp(argv[0], "-v")) {
            argv++;
            argc--;
            options->verbose_level = 1;
        } else if (!strncmp(argv[0], "-vv", sizeof("-vv"))) {
            // Since this is the max verbosity, adding extra 'v's does nothing.
            argv++;
            argc--;
            options->verbose_level = 2;
        } else if (!strcmp(argv[0], "--raw")) {
            argv++;
            argc--;
            options->raw = true;
        } else {
            return usage();
        }
    }

    if (argc == 0) {
        return usage();
    } else if (!strcmp(argv[0], "--help")) {
        return usage();
    }

    options->device = argv[0];
    return 0;
}

int main(int argc, const char** argv) {
    netdump_options_t options;
    memset(&options, 0, sizeof(options));
    options.dumpfile = -1;
    if (parse_args(argc - 1, argv + 1, &options)) {
        return -1;
    }

    int fd;
    if ((fd = open(options.device, O_RDWR)) < 0) {
        fprintf(stderr, "netdump: cannot open '%s'\n", options.device);
        return -1;
    }

    eth_fifos_t fifos;
    zx_status_t status;

    ssize_t r;
    if ((r = ioctl_ethernet_get_fifos(fd, &fifos)) < 0) {
        fprintf(stderr, "netdump: failed to get fifos: %zd\n", r);
        return r;
    }

    unsigned count = fifos.rx_depth / 2;
    zx_handle_t iovmo;
    // allocate shareable ethernet buffer data heap
    if ((status = zx_vmo_create(count * BUFSIZE, 0, &iovmo)) < 0) {
        return -1;
    }

    char* iobuf;
    if ((status = zx_vmar_map(zx_vmar_root_self(), 0, iovmo, 0, count * BUFSIZE,
                              ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                              (uintptr_t*)&iobuf)) < 0) {
        return -1;
    }

    if ((r = ioctl_ethernet_set_iobuf(fd, &iovmo)) < 0) {
        fprintf(stderr, "netdump: failed to set iobuf: %zd\n", r);
        return -1;
    }

    if ((r = ioctl_ethernet_set_client_name(fd, "netdump", 7)) < 0) {
        fprintf(stderr, "netdump: failed to set client name %zd\n", r);
    }

    if (options.promisc) {
        bool yes = true;
        if ((r = ioctl_ethernet_set_promisc(fd, &yes)) < 0) {
            fprintf(stderr, "netdump: failed to set promisc mode: %zd\n", r);
        }
    }

    // assign data chunks to ethbufs
    for (unsigned n = 0; n < count; n++) {
        eth_fifo_entry_t entry = {
            .offset = n * BUFSIZE,
            .length = BUFSIZE,
            .flags = 0,
            .cookie = NULL,
        };
        uint32_t actual;
        if ((status = zx_fifo_write_old(fifos.rx_fifo, &entry, sizeof(entry), &actual)) < 0) {
            fprintf(stderr, "netdump: failed to queue rx packet: %d\n", status);
            return -1;
        }
    }

    if (ioctl_ethernet_start(fd) < 0) {
        fprintf(stderr, "netdump: failed to start network interface\n");
        return -1;
    }

    if (ioctl_ethernet_tx_listen_start(fd) < 0) {
        fprintf(stderr, "netdump: failed to start listening\n");
        return -1;
    }

    handle_rx(fifos.rx_fifo, iobuf, count, &options);

    zx_handle_close(fifos.rx_fifo);
    if (options.dumpfile != -1) {
        close(options.dumpfile);
    }
    return 0;
}
