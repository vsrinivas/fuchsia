// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <magenta/boot/netboot.h>

#include "bootserver.h"

char* appname;
int64_t us_between_packets = DEFAULT_US_BETWEEN_PACKETS;
bool use_filename_prefix = true;

static bool use_tftp = false;
static size_t total_file_size;
static int progress_reported;
static int packets_sent;
static struct timeval start_time, end_time;
static bool is_redirected;
static const char spinner[] = {'|', '/', '-', '\\'};

void initialize_status(const char* name, size_t size) {
    total_file_size = size;
    progress_reported = 0;
    packets_sent = 0;
    size_t prefix_len = strlen(NB_FILENAME_PREFIX);
    const char* base_name;
    if (!strncmp(name, NB_FILENAME_PREFIX, prefix_len)) {
        base_name = &name[prefix_len];
    } else {
        base_name = name;
    }
    fprintf(stderr, "Sending %s [%lu bytes]:\n", base_name, (unsigned long)size);
}

void update_status(size_t bytes_so_far) {
    packets_sent++;
    if (total_file_size == 0) {
        return;
    }
    if (is_redirected) {
        int percent_sent = (bytes_so_far / (total_file_size / 100));
        if (percent_sent - progress_reported >= 5) {
            fprintf(stderr, "%d%%...", percent_sent);
            progress_reported = percent_sent;
        }
    } else {
        if (packets_sent > 1024) {
            packets_sent = 0;
            float bw = 0;
            static int spin = 0;

            struct timeval now;
            gettimeofday(&now, NULL);
            int64_t us_since_begin = ((int64_t)(now.tv_sec - start_time.tv_sec) * 1000000) +
                                     ((int64_t)now.tv_usec - start_time.tv_usec);
            if (us_since_begin >= 1000000) {
                bw = (float)bytes_so_far / (1024.0 * 1024.0 * ((float)us_since_begin / 1000000));
            }

            fprintf(stderr, "\33[2K\r");
            if (total_file_size > 0) {
                fprintf(stderr, "%c %.01f%%", spinner[(spin++) % 4],
                        100.0 * (float)bytes_so_far / (float)total_file_size);
            } else {
                fprintf(stderr, "%c", spinner[(spin++) % 4]);
            }
            if (bw > 0.1) {
                fprintf(stderr, " %.01fMB/s", bw);
            }
        }
    }
}

static int xfer(struct sockaddr_in6* addr, const char* local_name, const char* remote_name) {
    int result;
    is_redirected = !isatty(fileno(stdout));
    gettimeofday(&start_time, NULL);
    if (use_tftp) {
        result = tftp_xfer(addr, local_name, remote_name);
    } else {
        result = netboot_xfer(addr, local_name, remote_name);
    }
    gettimeofday(&end_time, NULL);
    if (end_time.tv_usec < start_time.tv_usec) {
        end_time.tv_sec -= 1;
        end_time.tv_usec += 1000000;
    }
    if (result == 0) {
        fprintf(stderr, "\nTransfer completed in %d.%06d sec\n",
                (int)(end_time.tv_sec - start_time.tv_sec),
                (int)(end_time.tv_usec - start_time.tv_usec));
    }
    return result;
}

void usage(void) {
    fprintf(stderr,
            "usage:   %s [ <option> ]* <kernel> [ <ramdisk> ] [ -- [ <kerneloption> ]* ]\n"
            "\n"
            "options:\n"
            "  -1         only boot once, then exit\n"
            "  -a         only boot device with this IPv6 address\n"
            "  -b <sz>    tftp block size (default=%d, ignored with --netboot)\n"
            "  -i <NN>    number of microseconds between packets\n"
            "             set between 50-500 to deal with poor bootloader network stacks (default=%d)\n"
            "             (ignored with --tftp)\n"
            "  -n         only boot device with this nodename\n"
            "  -w <sz>    tftp window size (default=%d, ignored with --netboot)\n"
            "  --netboot  use the netboot protocol (default)\n"
            "  --tftp     use the tftp protocol\n",
            appname, DEFAULT_TFTP_BLOCK_SZ, DEFAULT_US_BETWEEN_PACKETS, DEFAULT_TFTP_WIN_SZ);
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

int send_boot_command(struct sockaddr_in6* ra) {
    // Construct message
    nbmsg msg;
    static int cookie = 0;
    msg.magic = NB_MAGIC;
    msg.cookie = cookie++;
    msg.cmd = NB_BOOT;
    msg.arg = 0;

    // Send to NB_SERVER_PORT
    struct sockaddr_in6 target_addr;
    memcpy(&target_addr, ra, sizeof(struct sockaddr_in6));
    target_addr.sin6_port = htons(NB_SERVER_PORT);
    int s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        fprintf(stderr, "%s: cannot create socket %d\n", appname, s);
        return -1;
    }
    ssize_t send_result = sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&target_addr,
                                 sizeof(target_addr));
    if (send_result == sizeof(msg)) {
        fprintf(stderr, "%s: sent boot command\n", appname);
        return 0;
    }
    fprintf(stderr, "%s: failure sending boot command\n", appname);
    return -1;
}

