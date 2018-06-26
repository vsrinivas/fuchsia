// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <zircon/boot/netboot.h>

#include "bootserver.h"

#define ANSI_RED "\x1b[31m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_RESET "\x1b[0m"
#define ANSI_LINESTART "\33[2K\r"

#define MAX_FVM_IMAGES 4

#define ANSI(name) (use_color == false || is_redirected) ? "" : ANSI_##name

#define log(args...)                                                      \
    do {                                                                  \
        char logline[1024];                                               \
        snprintf(logline, sizeof(logline), args);                         \
        fprintf(stderr, "%s [%s] %s\n", date_string(), appname, logline); \
    } while (false)

char* appname;
int64_t us_between_packets = DEFAULT_US_BETWEEN_PACKETS;

static bool use_tftp = true;
static bool use_color = true;
static size_t total_file_size;
static bool file_info_printed;
static int progress_reported;
static int packets_sent;
static struct timeval start_time, end_time;
static bool is_redirected;
static const char spinner[] = {'|', '/', '-', '\\'};

char* date_string() {
    static char date_buf[80];
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    snprintf(date_buf, sizeof(date_buf), "%4d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return date_buf;
}

char* sockaddr_str(struct sockaddr_in6* addr) {
    static char buf[128];
    char tmp[INET6_ADDRSTRLEN];
    snprintf(buf, sizeof(buf), "[%s]%d",
             inet_ntop(AF_INET6, &addr->sin6_addr, tmp, INET6_ADDRSTRLEN),
             ntohs(addr->sin6_port));
    return buf;
}

static void print_file_info(const char* name, size_t size) {
    size_t prefix_len = strlen(NB_FILENAME_PREFIX);
    const char* base_name;
    if (!strncmp(name, NB_FILENAME_PREFIX, prefix_len)) {
        base_name = &name[prefix_len];
    } else {
        base_name = name;
    }

    char path1[PATH_MAX]; // dirname(), basename() modify in situ.
    char path2[PATH_MAX];
    snprintf(path1, PATH_MAX, "%s", name);
    snprintf(path2, PATH_MAX, "%s", name);

    log("Transfer starts   [%5.1f MB]   %s/%s%s%s (%zu bytes)",
        (float)size / 1024.0 / 1024.0,
        dirname(path1), ANSI(GREEN), basename(path2), ANSI(RESET), size);
}

void initialize_status(const char* name, size_t size) {
    total_file_size = size;
    progress_reported = 0;
    packets_sent = 0;

    if (!file_info_printed) {
        print_file_info(name, size);
        file_info_printed = true;
    }
}

void update_status(size_t bytes_so_far) {
    packets_sent++;

    bool is_last_piece = (bytes_so_far == total_file_size);
    if (total_file_size == 0) {
        return;
    }

    if (is_redirected) {
        int percent_sent = (bytes_so_far * 100 / (total_file_size));
        if (percent_sent - progress_reported >= 5) {
            fprintf(stderr, "\t%d%%...", percent_sent);
            progress_reported = percent_sent;
        }
    } else {
        if (packets_sent > 1024 || is_last_piece) {
            packets_sent = 0;
            float bw = 0;
            static int spin = 0;

            struct timeval now;
            gettimeofday(&now, NULL);
            int64_t sec = (int64_t)(now.tv_sec - start_time.tv_sec);
            int64_t usec = (int64_t)(now.tv_usec - start_time.tv_usec);
            int64_t elapsed_usec = sec * 1000000 + usec;
            bw = (float)bytes_so_far * 1000000 / (1024.0 * 1024.0 * ((float)elapsed_usec));

            fprintf(stderr, "%s", ANSI_LINESTART);
            if (total_file_size > 0) {
                fprintf(stderr, "\t%c %.01f%%", spinner[(spin++) % 4],
                        100.0 * (float)bytes_so_far / (float)total_file_size);
            } else {
                fprintf(stderr, "\t%c", spinner[(spin++) % 4]);
            }
            fprintf(stderr, "\t %.01fMB/s", bw);
            if (is_last_piece) {
                fprintf(stderr, "\tTook %" PRId64 ".%" PRId64 " sec",
                        elapsed_usec / 1000000, elapsed_usec % 1000000);
            }
        }
    }
}

static int xfer(struct sockaddr_in6* addr, const char* local_name, const char* remote_name) {
    int result;
    is_redirected = !isatty(fileno(stdout));
    gettimeofday(&start_time, NULL);
    file_info_printed = false;
    if (use_tftp) {
        bool first = true;
        while ((result = tftp_xfer(addr, local_name, remote_name)) == -EAGAIN) {
            if (first) {
                fprintf(stderr, "Target busy, waiting.");
                first = false;
            } else {
                fprintf(stderr, ".");
            }
            sleep(1);
            gettimeofday(&start_time, NULL);
        }
    } else {
        result = netboot_xfer(addr, local_name, remote_name);
    }
    gettimeofday(&end_time, NULL);
    if (end_time.tv_usec < start_time.tv_usec) {
        end_time.tv_sec -= 1;
        end_time.tv_usec += 1000000;
    }
    fprintf(stderr, "\n");
    return result;
}

void usage(void) {
    fprintf(stderr,
            "usage:   %s [ <option> ]* [<kernel>] [ <ramdisk> ] [ -- [ <kerneloption> ]* ]\n"
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
            "  --fvm <file>     use the supplied file as a sparse FVM image (up to 4 times)\n"
            "  --efi <file>     use the supplied file as an EFI image\n"
            "  --kernc <file>   use the supplied file as a KERN-C CrOS image\n"
            "  --zircona <file> use the supplied file as a ZIRCON-A ZBI\n"
            "  --zirconb <file> use the supplied file as a ZIRCON-B ZBI\n"
            "  --zirconr <file> use the supplied file as a ZIRCON-R ZBI\n"
            "  --netboot    use the netboot protocol\n"
            "  --tftp       use the tftp protocol (default)\n"
            "  --nocolor    disable ANSI color (false)\n",
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
        log("cannot create socket %d", s);
        return -1;
    }
    ssize_t send_result = sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&target_addr,
                                 sizeof(target_addr));
    if (send_result == sizeof(msg)) {
        close(s);
        log("Issued boot command to %s\n\n", sockaddr_str(ra));
        return 0;
    }
    close(s);
    log("failure sending boot command to %s", sockaddr_str(ra));
    return -1;
}

