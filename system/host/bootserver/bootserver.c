// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <errno.h>
#include <stdint.h>

#include <magenta/boot/netboot.h>

#define DEFAULT_US_BETWEEN_PACKETS 20

static uint32_t cookie = 1;
static char* appname;
static struct in6_addr allowed_addr;
static const char spinner[] = {'|', '/', '-', '\\'};
static const int MAX_READ_RETRIES = 10;
static const int MAX_SEND_RETRIES = 10000;
static int64_t us_between_packets = DEFAULT_US_BETWEEN_PACKETS;

static int io_rcv(int s, nbmsg* msg, nbmsg* ack) {
    for (int i = 0; i < MAX_READ_RETRIES; i++) {
        bool retry_allowed = i + 1 < MAX_READ_RETRIES;

        int r = read(s, ack, 2048);
        if (r < 0) {
            if (retry_allowed && errno == EAGAIN) {
                continue;
            }
            fprintf(stderr, "\n%s: error: Socket read error %d\n", appname, errno);
            return -1;
        }
        if (r < sizeof(nbmsg)) {
            fprintf(stderr, "\n%s: error: Read too short\n", appname);
            return -1;
        }
#ifdef DEBUG
        fprintf(stdout, " < magic = %08x, cookie = %08x, cmd = %08x, arg = %08x\n",
                ack->magic, ack->cookie, ack->cmd, ack->arg);
#endif

        if (ack->magic != NB_MAGIC) {
            fprintf(stderr, "\n%s: error: Bad magic\n", appname);
            return 0;
        }
        if (msg) {
            if (ack->cookie > msg->cookie) {
                fprintf(stderr, "\n%s: error: Bad cookie\n", appname);
                return 0;
            }
        }

        if (ack->cmd == NB_ACK || ack->cmd == NB_FILE_RECEIVED) {
            if (msg && ack->arg > msg->arg) {
                fprintf(stderr, "\n%s: error: Argument mismatch\n", appname);
                return 0;
            }
            return 0;
        }

        switch (ack->cmd) {
        case NB_ERROR:
            fprintf(stderr, "\n%s: error: Generic error\n", appname);
            break;
        case NB_ERROR_BAD_CMD:
            fprintf(stderr, "\n%s: error: Bad command\n", appname);
            break;
        case NB_ERROR_BAD_PARAM:
            fprintf(stderr, "\n%s: error: Bad parameter\n", appname);
            break;
        case NB_ERROR_TOO_LARGE:
            fprintf(stderr, "\n%s: error: File too large\n", appname);
            break;
        case NB_ERROR_BAD_FILE:
            fprintf(stderr, "\n%s: error: Bad file\n", appname);
            break;
        default:
            fprintf(stderr, "\n%s: error: Unknown command 0x%08X\n", appname, ack->cmd);
        }
        return -1;
    }
    fprintf(stderr, "\n%s: error: Unexpected code path\n", appname);
    return -1;
}

static int io_send(int s, nbmsg* msg, size_t len) {
    for (int i = 0; i < MAX_SEND_RETRIES; i++) {
#if defined(__APPLE__)
        bool retry_allowed = i + 1 < MAX_SEND_RETRIES;
#endif

        int r = write(s, msg, len);
        if (r < 0) {
#if defined(__APPLE__)
            if (retry_allowed && errno == ENOBUFS) {
                // On Darwin we manage to overflow the ethernet driver, so retry
                struct timespec reqtime;
                reqtime.tv_sec = 0;
                reqtime.tv_nsec = 50 * 1000;
                nanosleep(&reqtime, NULL);
                continue;
            }
#endif
            fprintf(stderr, "\n%s: error: Socket write error %d\n", appname, errno);
            return -1;
        }
        return 0;
    }
    fprintf(stderr, "\n%s: error: Unexpected code path\n", appname);
    return -1;
}

