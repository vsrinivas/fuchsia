// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

// Error return codes.
#define OK 0
#define ERR_SYSTEM_CANT_OPEN_SOCKET -1
#define ERR_SYSTEM_SENDTO -2
#define ERR_SYSTEM_RECVFROM -3
#define ERR_SYSTEM_POLLING -4
#define ERR_UNKNOWN_HOST -5
#define ERR_RESPONSE_VALIDATION -6
#define ERR_RESPONSE_TIMEOUT -7

#ifdef __cplusplus
extern "C" {
#endif
ssize_t c_ping(const char* url);
#ifdef __cplusplus
}
#endif

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

  explicit Options(const char* h, long min) {
    host = h;
    payload_size_bytes = min;
    min_payload_size_bytes = min;
  }
};

struct PingStatistics {
  uint64_t min_rtt_usec = UINT64_MAX;
  uint64_t max_rtt_usec = 0;
  uint64_t sum_rtt_usec = 0;
  uint16_t num_sent = 0;
  uint16_t num_lost = 0;

  void Update(uint64_t rtt_usec) {
    if (rtt_usec < min_rtt_usec) {
      min_rtt_usec = rtt_usec;
    }
    if (rtt_usec > max_rtt_usec) {
      max_rtt_usec = rtt_usec;
    }
    sum_rtt_usec += rtt_usec;
    num_sent++;
  }
};

bool ValidateReceivedPacket(const packet_t& sent_packet, size_t sent_packet_size,
                            const packet_t& received_packet, size_t received_packet_size,
                            const Options& options) {
  if (received_packet_size != sent_packet_size) {
    return false;
  }
  if (received_packet.hdr.type != ICMP_ECHOREPLY) {
    return false;
  }
  if (received_packet.hdr.code != 0) {
    return false;
  }

  // Do not match identifier.
  // RFC 792 and RFC 1122 3.2.2.6 require the echo reply carries the same value
  // from the echo request, implementations honor that. But the requester side
  // NAT may rewrite the identifier on echo request, which makes impossible
  // for the host to match.

  if (received_packet.hdr.un.echo.sequence != sent_packet.hdr.un.echo.sequence) {
    return false;
  }
  if (memcmp(received_packet.payload, sent_packet.payload, options.payload_size_bytes) != 0) {
    return false;
  }
  return true;
}

ssize_t c_ping(const char* url) {
  constexpr char ping_message[] = "This is an echo message!";
  long message_size = static_cast<long>(strlen(ping_message) + 1);
  Options options(url, message_size);
  PingStatistics stats;

  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
  if (s < 0) {
    return ERR_SYSTEM_CANT_OPEN_SOCKET;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_RAW;
  hints.ai_flags = 0;
  hints.ai_protocol = IPPROTO_ICMP;
  struct addrinfo* info;
  if (getaddrinfo(options.host, NULL, &hints, &info)) {
    return ERR_UNKNOWN_HOST;
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
    if (sendto(s, &packet, sent_packet_size, 0, saddr, sizeof(*saddr)) < 0) {
      r = ERR_SYSTEM_SENDTO;
      break;
    }

    struct pollfd fd;
    fd.fd = s;
    fd.events = POLLIN;
    switch (poll(&fd, 1, static_cast<int>(options.timeout_msec))) {
      case 1:
        if (fd.revents & POLLIN) {
          ssize_t recvd = recvfrom(s, &received_packet, sizeof(received_packet), 0, NULL, NULL);
          if (recvd < 0) {
            r = ERR_SYSTEM_RECVFROM;
          } else if (!ValidateReceivedPacket(packet, sent_packet_size, received_packet, recvd,
                                             options)) {
            r = ERR_RESPONSE_VALIDATION;
          }
          break;
        } else {
          r = ERR_SYSTEM_POLLING;
          break;
        }
      case 0:
        r = ERR_RESPONSE_TIMEOUT;
        break;
      default:
        r = ERR_SYSTEM_POLLING;
    }

    if (r < 0) {
      break;
    }

    zx_ticks_t after = zx_ticks_get();
    uint64_t usec = (after - before) / ticks_per_usec;
    stats.Update(usec);
    if (options.count > 0) {
      usleep(static_cast<unsigned int>(options.interval_msec * 1000));
    }
  }

  freeaddrinfo(info);
  close(s);

  return r;
}