int send_reboot_command(struct sockaddr_in6* ra) {
    // Construct message
    nbmsg msg;
    static int cookie = 0;
    msg.magic = NB_MAGIC;
    msg.cookie = cookie++;
    msg.cmd = NB_REBOOT;
    msg.arg = 0;

    // Send to NB_SERVER_PORT
    struct sockaddr_in6 target_addr;
    memcpy(&target_addr, ra, sizeof(struct sockaddr_in6));
    target_addr.sin6_port = htons(NB_SERVER_PORT);
    int s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        log("cannot create socket %d", s);
        return -1;
    }
    ssize_t send_result = sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&target_addr,
                                 sizeof(target_addr));
    if (send_result == sizeof(msg)) {
        close(s);
        log("Issued reboot command to %s\n\n", sockaddr_str(ra));
        return 0;
    }
    close(s);
    log("failure sending reboot command to %s", sockaddr_str(ra));
    return -1;
}

int main(int argc, char** argv) {
    struct in6_addr allowed_addr;
    struct sockaddr_in6 addr;
    char tmp[INET6_ADDRSTRLEN];
    char cmdline[4096];
    char* cmdnext = cmdline;
    char* nodename = NULL;
    int r, s = 1;
    int num_fvms = 0;
    const char* efi_image = NULL;
    const char* kernc_image = NULL;
    const char* zircona_image = NULL;
    const char* zirconb_image = NULL;
    const char* zirconr_image = NULL;
    const char* fvm_images[MAX_FVM_IMAGES] = {NULL, NULL, NULL, NULL};
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
        } else if (!strcmp(argv[1], "--fvm")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--fvm' option requires an argument (FVM image)\n");
                return -1;
            }
            if (num_fvms == MAX_FVM_IMAGES) {
                fprintf(stderr, "'--fvm' supplied too many times\n");
                return -1;
            }
            fvm_images[num_fvms++] = argv[1];
        } else if (!strcmp(argv[1], "--efi")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--efi' option requires an argument (EFI image)\n");
                return -1;
            }
            efi_image = argv[1];
        } else if (!strcmp(argv[1], "--kernc")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--kernc' option requires an argument (KERN-C image)\n");
                return -1;
            }
            kernc_image = argv[1];
        } else if (!strcmp(argv[1], "--zircona")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--zircona' option requires an argument (ZIRCON-A image)\n");
                return -1;
            }
            zircona_image = argv[1];
        } else if (!strcmp(argv[1], "--zirconb")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--zirconb' option requires an argument (ZIRCON-B image)\n");
                return -1;
            }
            zirconb_image = argv[1];
        } else if (!strcmp(argv[1], "--zirconr")) {
            argc--;
            argv++;
            if (argc <= 1) {
                fprintf(stderr, "'--zirconr' option requires an argument (ZIRCON-R image)\n");
                return -1;
            }
            zirconr_image = argv[1];
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
        } else if (!strcmp(argv[1], "--nocolor")) {
            use_color = false;
        } else if (!strcmp(argv[1], "--")) {
            while (argc > 2) {
                size_t len = strlen(argv[2]);
                if (len > (sizeof(cmdline) - 2 - (cmdnext - cmdline))) {
                    fprintf(stderr, "[%s] commandline too large\n", appname);
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
    if (!kernel_fn && !efi_image && !kernc_image && !zircona_image && !zirconb_image &&
        !zirconr_image && !fvm_images[0]) {
        usage();
    }
    if (!nodename) {
        nodename = getenv("ZIRCON_NODENAME");
    }
    if (nodename) {
        fprintf(stderr, "[%s] Will only boot nodename '%s'\n", appname, nodename);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(NB_ADVERT_PORT);

    s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        log("cannot create socket %d", s);
        return -1;
    }
    if ((r = bind(s, (void*)&addr, sizeof(addr))) < 0) {
        log("cannot bind to %s %d: %s\nthere may be another bootserver running\n",
            sockaddr_str(&addr),
            errno, strerror(errno));
        close(s);
        return -1;
    }

    log("listening on %s", sockaddr_str(&addr));

    for (;;) {
        struct sockaddr_in6 ra;
        socklen_t rlen;
        char buf[4096];
        nbmsg* msg = (void*)buf;
        rlen = sizeof(ra);
        r = recvfrom(s, buf, sizeof(buf) - 1, 0, (void*)&ra, &rlen);
        if (r < 0) {
            log("socket read error %d", r);
            close(s);
            return -1;
        }
        if (r < sizeof(nbmsg))
            continue;
        if (!IN6_IS_ADDR_LINKLOCAL(&ra.sin6_addr)) {
            log("ignoring non-link-local message");
            continue;
        }
        if (!IN6_IS_ADDR_UNSPECIFIED(&allowed_addr) &&
            !IN6_ARE_ADDR_EQUAL(&allowed_addr, &ra.sin6_addr)) {
            log("ignoring message not from allowed address '%s'",
                inet_ntop(AF_INET6, &allowed_addr, tmp, sizeof(tmp)));
            continue;
        }
        if (msg->magic != NB_MAGIC)
            continue;
        if (msg->cmd != NB_ADVERTISE)
            continue;
        if ((use_tftp && (msg->arg < NB_VERSION_1_3)) ||
            (!use_tftp && (msg->arg < NB_VERSION_1_1))) {
            log("%sIncompatible version 0x%08X of bootloader "
                "detected from %s, please upgrade your bootloader%s",
                ANSI(RED), msg->arg, sockaddr_str(&ra), ANSI(RESET));
            if (once) {
                close(s);
                return -1;
            }
            continue;
        }

        log("got beacon from %s", sockaddr_str(&ra));

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
            } else if (!strncmp(var, "version=", 8)) {
                adv_version = var + 8;
            }
        }

        if (nodename) {
            if (adv_nodename == NULL) {
                log("ignoring unknown nodename (expecting %s)",
                    nodename);
            } else if (strcmp(adv_nodename, nodename)) {
                log("ignoring nodename %s (expecting %s)",
                    adv_nodename, nodename);
                continue;
            }
        }

        if (strcmp(BOOTLOADER_VERSION, adv_version)) {
            log("%sWARNING: Bootserver version '%s' != remote bootloader '%s'. Please Upgrade%s",
                ANSI(RED), BOOTLOADER_VERSION, adv_version, ANSI(RESET));
            close(s);
            return -1;
        }

        if (cmdline[0]) {
            status = xfer(&ra, "(cmdline)", cmdline);
        } else {
            status = 0;
        }
        if (status == 0) {
            if (ramdisk_fn) {
                status = xfer(&ra, ramdisk_fn, NB_RAMDISK_FILENAME);
            }
        }
        for (size_t i = 0; i < num_fvms; i++) {
            if (status == 0 && fvm_images[i]) {
                status = xfer(&ra, fvm_images[i], NB_FVM_FILENAME);
            }
        }
        if (status == 0 && efi_image) {
            status = xfer(&ra, efi_image, NB_EFI_FILENAME);
        }
        if (status == 0 && kernc_image) {
            status = xfer(&ra, kernc_image, NB_KERNC_FILENAME);
        }
        if (status == 0 && zircona_image) {
            status = xfer(&ra, zircona_image, NB_ZIRCONA_FILENAME);
        }
        if (status == 0 && zirconb_image) {
            status = xfer(&ra, zirconb_image, NB_ZIRCONB_FILENAME);
        }
        if (status == 0 && zirconr_image) {
            status = xfer(&ra, zirconr_image, NB_ZIRCONR_FILENAME);
        }
        if (status == 0 && kernel_fn) {
            status = xfer(&ra, kernel_fn, NB_KERNEL_FILENAME);
        }
        if (status == 0) {
            if (kernel_fn) {
                send_boot_command(&ra);
            } else {
                send_reboot_command(&ra);
            }
        }
        if (once) {
            close(s);
            return status == 0 ? 0 : -1;
        }
        drain(s);
    }

    close(s);
    return 0;
}