static int io(int s, nbmsg* msg, size_t len, nbmsg* ack, bool wait_reply) {
    int r, n;
    struct timeval tv;
    fd_set reads, writes;
    fd_set* ws = NULL;
    fd_set* rs = NULL;

    ack->cookie = 0;
    ack->cmd = 0;
    ack->arg = 0;

    FD_ZERO(&reads);
    if (!wait_reply) {
        FD_SET(s, &reads);
        rs = &reads;
    }

    FD_ZERO(&writes);
    if (msg && len > 0) {
        msg->magic = NB_MAGIC;
        msg->cookie = cookie++;

        FD_SET(s, &writes);
        ws = &writes;
    }

    if (rs || ws) {
        n = s + 1;
        tv.tv_sec = 10;
        tv.tv_usec = 500000;
        int rv = select(n, rs, ws, NULL, &tv);
        if (rv == -1) {
            fprintf(stderr, "\n%s: error: Select failed %d\n", appname, errno);
            return -1;
        } else if (rv == 0) {
            // Timed-out
            fprintf(stderr, "\n%s: error: Select timed out\n", appname);
            return -1;
        } else {
            if (FD_ISSET(s, &reads)) {
                r = io_rcv(s, msg, ack);
            }

            if (FD_ISSET(s, &writes)) {
                r = io_send(s, msg, len);
            }

            if (!wait_reply) {
                return r;
            }
        }
    } else if (!wait_reply) { // no-op
        return 0;
    }

    if (wait_reply) {
        return io_rcv(s, msg, ack);
    }
    fprintf(stderr, "\n%s: error: Select triggered without events\n", appname);
    return -1;
}

typedef struct {
    FILE* fp;
    const char* data;
    size_t datalen;
} xferdata;

static ssize_t xread(xferdata* xd, void* data, size_t len) {
    if (xd->fp == NULL) {
        if (len > xd->datalen) {
            len = xd->datalen;
        }
        memcpy(data, xd->data, len);
        xd->datalen -= len;
        xd->data += len;
        return len;
    } else {
        ssize_t r = fread(data, 1, len, xd->fp);
        if (r == 0) {
            return ferror(xd->fp) ? -1 : 0;
        }
        return r;
    }
}

// UDP6_MAX_PAYLOAD (ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN - UDP_HDR_LEN)
//      1452           1514   -     14      -     40      -    8
// nbfile is PAYLOAD_SIZE + 2 * sizeof(size_t)

// Some EFI network stacks have problems with larger packets
// 1280 is friendlier
#define PAYLOAD_SIZE 1280

