// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#include "bootserver.h"

#define DEFAULT_US_BETWEEN_PACKETS 20

static uint32_t cookie = 1;
static const int MAX_READ_RETRIES = 10;
static const int MAX_SEND_RETRIES = 10000;

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
    const char* ptr;
    size_t avail;
} xferdata;

static ssize_t xread(xferdata* xd, void* data, size_t len) {
    if (xd->fp == NULL) {
        if (len > xd->avail) {
            len = xd->avail;
        }
        memcpy(data, xd->ptr, len);
        xd->avail -= len;
        xd->ptr += len;
        return len;
    } else {
        ssize_t r = fread(data, 1, len, xd->fp);
        if (r == 0) {
            return ferror(xd->fp) ? -1 : 0;
        }
        return r;
    }
}

static int xseek(xferdata* xd, off_t off) {
    if (xd->fp == NULL) {
        if (off > xd->datalen) {
            return -1;
        }
        xd->ptr = xd->data + off;
        xd->avail = xd->datalen - off;
        return 0;
    } else {
        return fseek(xd->fp, off, SEEK_SET);
    }
}

// UDP6_MAX_PAYLOAD (ETH_MTU - ETH_HDR_LEN - IP6_HDR_LEN - UDP_HDR_LEN)
//      1452           1514   -     14      -     40      -    8
// nbfile is PAYLOAD_SIZE + 2 * sizeof(size_t)

// Some EFI network stacks have problems with larger packets
// 1280 is friendlier
#define PAYLOAD_SIZE 1280

int netboot_xfer(struct sockaddr_in6* addr, const char* fn, const char* name) {
    xferdata xd;
    char msgbuf[2048];
    char ackbuf[2048];
    char tmp[INET6_ADDRSTRLEN];
    struct timeval tv;
    nbmsg* msg = (void*)msgbuf;
    nbmsg* ack = (void*)ackbuf;
    int s, r;
    int status = -1;
    size_t current_pos = 0;
    long sz = 0;

    if (!strcmp(fn, "(cmdline)")) {
        xd.fp = NULL;
        xd.data = name;
        xd.datalen = strlen(name) + 1;
        xd.ptr = xd.data;
        xd.avail = xd.datalen;
        name = use_filename_prefix ? NB_CMDLINE_FILENAME : "cmdline";
        sz = xd.datalen;
    } else {
        if ((xd.fp = fopen(fn, "rb")) == NULL) {
            fprintf(stderr, "%s: error: Could not open file %s\n", appname, fn);
            return -1;
        }
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

    if (sz > 0) {
        initialize_status(xd.data, sz);
    }

    if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr, "%s: error: Cannot create socket %d\n", appname, errno);
        goto done;
    }
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

        update_status(msg->arg);

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
                us_since_last_packet = (int64_t)(now.tv_sec - packet_start_time.tv_sec) * 1000000 +
                                       ((int64_t)now.tv_usec - (int64_t)packet_start_time.tv_usec);
            } while (us_since_last_packet < us_between_packets);
        }

        // ACKs really are NACKs
        if (ack->cookie > 0 && ack->cmd == NB_ACK && ack->arg != current_pos) {
            fprintf(stderr, "\n%s: need to rewind to %d from %zu\n",
                    appname, ack->arg, current_pos);
            current_pos = ack->arg;
            if (xseek(&xd, current_pos)) {
                fprintf(stderr, "\n%s: error: Failed to rewind '%s' to %zu\n",
                        appname, fn, current_pos);
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
done:
    if (s >= 0) {
        close(s);
    }
    if (xd.fp != NULL) {
        fclose(xd.fp);
    }
    return status;
}
