// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/syscalls.h>

const int MAX_PAYLOAD_SIZE_BYTES = 1400;

typedef struct {
    icmphdr hdr;
    uint8_t payload[MAX_PAYLOAD_SIZE_BYTES];
} __PACKED packet_t;

struct Options {
    long interval_msec = 1000;
    long payload_size_bytes = 0;
    long count = 3;
    long timeout_msec = 1000;
    const char* host = nullptr;
    long min_payload_size_bytes = 0;

    explicit Options(long min) {
        payload_size_bytes = min;
        min_payload_size_bytes = min;
    }

    void Print() const {
        printf("Count: %ld, ", count);
        printf("Payload size: %ld bytes, ", payload_size_bytes);
        printf("Interval: %ld ms, ", interval_msec);
        printf("Timeout: %ld ms, ", timeout_msec);
        if (host != nullptr) {
            printf("Destination: %s\n", host);
        }
    }

    bool Validate() const {
        if (interval_msec <= 0) {
            fprintf(stderr, "interval must be positive: %ld\n", interval_msec);
            return false;
        }

        if (payload_size_bytes >= MAX_PAYLOAD_SIZE_BYTES) {
            fprintf(stderr, "payload size must be smaller than: %d\n", MAX_PAYLOAD_SIZE_BYTES);
            return false;
        }

        if (payload_size_bytes < min_payload_size_bytes) {
            fprintf(stderr, "payload size must be more than: %ld\n", min_payload_size_bytes);
            return false;
        }

        if (count <= 0) {
            fprintf(stderr, "count must be positive: %ld\n", count);
            return false;
        }

        if (timeout_msec <= 0) {
            fprintf(stderr, "timeout must be positive: %ld\n", timeout_msec);
            return false;
        }

        if (host == nullptr) {
            fprintf(stderr, "destination must be provided\n");
            return false;
        }
        return true;
    }

    int Usage() const {
        fprintf(stderr, "\n\tUsage: ping [ <option>* ] destination\n");
        fprintf(stderr, "\n\tSend ICMP ECHO_REQUEST to a destination. This destination\n");
        fprintf(stderr, "\tmay be a hostname (google.com) or an IP address (8.8.8.8).\n\n");
        fprintf(stderr, "\t-c count: Only send count packets (default = 3)\n");
        fprintf(stderr, "\t-i interval(ms): Time interval between pings (default = 1000)\n");
        fprintf(stderr, "\t-t timeout(ms): Timeout waiting for ping response (default = 1000)\n");
        fprintf(stderr, "\t-s size(bytes): Number of payload bytes (default = %ld, max 1400)\n",
                payload_size_bytes);
        fprintf(stderr, "\t-h: View this help message\n\n");
        return -1;
    }

    int ParseCommandLine(int argc, char** argv) {
        int opt;
        while ((opt = getopt(argc, argv, "s:c:i:t:h")) != -1) {
            char* endptr = nullptr;
            switch (opt) {
            case 'h':
                return Usage();
            case 'i':
                interval_msec = strtol(optarg, &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "-i must be followed by a non-negative integer\n");
                    return Usage();
                }
                break;
            case 's':
                payload_size_bytes = strtol(optarg, &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "-s must be followed by a non-negative integer\n");
                    return Usage();
                }
                break;
            case 'c':
                count = strtol(optarg, &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "-c must be followed by a non-negative integer\n");
                    return Usage();
                }
                break;
            case 't':
                timeout_msec = strtol(optarg, &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "-t must be followed by a non-negative integer\n");
                    return Usage();
                }
                break;
            default:
                return Usage();
            }
        }
        if (optind >= argc) {
            fprintf(stderr, "missing destination\n");
            return Usage();
        }
        host = argv[optind];
        return 0;
    }
};

struct PingStatistics {
    uint64_t min_rtt_msec = UINT64_MAX;
    uint64_t max_rtt_msec = 0;
    uint64_t sum_rtt_msec = 0;
    uint16_t num_sent = 0;
    uint16_t num_lost = 0;

    void Update(uint64_t rtt_msec) {
        if (rtt_msec < min_rtt_msec)
            min_rtt_msec = rtt_msec;
        if (rtt_msec > max_rtt_msec)
            max_rtt_msec = rtt_msec;
        sum_rtt_msec += rtt_msec;
        num_sent++;
    }

    void Print() const {
        if (num_sent == 0)
            return;
        printf("Min RTT: %" PRIu64 " us, Max RTT: %" PRIu64 " us, Avg RTT: %" PRIu64 " us\n",
               min_rtt_msec, max_rtt_msec, (sum_rtt_msec / num_sent));
    }
};

