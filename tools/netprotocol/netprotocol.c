// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _GNU_SOURCE

#include "netprotocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <zircon/boot/netboot.h>

uint16_t tftp_block_size = TFTP_DEFAULT_BLOCK_SZ;
uint16_t tftp_window_size = TFTP_DEFAULT_WINDOW_SZ;

static uint32_t cookie = 0x12345678;
static int netboot_timeout = 250;
static bool netboot_wait = true;

static struct timeval netboot_timeout_init(int msec) {
  struct timeval timeout_tv;
  timeout_tv.tv_sec = msec / 1000;
  timeout_tv.tv_usec = (msec % 1000) * 1000;

  struct timeval end_tv;
  gettimeofday(&end_tv, NULL);
  timeradd(&end_tv, &timeout_tv, &end_tv);

  return end_tv;
}

static int netboot_timeout_get_msec(const struct timeval* end_tv) {
  struct timeval wait_tv;
  struct timeval now_tv;
  gettimeofday(&now_tv, NULL);
  timersub(end_tv, &now_tv, &wait_tv);
  return wait_tv.tv_sec * 1000 + wait_tv.tv_usec / 1000;
}

static int netboot_bind_to_cmd_port(int socket) {
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;

  for (uint16_t port = NB_CMD_PORT_START; port <= NB_CMD_PORT_END; port++) {
    addr.sin6_port = htons(port);
    if (bind(socket, (void*)&addr, sizeof(addr)) == 0) {
      return 0;
    }
  }
  return -1;
}

static int netboot_send_query(int socket, unsigned port, const char* ifname) {
  const char* hostname = "*";
  size_t hostname_len = strlen(hostname) + 1;

  msg m;
  m.hdr.magic = NB_MAGIC;
  m.hdr.cookie = ++cookie;
  m.hdr.cmd = NB_QUERY;
  m.hdr.arg = 0;
  memcpy(m.data, hostname, hostname_len);

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  inet_pton(AF_INET6, "ff02::1", &addr.sin6_addr);

  struct ifaddrs* ifa;
  if (getifaddrs(&ifa) < 0) {
    fprintf(stderr, "error: cannot enumerate network interfaces\n");
    return -1;
  }

  bool success = false;

  struct ifaddrs* ifa_it = ifa;
  for (; ifa_it != NULL; ifa_it = ifa_it->ifa_next) {
    if (ifa_it->ifa_addr == NULL) {
      continue;
    }
    if (ifa_it->ifa_addr->sa_family != AF_INET6) {
      continue;
    }
    struct sockaddr_in6* in6 = (void*)ifa_it->ifa_addr;
    if (in6->sin6_scope_id == 0) {
      continue;
    }
    if (ifname && ifname[0] != 0 && strcmp(ifname, ifa_it->ifa_name)) {
      continue;
    }
    // printf("tx %s (sid=%d)\n", ifa_it->ifa_name, in6->sin6_scope_id);
    size_t sz = sizeof(nbmsg) + hostname_len;
    addr.sin6_scope_id = in6->sin6_scope_id;

    ssize_t r = sendto(socket, &m, sz, 0, (struct sockaddr*)&addr, sizeof(addr));
    if ((r >= 0) && (size_t)r == sz) {
      success = true;
    }
  }

  freeifaddrs(ifa);

  if (!success) {
    fprintf(stderr, "error: failed to find interface for sending query\n");
    return -1;
  }

  return 0;
}

static bool netboot_receive_query(int socket, on_device_cb callback, void* data) {
  struct sockaddr_in6 ra;
  socklen_t rlen = sizeof(ra);
  memset(&ra, 0, sizeof(ra));
  msg m;
  ssize_t r = recvfrom(socket, &m, sizeof(m), 0, (void*)&ra, &rlen);
  if (r < 0) {
    fprintf(stderr, "error: recvfrom: %s\n", strerror(errno));
  } else if ((size_t)r > sizeof(nbmsg)) {
    r -= sizeof(nbmsg);
    m.data[r] = 0;
    if ((m.hdr.magic == NB_MAGIC) && (m.hdr.cookie == cookie) && (m.hdr.cmd == NB_ACK)) {
      char tmp[INET6_ADDRSTRLEN];
      if (inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)) == NULL) {
        strcpy(tmp, "???");
      }
      // printf("found %s at %s/%d\n", (char*)m.data, tmp, ra.sin6_scope_id);
      if (strncmp("::", tmp, 2)) {
        device_info_t info;
        strncpy(info.nodename, (char*)m.data, sizeof(info.nodename));
        strncpy(info.inet6_addr_s, tmp, INET6_ADDRSTRLEN);
        memcpy(&info.inet6_addr, &ra, sizeof(ra));
        info.state = DEVICE;
        return callback(&info, data);
      }
    }
  }
  return false;
}