static int xfer(struct sockaddr_in6* addr, const char* fn, const char* name, bool boot) {
    xferdata xd;
    char msgbuf[2048];
    char ackbuf[2048];
    char tmp[INET6_ADDRSTRLEN];
    struct timeval tv;
    struct timeval begin, end;
    nbmsg* msg = (void*)msgbuf;
    nbmsg* ack = (void*)ackbuf;
    int s, r;
    int count = 0, spin = 0;
    int status = -1;
    size_t current_pos = 0;

    // This only works on POSIX systems
    bool is_redirected = !isatty(fileno(stdout));

    if (!strcmp(fn, "(cmdline)")) {
        xd.fp = NULL;
        xd.data = name;
        xd.datalen = strlen(name) + 1;
        name = "cmdline";
    } else if ((xd.fp = fopen(fn, "rb")) == NULL) {
        fprintf(stderr, "%s: error: Could not open file %s\n", appname, fn);
        return -1;
    }

    long sz = 0;
    if (xd.fp) {
        if (fseek(xd.fp, 0L, SEEK_END)) {
            fprintf(stderr, "%s: error: Could not determine size of %s\n", appname, fn);
        } else if ((sz = ftell(xd.fp)) < 0) {
            fprintf(stderr, "%s: error: Could not determine size of %s\n", appname, fn);
            sz = 0;
        } else if (fseek(xd.fp, 0L, SEEK_SET)) {
            fprintf(stderr, "%s: error: Failed to rewind %s\n", appname, fn);
            return -1;
        }
    }

    if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr, "%s: error: Cannot create socket %d\n", appname, errno);
        goto done;
    }
    fprintf(stderr, "%s: sending '%s'...\n", appname, fn);
    gettimeofday(&begin, NULL);
    tv.tv_sec = 0;
    tv.tv_usec = 250 * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(s, (void*)addr, sizeof(*addr)) < 0) {
        fprintf(stderr, "%s: error: Cannot connect to [%s]%d\n", appname,
                inet_ntop(AF_INET6, &addr->sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr->sin6_port));
        goto done;
    }

    msg->cmd = NB_SEND_FILE;
    msg->arg = sz;
    strcpy((void*)msg->data, name);
    if (io(s, msg, sizeof(nbmsg) + strlen(name) + 1, ack, true)) {
        fprintf(stderr, "%s: error: Failed to start transfer\n", appname);
        goto done;
    }

    msg->cmd = NB_DATA;
    msg->arg = 0;

    bool completed = false;
    do {
        struct timeval packet_start_time;
        gettimeofday(&packet_start_time, NULL);

        r = xread(&xd, msg->data, PAYLOAD_SIZE);
        if (r < 0) {
            fprintf(stderr, "\n%s: error: Reading '%s'\n", appname, fn);
            goto done;
        }

        if (is_redirected) {
            if (count++ > 8 * 1024) {
                fprintf(stderr, "%.01f%%\n", 100.0 * (float)msg->arg / (float)sz);
                count = 0;
            }
        } else {
            if (count++ > 1024 || r == 0) {
                count = 0;
                float bw = 0;

                struct timeval now;
                gettimeofday(&now, NULL);
                int64_t us_since_begin = ((int64_t)(now.tv_sec - begin.tv_sec) * 1000000 + ((int64_t)now.tv_usec - (int64_t)begin.tv_usec));
                if (us_since_begin >= 1000000) {
                    bw = (float)current_pos / (1024.0 * 1024.0 * (float)(us_since_begin / 1000000));
                }

                fprintf(stderr, "\33[2K\r");
                if (sz > 0) {
                    fprintf(stderr, "%c %.01f%%", spinner[(spin++) % 4], 100.0 * (float)current_pos / (float)sz);
                } else {
                    fprintf(stderr, "%c", spinner[(spin++) % 4]);
                }
                if (bw > 0.1) {
                    fprintf(stderr, " %.01fMB/s", bw);
                }
            }
        }

        if (r == 0) {
            fprintf(stderr, "\n%s: Reached end of file, waiting for confirmation.\n", appname);
            // Do not send anything, but keep waiting on incoming messages
            if (io(s, NULL, 0, ack, true)) {
                goto done;
            }
        } else {
            if (current_pos + r >= sz) {
                msg->cmd = NB_LAST_DATA;
            } else {
                msg->cmd = NB_DATA;
            }

            if (io(s, msg, sizeof(nbmsg) + r, ack, false)) {
                goto done;
            }

            // Some UEFI netstacks can lose back-to-back packets at max speed
            // so throttle output.
            // At 1280 bytes per packet, we should at least have 10 microseconds
            // between packets, to be safe using 20 microseconds here.
            // 1280 bytes * (1,000,000/10) seconds = 128,000,000 bytes/seconds = 122MB/s = 976Mb/s
            // We wait as a busy wait as the context switching a sleep can cause
            // will often degrade performance significantly.
            int64_t us_since_last_packet;
            do {
                struct timeval now;
                gettimeofday(&now, NULL);
                us_since_last_packet = (int64_t)(now.tv_sec - packet_start_time.tv_sec) * 1000000 + ((int64_t)now.tv_usec - (int64_t)packet_start_time.tv_usec);
            } while (us_since_last_packet < us_between_packets);
        }

        // ACKs really are NACKs
        if (ack->cookie > 0 && ack->cmd == NB_ACK && ack->arg != current_pos) {
            fprintf(stderr, "\n%s: need to rewind to %d from %zu\n", appname, ack->arg, current_pos);
            current_pos = ack->arg;
            if (fseek(xd.fp, current_pos, SEEK_SET)) {
                fprintf(stderr, "\n%s: error: Failed to rewind '%s' to %zu\n", appname, fn, current_pos);
                goto done;
            }
        } else if (ack->cmd == NB_FILE_RECEIVED) {
            completed = true;
        } else {
            current_pos += r;
        }

        msg->arg = current_pos;
    } while (!completed);

    status = 0;

    if (boot) {
        msg->cmd = NB_BOOT;
        msg->arg = 0;
        if (io(s, msg, sizeof(nbmsg), ack, true)) {
            fprintf(stderr, "\n%s: error: Failed to send boot command\n", appname);
        } else {
            fprintf(stderr, "\n%s: sent boot command\n", appname);
        }
    } else {
        fprintf(stderr, "\n");
    }
done:
    gettimeofday(&end, NULL);
    if (end.tv_usec < begin.tv_usec) {
        end.tv_sec -= 1;
        end.tv_usec += 1000000;
    }
    fprintf(stderr, "%s: %s %ldMB %d.%06d sec\n\n", appname,
            fn, current_pos / (1024 * 1024), (int)(end.tv_sec - begin.tv_sec),
            (int)(end.tv_usec - begin.tv_usec));
    if (s >= 0) {
        close(s);
    }
    if (xd.fp != NULL) {
        fclose(xd.fp);
    }
    return status;
}

