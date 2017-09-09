// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _GNU_SOURCE

#include "netprotocol.h"

#include <magenta/boot/netboot.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

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

static int netboot_timeout_get_msec(const struct timeval *end_tv) {
    struct timeval wait_tv;
    struct timeval now_tv;
    gettimeofday(&now_tv, NULL);
    timersub(end_tv, &now_tv, &wait_tv);
    return wait_tv.tv_sec * 1000 + wait_tv.tv_usec / 1000;
}

static bool netboot_timer_is_expired(const struct timeval *end_tv) {
    struct timeval now_tv;
    gettimeofday(&now_tv, NULL);

    return timercmp(&now_tv, end_tv, >=);
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

static int netboot_send_query(int socket, unsigned port, const char *ifname) {
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

    for (; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_INET6) {
            continue;
        }
        struct sockaddr_in6* in6 = (void*)ifa->ifa_addr;
        if (in6->sin6_scope_id == 0) {
            continue;
        }
        if (ifname && ifname[0] != 0 && strcmp(ifname, ifa->ifa_name))
            continue;
        // printf("tx %s (sid=%d)\n", ifa->ifa_name, in6->sin6_scope_id);
        size_t sz = sizeof(nbmsg) + hostname_len;
        addr.sin6_scope_id = in6->sin6_scope_id;

        int r;
        if ((r = sendto(socket, &m, sz, 0, (struct sockaddr*)&addr, sizeof(addr))) != sz) {
            fprintf(stderr, "error: cannot send %d %s\n", errno, strerror(errno));
        }
    }

    return 0;
}

static bool netboot_receive_query(int socket, on_device_cb callback, void* data) {
    struct sockaddr_in6 ra;
    socklen_t rlen = sizeof(ra);
    memset(&ra, 0, sizeof(ra));
    msg m;
    int r = recvfrom(socket, &m, sizeof(m), 0, (void*)&ra, &rlen);
    if (r > sizeof(nbmsg)) {
        r -= sizeof(nbmsg);
        m.data[r] = 0;
        if ((m.hdr.magic == NB_MAGIC) &&
            (m.hdr.cookie == cookie) &&
            (m.hdr.cmd == NB_ACK)) {
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
    {"help",    no_argument,       NULL, 'h'},
    {"timeout", required_argument, NULL, 't'},
    {"nowait",  no_argument,       NULL, 'n'},
    {NULL,      0,                 NULL, 0},
};

static const struct option netboot_zero_opt = {NULL, 0, NULL, 0};

static size_t netboot_count_opts(const struct option *opts) {
    if (!opts) {
        return 0;
    }
    size_t count = 0;
    while (memcmp(&opts[count], &netboot_zero_opt, sizeof(netboot_zero_opt))) {
        count++;
    }
    return count;
}

static void netboot_copy_opts(struct option *dst_opts, const struct option *src_opts) {
    if (!src_opts) {
        return;
    }
    size_t i;
    for (i = 0; memcmp(&src_opts[i], &netboot_zero_opt, sizeof(netboot_zero_opt)); i++) {
        dst_opts[i] = src_opts[i];
    }
}

int netboot_handle_custom_getopt(int argc, char * const *argv,
                                 const struct option *custom_opts,
                                 size_t num_custom_opts0,
                                 bool (*opt_callback)(int ch, int argc, char * const *argv)) {
    size_t num_default_opts = netboot_count_opts(default_opts);
    size_t num_custom_opts = netboot_count_opts(custom_opts);

    struct option *combined_opts;
    combined_opts = (struct option*)malloc(sizeof(struct option) *
                                           (num_default_opts + num_custom_opts +1));

    netboot_copy_opts(combined_opts, default_opts);
    netboot_copy_opts(combined_opts + num_default_opts, custom_opts);
    memset(&combined_opts[num_default_opts + num_custom_opts], 0x0,
           sizeof(struct option));

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

int netboot_handle_getopt(int argc, char * const *argv) {
    return netboot_handle_custom_getopt(argc, argv, NULL, 0, NULL);
}

void netboot_usage(void) {
    fprintf(stderr, "options:\n");
    fprintf(stderr, "    --help            Print this message.\n");
    fprintf(stderr, "    --timeout=<msec>  Set discovery timeout to <msec>.\n");
    fprintf(stderr, "    --nowait          Do not wait for first packet before timing out.\n");
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
        fprintf(stderr, "cannot bind to command port: %s\n", strerror(errno));
        return -1;
    }

    netboot_send_query(s, port, ifname);

    struct pollfd fds;
    fds.fd = s;
    fds.events = POLLIN;
    bool received_packets = false;
    bool first_wait = netboot_wait;

    struct timeval end_tv = netboot_timeout_init(first_wait ? 3600000 : netboot_timeout);
    do {

        int wait_ms = netboot_timeout_get_msec(&end_tv);

        int r = poll(&fds, 1, wait_ms);
        if (r > 0 && (fds.revents & POLLIN)) {
            received_packets = true;
            if (!netboot_receive_query(s, callback, data)) {
                break;
            }
        } else if (r < 0 && errno != EAGAIN && errno != EINTR) {
            fprintf(stderr, "poll returned error: %s\n", strerror(errno));
            return -1;
        }
        if (first_wait) {
            end_tv = netboot_timeout_init(netboot_timeout);
            first_wait = 0;
        }
    } while (!netboot_timer_is_expired(&end_tv));

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
    const char *hostname;
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

int netboot_open(const char* hostname, const char* ifname,
                 struct sockaddr_in6* addr, bool make_connection) {
    if ((hostname == NULL) || (hostname[0] == 0)) {
        char* envname = getenv("MAGENTA_NODENAME");
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

// The netboot protocol ignores response packets that are invalid,
// retransmits requests if responses don't arrive in a timely
// fashion, and only returns an error upon eventual timeout or
// a specific (correctly formed) remote error packet.
int netboot_txn(int s, msg* in, msg* out, int outlen) {
    ssize_t r;

    out->hdr.magic = NB_MAGIC;
    out->hdr.cookie = ++cookie;

    int retry = 5;
resend:
    write(s, out, outlen);
    for (;;) {
        if ((r = recv(s, in, sizeof(*in), 0)) < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                if (retry-- > 0) {
                    goto resend;
                }
                errno = ETIMEDOUT;
            }
            return -1;
        }
        if (r < (ssize_t)sizeof(in->hdr)) {
            fprintf(stderr, "netboot: response too short\n");
            continue;
        }
        if ((in->hdr.magic != NB_MAGIC) ||
            (in->hdr.cookie != out->hdr.cookie) ||
            (in->hdr.cmd != NB_ACK)) {
            fprintf(stderr, "netboot: bad ack header"
                            " (magic=0x%x, cookie=%x/%x, cmd=%d)\n",
                    in->hdr.magic, in->hdr.cookie, cookie, in->hdr.cmd);
            continue;
        }
        int arg = in->hdr.arg;
        if (arg < 0) {
            errno = -arg;
            return -1;
        }
        return r;
    }
}