static struct option default_opts[] = {
    {"help", no_argument, NULL, 'h'},
    {"timeout", required_argument, NULL, 't'},
    {"nowait", no_argument, NULL, 'n'},
    {"block-size", required_argument, NULL, 'b'},
    {"window-size", required_argument, NULL, 'w'},
    {NULL, 0, NULL, 0},
};

static const struct option netboot_zero_opt = {NULL, 0, NULL, 0};

static size_t netboot_count_opts(const struct option* opts) {
  if (!opts) {
    return 0;
  }
  size_t count = 0;
  while (memcmp(&opts[count], &netboot_zero_opt, sizeof(netboot_zero_opt))) {
    count++;
  }
  return count;
}

static void netboot_copy_opts(struct option* dst_opts, const struct option* src_opts) {
  if (!src_opts) {
    return;
  }
  size_t i;
  for (i = 0; memcmp(&src_opts[i], &netboot_zero_opt, sizeof(netboot_zero_opt)); i++) {
    dst_opts[i] = src_opts[i];
  }
}

int netboot_handle_custom_getopt(int argc, char* const* argv, const struct option* custom_opts,
                                 bool (*opt_callback)(int ch, int argc, char* const* argv)) {
  size_t num_default_opts = netboot_count_opts(default_opts);
  size_t num_custom_opts = netboot_count_opts(custom_opts);

  struct option* combined_opts;
  combined_opts =
      (struct option*)malloc(sizeof(struct option) * (num_default_opts + num_custom_opts + 1));

  netboot_copy_opts(combined_opts, default_opts);
  netboot_copy_opts(combined_opts + num_default_opts, custom_opts);
  memset(&combined_opts[num_default_opts + num_custom_opts], 0x0, sizeof(struct option));

  int retval = -1;
  int ch;
  while ((ch = getopt_long_only(argc, argv, "t:", combined_opts, NULL)) != -1) {
    switch (ch) {
      case 't':
        netboot_timeout = atoi(optarg);
        break;
      case 'n':
        netboot_wait = false;
        break;
      case 'b':
        tftp_block_size = atoi(optarg);
        break;
      case 'w':
        tftp_window_size = atoi(optarg);
        break;
      default:
        if (opt_callback && opt_callback(ch, argc, argv)) {
          break;
        } else {
          goto err;
        }
    }
  }
  retval = optind;
err:
  free(combined_opts);
  return retval;
}

int netboot_handle_getopt(int argc, char* const* argv) {
  return netboot_handle_custom_getopt(argc, argv, NULL, NULL);
}

void netboot_usage(bool show_tftp_opts) {
  fprintf(stderr, "options:\n");
  fprintf(stderr, "    --help              Print this message.\n");
  fprintf(stderr, "    --timeout=<msec>    Set discovery timeout to <msec>.\n");
  fprintf(stderr, "    --nowait            Do not wait for first packet before timing out.\n");
  if (show_tftp_opts) {
    fprintf(stderr, "    --block-size=<sz>   Set tftp block size (default=%d).\n",
            TFTP_DEFAULT_BLOCK_SZ);
    fprintf(stderr, "    --window-size=<sz>  Set tftp window size (default=%d).\n",
            TFTP_DEFAULT_WINDOW_SZ);
  }
}