void usage(void) {
    fprintf(stderr,
            "usage:   %s [ <option> ]* <kernel> [ <ramdisk> ] [ -- [ <kerneloption> ]* ]\n"
            "\n"
            "options:\n"
            "  -1      only boot once, then exit\n"
            "  -a      only boot device with this IPv6 address\n"
            "  -i <NN> number of microseconds between packets\n"
            "          set between 50-500 to deal with poor bootloader network stacks (default=%d)\n"
            "  -n      only boot device with this nodename\n",
            appname, DEFAULT_US_BETWEEN_PACKETS);
    exit(1);
}

void drain(int fd) {
    char buf[4096];
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
        while (read(fd, buf, sizeof(buf)) > 0)
            ;
        fcntl(fd, F_SETFL, 0);
    }
}

int main(int argc, char** argv) {
    struct sockaddr_in6 addr;
    char tmp[INET6_ADDRSTRLEN];
    char cmdline[4096];
    char* cmdnext = cmdline;
    char* nodename = NULL;
    int r, s, n = 1;
    const char* kernel_fn = NULL;
    const char* ramdisk_fn = NULL;
    int once = 0;
    int status;

    cmdline[0] = 0;
    if ((appname = strrchr(argv[0], '/')) != NULL) {
        appname++;
    } else {
        appname = argv[0];
    }

    while (argc > 1) {
        if (argv[1][0] != '-') {
            if (kernel_fn == NULL) {
                kernel_fn = argv[1];
            } else if (ramdisk_fn == NULL) {
                ramdisk_fn = argv[1];
            } else {
                usage();
            }
        } else if (!strcmp(argv[1], "-1")) {
            once = 1;
        } else if (!strcmp(argv[1], "-i")) {
            if (argc <= 1) {
                fprintf(stderr, "'-i' option requires an argument (micros between packets)\n");
                return -1;
            }
            errno = 0;
            us_between_packets = strtoll(argv[2], NULL, 10);
            if (errno != 0 || us_between_packets <= 0) {
                fprintf(stderr, "invalid arg for -i: %s\n", argv[2]);
                return -1;
            }
            fprintf(stderr, "packet spacing set to %" PRId64 " microseconds\n", us_between_packets);
            argc--;
            argv++;
        } else if (!strcmp(argv[1], "-a")) {
            if (argc <= 1) {
                fprintf(stderr, "'-a' option requires a valid ipv6 address\n");
                return -1;
            }
            if (inet_pton(AF_INET6, argv[2], &allowed_addr) != 1) {
                fprintf(stderr, "%s: invalid ipv6 address specified\n", argv[2]);
                return -1;
            }
            argc--;
            argv++;
        } else if (!strcmp(argv[1], "-n")) {
            if (argc <= 1) {
                fprintf(stderr, "'-n' option requires a valid nodename\n");
                return -1;
            }
            nodename = argv[2];
            argc--;
            argv++;
        } else if (!strcmp(argv[1], "--")) {
            while (argc > 2) {
                size_t len = strlen(argv[2]);
                if (len > (sizeof(cmdline) - 2 - (cmdnext - cmdline))) {
                    fprintf(stderr, "%s: commandline too large\n", appname);
                    return -1;
                }
                if (cmdnext != cmdline) {
                    *cmdnext++ = ' ';
                }
                memcpy(cmdnext, argv[2], len + 1);
                cmdnext += len;
                argc--;
                argv++;
            }
            break;
        } else {
            usage();
        }
        argc--;
        argv++;
    }
    if (kernel_fn == NULL) {
        usage();
    }
    if (!nodename) {
        nodename = getenv("MAGENTA_NODENAME");
    }
    if (nodename) {
        fprintf(stderr, "%s: Will only boot nodename '%s'\n", appname, nodename);
    }

    // compute the default ramdisk fn to use if
    // ramdisk is not specified and such a ramdisk
    // file actually exists
    char* auto_ramdisk_fn = NULL;
    if (ramdisk_fn == NULL) {
        char* bootdata_fn = "bootdata.bin";
        char *end = strrchr(kernel_fn, '/');
        if (end == NULL) {
            auto_ramdisk_fn = bootdata_fn;
        } else {
            size_t prefix_len = (end - kernel_fn) + 1;
            size_t len = prefix_len + strlen(bootdata_fn) + 1;
            if ((auto_ramdisk_fn = malloc(len)) != NULL) {
                memcpy(auto_ramdisk_fn, kernel_fn, prefix_len);
                memcpy(auto_ramdisk_fn + prefix_len, bootdata_fn, strlen(bootdata_fn) + 1);
            }
        }
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(NB_ADVERT_PORT);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, s);
        return -1;
    }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    if ((r = bind(s, (void*)&addr, sizeof(addr))) < 0) {
        fprintf(stderr, "%s: cannot bind to [%s]%d %d: %s\n", appname,
                inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
                ntohs(addr.sin6_port), errno, strerror(errno));
        return -1;
    }

    fprintf(stderr, "%s: listening on [%s]%d\n", appname,
            inet_ntop(AF_INET6, &addr.sin6_addr, tmp, sizeof(tmp)),
            ntohs(addr.sin6_port));

    for (;;) {
        struct sockaddr_in6 ra;
        socklen_t rlen;
        char buf[4096];
        nbmsg* msg = (void*)buf;
        rlen = sizeof(ra);
        r = recvfrom(s, buf, sizeof(buf) - 1, 0, (void*)&ra, &rlen);
        if (r < 0) {
            fprintf(stderr, "%s: socket read error %d\n", appname, r);
            break;
        }
        if (r < sizeof(nbmsg))
            continue;
        if (!IN6_IS_ADDR_LINKLOCAL(&ra.sin6_addr)) {
            fprintf(stderr, "%s: ignoring non-link-local message\n", appname);
            continue;
        }
        if (!IN6_IS_ADDR_UNSPECIFIED(&allowed_addr) && !IN6_ARE_ADDR_EQUAL(&allowed_addr, &ra.sin6_addr)) {
            fprintf(stderr, "%s: ignoring message not from allowed address '%s'\n", appname, inet_ntop(AF_INET6, &allowed_addr, tmp, sizeof(tmp)));
            continue;
        }
        if (msg->magic != NB_MAGIC)
            continue;
        if (msg->cmd != NB_ADVERTISE)
            continue;
        if (msg->arg != NB_VERSION_CURRENT) {
            fprintf(stderr, "%s: Incompatible version 0x%08X of bootloader detected from [%s]%d, please upgrade your bootloader\n", appname, msg->arg,
                    inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)),
                    ntohs(ra.sin6_port));
            if (once) {
                break;
            }
            continue;
        }
        fprintf(stderr, "%s: got beacon from [%s]%d\n", appname,
                inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)),
                ntohs(ra.sin6_port));

        // ensure any payload is null-terminated
        buf[r] = 0;


        char* save = NULL;
        char* adv_nodename = NULL;
        char* adv_version = "unknown";
        for (char* var = strtok_r((char*)msg->data, ";", &save); var; var = strtok_r(NULL, ";", &save)) {
            if (!strncmp(var, "nodename=", 9)) {
                adv_nodename = var + 9;
            } else if(!strncmp(var, "version=", 8)) {
                adv_version = var + 8;
            }
        }

        if (nodename) {
            if (adv_nodename == NULL) {
                fprintf(stderr, "%s: ignoring unknown nodename (expecting %s)\n",
                        appname, nodename);
            } else if (strcmp(adv_nodename, nodename)) {
                fprintf(stderr, "%s: ignoring nodename %s (expecting %s)\n",
                        appname, adv_nodename, nodename);
                continue;
            }
        }

        if (strcmp(BOOTLOADER_VERSION, adv_version)) {
            fprintf(stderr,
                    "%s: WARNING:\n"
                    "%s: WARNING: Bootloader version '%s' != '%s'. Please Upgrade.\n"
                    "%s: WARNING:\n",
                    appname, appname, adv_version, BOOTLOADER_VERSION, appname);
        }

        if (cmdline[0]) {
            status = xfer(&ra, "(cmdline)", cmdline, false);
        } else {
            status = 0;
        }
        if (status == 0) {
            struct stat s;
            if (ramdisk_fn) {
                status = xfer(&ra, ramdisk_fn, "ramdisk.bin", false);
            } else if (auto_ramdisk_fn && (stat(auto_ramdisk_fn, &s) == 0)) {
                status = xfer(&ra, auto_ramdisk_fn, "ramdisk.bin", false);
            }
        }
        if (status == 0) {
            xfer(&ra, kernel_fn, "kernel.bin", true);
        }
        if (once) {
            break;
        }
        drain(s);
    }

    return 0;
}