int main(int argc, char** argv) {
    struct in6_addr allowed_addr;
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

    memset(&allowed_addr, 0, sizeof(allowed_addr));
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
        } else if (!strcmp(argv[1], "-b")) {
            if (argc <= 1) {
                fprintf(stderr, "'-b' option requires an argument (tftp block size)\n");
                return -1;
            }
            errno = 0;
            static uint16_t block_size;
            block_size = strtoll(argv[2], NULL, 10);
            if (errno != 0 || block_size <= 0) {
                fprintf(stderr, "invalid arg for -b: %s\n", argv[2]);
                return -1;
            }
            tftp_block_size = &block_size;
            argc--;
            argv++;
        } else if (!strcmp(argv[1], "-w")) {
            if (argc <= 1) {
                fprintf(stderr, "'-w' option requires an argument (tftp window size)\n");
                return -1;
            }
            errno = 0;
            static uint16_t window_size;
            window_size = strtoll(argv[2], NULL, 10);
            if (errno != 0 || window_size <= 0) {
                fprintf(stderr, "invalid arg for -w: %s\n", argv[2]);
                return -1;
            }
            tftp_window_size = &window_size;
            argc--;
            argv++;
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
        } else if (!strcmp(argv[1], "--netboot")) {
            use_tftp = false;
        } else if (!strcmp(argv[1], "--tftp")) {
            use_tftp = true;
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
        if (!IN6_IS_ADDR_UNSPECIFIED(&allowed_addr) &&
            !IN6_ARE_ADDR_EQUAL(&allowed_addr, &ra.sin6_addr)) {
            fprintf(stderr, "%s: ignoring message not from allowed address '%s'\n",
                    appname, inet_ntop(AF_INET6, &allowed_addr, tmp, sizeof(tmp)));
            continue;
        }
        if (msg->magic != NB_MAGIC)
            continue;
        if (msg->cmd != NB_ADVERTISE)
            continue;
        if ((use_tftp && (msg->arg < NB_VERSION_1_2)) ||
            (!use_tftp && (msg->arg < NB_VERSION_1_1))) {
            fprintf(stderr, "%s: Incompatible version 0x%08X of bootloader detected from [%s]%d, "
                            "please upgrade your bootloader\n",
                    appname, msg->arg, inet_ntop(AF_INET6, &ra.sin6_addr, tmp, sizeof(tmp)),
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
        for (char* var = strtok_r((char*)msg->data, ";", &save);
             var;
             var = strtok_r(NULL, ";", &save)) {
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
            if (!strcmp(adv_version, "0.5.5")) {
                use_filename_prefix = false;
            }
        } else {
            use_filename_prefix = true;
        }

        if (cmdline[0]) {
            status = xfer(&ra, "(cmdline)", cmdline);
        } else {
            status = 0;
        }
        if (status == 0) {
            struct stat s;
            if (ramdisk_fn) {
                status = xfer(&ra, ramdisk_fn,
                              use_filename_prefix ? NB_RAMDISK_FILENAME : "ramdisk.bin");
            } else if (auto_ramdisk_fn && (stat(auto_ramdisk_fn, &s) == 0)) {
                status = xfer(&ra, auto_ramdisk_fn,
                              use_filename_prefix ? NB_RAMDISK_FILENAME : "ramdisk.bin");
            }
        }
        if (status == 0) {
            status = xfer(&ra, kernel_fn, use_filename_prefix ? NB_KERNEL_FILENAME : "kernel.bin");
            if (status == 0) {
                send_boot_command(&ra);
            }
        }
        if (once) {
            break;
        }
        drain(s);
    }

    return 0;
}