int netboot_discover(unsigned port, const char* ifname, on_device_cb callback, void* data) {
  if (!callback) {
    errno = EINVAL;
    return -1;
  }

  int s;
  if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    fprintf(stderr, "error: cannot create socket: %s\n", strerror(errno));
    return -1;
  }

  if (netboot_bind_to_cmd_port(s) < 0) {
    fprintf(stderr, "error: cannot bind to command port: %s\n", strerror(errno));
    close(s);
    return -1;
  }

  if (netboot_send_query(s, port, ifname) < 0) {
    fprintf(stderr, "error: failed to send netboot query\n");
    close(s);
    return -1;
  }

  struct pollfd fds;
  fds.fd = s;
  fds.events = POLLIN;
  bool received_packets = false;
  bool first_wait = netboot_wait;

#if defined(__APPLE__)
  // macOS development hosts often have a firewall that prompts the user with a dialog box asking if
  // a conection should be allowed. On macOS, use a long timeout for the first wait to ensure the
  // user has a chance to read the dialog and respond. See also bug fxbug.dev/42296.
  //
  // TODO(maniscalco): Once macOS hosts are no longer supported for bringup development we can
  // remove this special case and the first_wait concept.
  struct timeval end_tv = netboot_timeout_init(first_wait ? 3600000 : netboot_timeout);
#else
  struct timeval end_tv = netboot_timeout_init(netboot_timeout);
#endif

  for (;;) {
    int wait_ms = netboot_timeout_get_msec(&end_tv);
    if (wait_ms < 0) {
      // Expired.
      break;
    }

    int r = poll(&fds, 1, wait_ms);
    if (r > 0 && (fds.revents & POLLIN)) {
      received_packets = true;
      if (!netboot_receive_query(s, callback, data)) {
        break;
      }
    } else if (r < 0 && errno != EAGAIN && errno != EINTR) {
      fprintf(stderr, "poll returned error: %s\n", strerror(errno));
      close(s);
      return -1;
    }
    if (first_wait) {
      end_tv = netboot_timeout_init(netboot_timeout);
      first_wait = 0;
    }
  }

  close(s);
  if (received_packets) {
    return 0;
  } else {
    errno = ETIMEDOUT;
    return -1;
  }
}

typedef struct netboot_open_cookie {
  struct sockaddr_in6 addr;
  const char* hostname;
  uint32_t index;
} netboot_open_cookie_t;

static bool netboot_open_callback(device_info_t* device, void* data) {
  netboot_open_cookie_t* cookie = data;
  cookie->index++;
  if (strcmp(cookie->hostname, "*") && strcmp(cookie->hostname, device->nodename)) {
    return true;
  }
  memcpy(&cookie->addr, &device->inet6_addr, sizeof(device->inet6_addr));
  return false;
}

int netboot_open(const char* hostname, const char* ifname, struct sockaddr_in6* addr,
                 bool make_connection) {
  if ((hostname == NULL) || (hostname[0] == 0)) {
    char* envname = getenv("ZIRCON_NODENAME");
    hostname = envname && envname[0] != 0 ? envname : "*";
  }
  size_t hostname_len = strlen(hostname) + 1;
  if (hostname_len > MAXSIZE) {
    errno = EINVAL;
    return -1;
  }

  netboot_open_cookie_t cookie;
  socklen_t rlen = sizeof(cookie.addr);
  memset(&(cookie.addr), 0, sizeof(cookie.addr));
  cookie.index = 0;
  cookie.hostname = hostname;
  if (netboot_discover(NB_SERVER_PORT, ifname, netboot_open_callback, &cookie) < 0) {
    return -1;
  }
  // Device not found
  if (cookie.index == 0) {
    errno = EINVAL;
    return -1;
  }

  int s;
  if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    fprintf(stderr, "error: cannot create socket: %s\n", strerror(errno));
    return -1;
  }

  if (netboot_bind_to_cmd_port(s) < 0) {
    fprintf(stderr, "cannot bind to command port: %s\n", strerror(errno));
    return -1;
  }

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 250 * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (addr) {
    memcpy(addr, &cookie.addr, sizeof(cookie.addr));
  }

  if (make_connection && connect(s, (void*)&cookie.addr, rlen) < 0) {
    fprintf(stderr, "error: cannot connect UDP port\n");
    close(s);
    return -1;
  }
  return s;
}