bool ValidateReceivedPacket(const packet_t& sent_packet, size_t sent_packet_size,
                            const packet_t& received_packet, size_t received_packet_size,
                            const Options& options) {
    if (received_packet_size != sent_packet_size) {
        fprintf(stderr, "Incorrect Packet size of received packet: %zu expected %zu\n",
                received_packet_size, sent_packet_size);
        return false;
    }
    if (received_packet.hdr.type != ICMP_ECHOREPLY) {
        fprintf(stderr, "Incorrect Header type in received packet: %d expected: %d\n",
                received_packet.hdr.type, ICMP_ECHOREPLY);
        return false;
    }
    if (received_packet.hdr.code != 0) {
        fprintf(stderr, "Incorrect Header code in received packet: %d expected: 0\n",
                received_packet.hdr.code);
        return false;
    }
    if (received_packet.hdr.un.echo.sequence != sent_packet.hdr.un.echo.sequence) {
        fprintf(stderr, "Incorrect Header sequence in received packet: %d expected: %d\n",
                received_packet.hdr.un.echo.sequence, sent_packet.hdr.un.echo.sequence);
        return false;
    }
    if (memcmp(received_packet.payload, sent_packet.payload, options.payload_size_bytes) != 0) {
        fprintf(stderr, "Incorrect Payload content in received packet\n");
        return false;
    }
    return true;
}

int main(int argc, char** argv) {

    constexpr char ping_message[] = "This is an echo message!";
    long message_size = static_cast<long>(strlen(ping_message) + 1);
    Options options(message_size);
    PingStatistics stats;

    if (options.ParseCommandLine(argc, argv) != 0) {
        return -1;
    }

    if (!options.Validate()) {
        return options.Usage();
    }

    options.Print();

    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (s < 0) {
        fprintf(stderr, "Could not acquire ICMP socket: %d\n", errno);
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = IPPROTO_ICMP;
    struct addrinfo* info;
    if (getaddrinfo(options.host, NULL, &hints, &info)) {
        fprintf(stderr, "ping: unknown host %s\n", options.host);
        return -1;
    }

    struct sockaddr* saddr = info->ai_addr;
    char buf[256];
    if (saddr->sa_family == AF_INET) {
        struct sockaddr_in* iaddr = reinterpret_cast<struct sockaddr_in*>(saddr);
        inet_ntop(saddr->sa_family, &iaddr->sin_addr, buf, sizeof(buf));
    } else {
        struct sockaddr_in6* iaddr = reinterpret_cast<struct sockaddr_in6*>(saddr);
        inet_ntop(saddr->sa_family, &iaddr->sin6_addr, buf, sizeof(buf));
    }

    printf("PING %s (%s)\n", options.host, buf);

    uint16_t sequence = 1;
    packet_t packet, received_packet;
    ssize_t r = 0;
    ssize_t sent_packet_size = 0;
    const zx_ticks_t ticks_per_usec = zx_ticks_per_second() / 1000000;

    while (options.count-- > 0) {
        memset(&packet, 0, sizeof(packet));
        packet.hdr.type = ICMP_ECHO;
        packet.hdr.code = 0;
        packet.hdr.un.echo.id = 0;
        packet.hdr.un.echo.sequence = htons(sequence++);
        strcpy(reinterpret_cast<char*>(packet.payload), ping_message);
        // Netstack will overwrite the checksum
        zx_ticks_t before = zx_ticks_get();
        sent_packet_size = sizeof(packet.hdr) + options.payload_size_bytes;
        r = sendto(s, &packet, sent_packet_size, 0, saddr, sizeof(*saddr));
        if (r < 0) {
            fprintf(stderr, "ping: Could not send packet\n");
            return -1;
        }

        struct pollfd fd;
        fd.fd = s;
        fd.events = POLLIN;
        switch (poll(&fd, 1, static_cast<int>(options.timeout_msec))) {
        case 1:
            if (fd.revents & POLLIN) {
                r = recvfrom(s, &received_packet, sizeof(received_packet), 0, NULL, NULL);
                if (!ValidateReceivedPacket(packet, sent_packet_size, received_packet, r, options)) {
                    fprintf(stderr, "ping: Received packet didn't match sent packet: %d\n",
                            packet.hdr.un.echo.sequence);
                }
                break;
            } else {
                fprintf(stderr, "ping: Spurious wakeup from poll\n");
                r = -1;
                break;
            }
        case 0:
            fprintf(stderr, "ping: Timeout after %d ms\n", static_cast<int>(options.timeout_msec));
            __FALLTHROUGH;
        default:
            r = -1;
        }

        if (r < 0) {
            fprintf(stderr, "ping: Could not read result of ping\n");
            return -1;
        }
        zx_ticks_t after = zx_ticks_get();
        int seq = ntohs(packet.hdr.un.echo.sequence);
        uint64_t usec = (after - before) / ticks_per_usec;
        stats.Update(usec);
        printf("%" PRIu64 " bytes: icmp_seq=%d RTT=%" PRIu64 " us\n", r, seq, usec);
        if (options.count > 0) {
            usleep(static_cast<unsigned int>(options.interval_msec * 1000));
        }
    }
    freeaddrinfo(info);
    stats.Print();
    close(s);
    return 0;
}
