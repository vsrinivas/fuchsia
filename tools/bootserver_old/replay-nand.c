// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include "bootserver.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <zircon/boot/netboot.h>

#define ANSI_RED "\x1b[31m"
#define ANSI_GREEN "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_RESET "\x1b[0m"
#define ANSI_CLEARLINE "\33[2K\r"

#define ANSI(name) (use_color == false || is_redirected) ? "" : ANSI_##name

#define log(args...)                                                  \
  do {                                                                \
    char logline[1024];                                               \
    snprintf(logline, sizeof(logline), args);                         \
    fprintf(stderr, "%s [%s] %s\n", date_string(), appname, logline); \
  } while (false)

#define RETRY_DELAY_SEC 1

char* appname;
int64_t us_between_packets = DEFAULT_US_BETWEEN_PACKETS;

static bool use_color = true;
static size_t total_file_size;
static bool file_info_printed;
static int progress_reported;
static int packets_sent;
static char filename_in_flight[PATH_MAX];
static struct timeval start_time, end_time;
static bool is_redirected;
static const char spinner[] = {'|', '/', '-', '\\'};
static bool no_bind = false;
static bool reuseport = false;

char* date_string(void) {
  static char date_buf[80];
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  snprintf(date_buf, sizeof(date_buf), "%4d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return date_buf;
}

char* sockaddr_str(struct sockaddr_in6* addr) {
  static char buf[128];
  char tmp[INET6_ADDRSTRLEN];
  snprintf(buf, sizeof(buf), "[%s]:%d",
           inet_ntop(AF_INET6, &addr->sin6_addr, tmp, INET6_ADDRSTRLEN), ntohs(addr->sin6_port));
  return buf;
}

void initialize_status(const char* name, size_t size) {
  total_file_size = size;
  progress_reported = 0;
  packets_sent = 0;
  snprintf(filename_in_flight, sizeof(filename_in_flight), "%s", name);
}

void update_status(size_t bytes_so_far) {
  char progress_str[PATH_MAX];
  size_t offset = 0;

#define UPDATE_LOG(args...)                                               \
  do {                                                                    \
    if (offset < PATH_MAX) {                                              \
      offset += snprintf(progress_str + offset, PATH_MAX - offset, args); \
    }                                                                     \
  } while (false)

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
      static int spin = 0;

      size_t divider = (total_file_size > 0) ? total_file_size : 1;
      UPDATE_LOG("[%c] %5.01f%% of ", spinner[(spin++) % 4],
                 100.0 * (float)bytes_so_far / (float)divider);
      if (total_file_size < 1024) {
        UPDATE_LOG(" %3zu.0  B", total_file_size);
      } else if (total_file_size < 1024 * 1024) {
        UPDATE_LOG(" %5.1f KB", (float)total_file_size / 1024.0);
      } else if (total_file_size < 1024 * 1024 * 1024) {
        UPDATE_LOG(" %5.1f MB", (float)total_file_size / 1024.0 / 1024.0);
      } else {
        UPDATE_LOG(" %5.1f GB", (float)total_file_size / 1024.0 / 1024.0 / 1024.0);
      }

      struct timeval now;
      gettimeofday(&now, NULL);
      int64_t sec = (int64_t)(now.tv_sec - start_time.tv_sec);
      int64_t usec = (int64_t)(now.tv_usec - start_time.tv_usec);
      int64_t elapsed_usec = sec * 1000000 + usec;
      float bytes_in_sec;
      bytes_in_sec = (float)bytes_so_far * 1000000 / ((float)elapsed_usec);
      if (bytes_in_sec < 1024) {
        UPDATE_LOG("  %5.1f  B/s", bytes_in_sec);
      } else if (bytes_in_sec < 1024 * 1024) {
        UPDATE_LOG("  %5.1f KB/s", bytes_in_sec / 1024.0);
      } else if (bytes_in_sec < 1024 * 1024 * 1024) {
        UPDATE_LOG("  %5.1f MB/s", bytes_in_sec / 1024.0 / 1024.0);
      } else {
        UPDATE_LOG("  %5.1f GB/s", bytes_in_sec / 1024.0 / 1024.0 / 1024.0);
      }

      if (is_last_piece) {
        UPDATE_LOG(".");
      } else {
        UPDATE_LOG(" ");
      }

      // Simplify the file path if from "//out/".
      char* relative_path = strstr(filename_in_flight, "/out/");
      if (!relative_path) {
        UPDATE_LOG("  %s%s%s", ANSI(GREEN), filename_in_flight, ANSI(RESET));
      } else {
        // Path starting with "//" indicates the relative path wrt
        // the base directory of Fuchsia source code.
        UPDATE_LOG("  %s/%s%s", ANSI(GREEN), relative_path, ANSI(RESET));
      }
      fprintf(stderr, "%s%s", ANSI_CLEARLINE, progress_str);
    }
  }
}

static int xfer(struct sockaddr_in6* addr, const char* local_name, const char* remote_name) {
  int result;
  is_redirected = !isatty(fileno(stdout));
  gettimeofday(&start_time, NULL);
  file_info_printed = false;
  bool first = true;
  while ((result = tftp_xfer(addr, local_name, remote_name, true)) == -EAGAIN) {
    if (first) {
      fprintf(stderr, "Target busy, waiting.");
      first = false;
    } else {
      fprintf(stderr, ".");
    }
    sleep(1);
    gettimeofday(&start_time, NULL);
  }
  gettimeofday(&end_time, NULL);
  if (end_time.tv_usec < start_time.tv_usec) {
    end_time.tv_sec -= 1;
    end_time.tv_usec += 1000000;
  }
  fprintf(stderr, "\n");
  return result;
}

// Similar to xfer, but reads from remote to local.
static int xfer2(struct sockaddr_in6* addr, const char* local_name, const char* remote_name) {
  int result;
  is_redirected = !isatty(fileno(stdout));
  gettimeofday(&start_time, NULL);
  file_info_printed = false;
  bool first = true;
  while ((result = tftp_xfer(addr, local_name, remote_name, false)) == -EAGAIN) {
    if (first) {
      fprintf(stderr, "Target busy, waiting.");
      first = false;
    } else {
      fprintf(stderr, ".");
    }
    sleep(1);
    gettimeofday(&start_time, NULL);
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
  fprintf(
      stderr,
      "usage:   %s [ <option> ]* [<zbi>] -- [ <kerneloption> ]* ]\n"
      "\n"
      "options:\n"
      "  -a         only boot device with this IPv6 address\n"
      "  -b <sz>    tftp block size (default=%d, ignored with --netboot)\n"
      "  -i <NN>    number of microseconds between packets\n"
      "             set between 50-500 to deal with poor bootloader network stacks (default=%d)\n"
      "             (ignored with --tftp)\n"
      "  -n         only boot device with this nodename\n"
      "  -w <sz>    tftp window size (default=%d, ignored with --netboot)\n"
      //"  --board_name <name>      name of the board files are meant for\n"
      "  --fvm <file>             use the supplied file as a raw NAND image\n"
      "  --fail-fast  exit on first error\n"
      "  --nocolor    disable ANSI color (false)\n"
      "  --allow-zedboot-version-mismatch warn on zedboot version mismatch rather than fail\n"
      "  --fail-fast-if-version-mismatch  error if zedboot version does not match\n"
      "  --no-bind    do not bind to bootserver port. Should be used with -a <IPV6>\n"
      "  --reuseport  allow other programs to bind the listen port\n",
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
  ssize_t send_result =
      sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&target_addr, sizeof(target_addr));
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
  ssize_t send_result =
      sendto(s, &msg, sizeof(msg), 0, (struct sockaddr*)&target_addr, sizeof(target_addr));
  if (send_result == sizeof(msg)) {
    close(s);
    log("Issued reboot command to %s\n\n", sockaddr_str(ra));
    return 0;
  }
  close(s);
  log("failure sending reboot command to %s", sockaddr_str(ra));
  return -1;
}

static int validate_board_name(const char* board_name, const char* board_info_file) {
  chmod(board_info_file, S_IRWXU);
  int fd = open(board_info_file, O_RDONLY);
  if (fd < 0) {
    log("Unable to read the board info file [%s]", board_info_file);
    return -1;
  }

  board_info_t board_info = {};
  if (read(fd, &board_info, sizeof(board_info)) < (ssize_t)sizeof(board_info)) {
    log("Unable to read the board info file [%s]", board_info_file);
    goto err;
  }
  if (strncmp(board_info.board_name, board_name, sizeof(board_info.board_name)) != 0) {
    log("Expected target to be [%s], but found target is [%s]\n", board_name,
        board_info.board_name);
    log("Confirm that your `fx set` matches the target's board.");
    goto err;
  }

  return 0;
err:
  close(fd);
  return -1;
}

int main(int argc, char** argv) {
  bool fail_fast = false;
  bool fail_fast_if_version_mismatch = false;
  struct in6_addr allowed_addr;
  int32_t allowed_scope_id = -1;
  struct sockaddr_in6 addr;
  char tmp[INET6_ADDRSTRLEN];
  char* nodename = NULL;
  int sock = 1;
  const char* tmpdir = getenv("TMPDIR");
  char board_info_template[] = "%s/board_info.XXXXXX";
  char board_info_file[PATH_MAX];
  const char* board_name = NULL;
  const char* fvm_image = NULL;
  bool allow_zedboot_version_mismatch = false;
  int status;

  if (tmpdir == NULL) {
    tmpdir = "/tmp";
  }

  memset(&allowed_addr, 0, sizeof(allowed_addr));
  if ((appname = strrchr(argv[0], '/')) != NULL) {
    appname++;
  } else {
    appname = argv[0];
  }

  while (argc > 1) {
    if (argv[1][0] != '-') {
      usage();
    } else if (!strcmp(argv[1], "--fvm")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--fvm' option requires an argument (raw NAND image)\n");
        return -1;
      }
      if (fvm_image != NULL) {
        fprintf(stderr, "'--fvm' supplied too many times\n");
        return -1;
      }
      fvm_image = argv[1];
    } else if (!strcmp(argv[1], "--fail-fast")) {
      fail_fast = true;
    } else if (!strcmp(argv[1], "--fail-fast-if-version-mismatch")) {
      fail_fast_if_version_mismatch = true;
    } else if (!strcmp(argv[1], "-b")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'-b' option requires an argument (tftp block size)\n");
        return -1;
      }
      errno = 0;
      static uint16_t block_size;
      block_size = strtoll(argv[1], NULL, 10);
      if (errno != 0 || block_size <= 0) {
        fprintf(stderr, "invalid arg for -b: %s\n", argv[1]);
        return -1;
      }
      tftp_block_size = &block_size;
    } else if (!strcmp(argv[1], "-w")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'-w' option requires an argument (tftp window size)\n");
        return -1;
      }
      errno = 0;
      static uint16_t window_size;
      window_size = strtoll(argv[1], NULL, 10);
      if (errno != 0 || window_size <= 0) {
        fprintf(stderr, "invalid arg for -w: %s\n", argv[1]);
        return -1;
      }
      tftp_window_size = &window_size;
    } else if (!strcmp(argv[1], "-i")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'-i' option requires an argument (micros between packets)\n");
        return -1;
      }
      errno = 0;
      us_between_packets = strtoll(argv[1], NULL, 10);
      if (errno != 0 || us_between_packets <= 0) {
        fprintf(stderr, "invalid arg for -i: %s\n", argv[1]);
        return -1;
      }
      fprintf(stderr, "packet spacing set to %" PRId64 " microseconds\n", us_between_packets);
    } else if (!strcmp(argv[1], "-a")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'-a' option requires a valid ipv6 address\n");
        return -1;
      }

      char* token = strchr(argv[1], '/');
      if (token) {
        allowed_scope_id = atoi(token + 1);
        char temp_ifname[IF_NAMESIZE] = "";
        if (!token[1] || if_indextoname(allowed_scope_id, temp_ifname) == NULL) {
          fprintf(stderr, "%s: invalid interface specified\n", argv[1]);
          return -1;
        }
        argv[1][token - argv[1]] = '\0';
      }

      if (inet_pton(AF_INET6, argv[1], &allowed_addr) != 1) {
        fprintf(stderr, "%s: invalid ipv6 address specified\n", argv[1]);
        return -1;
      }
    } else if (!strcmp(argv[1], "-n")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'-n' option requires a valid nodename\n");
        return -1;
      }
      nodename = argv[1];
    } else if (!strcmp(argv[1], "--nocolor")) {
      use_color = false;
    } else if (!strcmp(argv[1], "--board_name")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--board_name' option requires a valid board name\n");
        return -1;
      }
      board_name = argv[1];
    } else if (!strcmp(argv[1], "--allow-zedboot-version-mismatch")) {
      allow_zedboot_version_mismatch = true;
    } else if (!strcmp(argv[1], "--no-bind")) {
      no_bind = true;
    } else if (!strcmp(argv[1], "--reuseport")) {
      reuseport = true;
    } else {
      usage();
    }
    argc--;
    argv++;
  }
  if (!fvm_image) {
    usage();
  }
  if (!nodename) {
    nodename = getenv("ZIRCON_NODENAME");
  }
  if (nodename) {
    fprintf(stderr, "[%s] Will only boot nodename '%s'\n", appname, nodename);
  }

  if (board_name) {
    log("Board name set to [%s]", board_name);
  }

  sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    log("cannot create socket %d", sock);
    return -1;
  }

  if (!IN6_IS_ADDR_UNSPECIFIED(&allowed_addr) || nodename || reuseport) {
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof 1);
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof 1);
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  if (no_bind) {
    if (IN6_IS_ADDR_UNSPECIFIED(&allowed_addr)) {
      log("need to specify ipv6 address using -a for --no-bind");
      close(sock);
      return -1;
    }
    if (allowed_scope_id == -1) {
      log("need to specify interface number in -a for --no-bind.");
      log("Ex: -a fe80::5054:ff:fe12:3456/4 \nHint: use netls to get the address");
      close(sock);
      return -1;
    }
    memcpy(&addr.sin6_addr, &allowed_addr, sizeof(struct in6_addr));
    addr.sin6_port = htons(NB_SERVER_PORT);
    addr.sin6_scope_id = allowed_scope_id;
    log("Sending request to %s", sockaddr_str(&addr));
  } else {
    addr.sin6_port = htons(NB_ADVERT_PORT);
    if (bind(sock, (void*)&addr, sizeof(addr)) < 0) {
      log("cannot bind to %s %d: %s\nthere may be another bootserver running\n",
          sockaddr_str(&addr), errno, strerror(errno));
      close(sock);
      return -1;
    }
    log("listening on %s", sockaddr_str(&addr));
  }

  for (;;) {
    struct sockaddr_in6 ra;
    socklen_t rlen;
    char buf[4096];
    nbmsg* msg = (void*)buf;
    rlen = sizeof(ra);

    if (no_bind) {
      // Send request to device to get the advertisement instead of waiting for the
      // broadcasted advertisement.
      msg->magic = NB_MAGIC;
      msg->cmd = NB_GET_ADVERT;

      ssize_t send_result =
          sendto(sock, buf, sizeof(nbmsg), 0, (struct sockaddr*)&addr, sizeof(addr));
      if (send_result != sizeof(nbmsg)) {
        if (fail_fast) {
          close(sock);
          return -1;
        }
        sleep(RETRY_DELAY_SEC);
        continue;
      }

      // Ensure that response is received.
      struct pollfd read_fd[1];
      read_fd[0].fd = sock;
      read_fd[0].events = POLLIN;
      int ret = poll(read_fd, 1, 1000);
      if (ret < 0 || !(read_fd[0].revents & POLLIN)) {
        // No response received. Resend request after delay.
        if (fail_fast) {
          close(sock);
          return -1;
        }
        sleep(RETRY_DELAY_SEC);
        continue;
      }
    }

    ssize_t r = recvfrom(sock, buf, sizeof(buf) - 1, 0, (void*)&ra, &rlen);
    if (r < 0) {
      log("socket read error %s", strerror(errno));
      close(sock);
      return -1;
    }
    if ((size_t)r < sizeof(nbmsg)) {
      continue;
    }
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
    if (msg->arg < NB_VERSION_1_3) {
      log("%sIncompatible version 0x%08X of bootloader "
          "detected from %s, please upgrade your bootloader%s",
          ANSI(RED), msg->arg, sockaddr_str(&ra), ANSI(RESET));
      if (fail_fast) {
        close(sock);
        return -1;
      }
      continue;
    }

    log("Received request from %s", sockaddr_str(&ra));

    // ensure any payload is null-terminated
    buf[r] = 0;

    char* save = NULL;
    char* adv_nodename = NULL;
    const char* adv_version = "unknown";
    for (char* var = strtok_r((char*)msg->data, ";", &save); var;
         var = strtok_r(NULL, ";", &save)) {
      if (!strncmp(var, "nodename=", 9)) {
        adv_nodename = var + 9;
      } else if (!strncmp(var, "version=", 8)) {
        adv_version = var + 8;
      }
    }

    if (nodename) {
      if (adv_nodename == NULL) {
        log("ignoring unknown nodename (expecting %s)", nodename);
      } else if (strcmp(adv_nodename, nodename)) {
        log("ignoring nodename %s (expecting %s)", adv_nodename, nodename);
        continue;
      }
    }

    if (strcmp(BOOTLOADER_VERSION, adv_version)) {
      if (allow_zedboot_version_mismatch) {
        log("%sWARNING: Bootserver version '%s' != remote Zedboot version '%s'."
            " Paving may fail.%s",
            ANSI(RED), BOOTLOADER_VERSION, adv_version, ANSI(RESET));
      } else {
        log("%sWARNING: Bootserver version '%s' != remote Zedboot version '%s'."
            " Device will not be serviced. Please upgrade Zedboot.%s",
            ANSI(RED), BOOTLOADER_VERSION, adv_version, ANSI(RESET));
        if (fail_fast || fail_fast_if_version_mismatch) {
          close(sock);
          return -1;
        }
        continue;
      }
    }

    if (adv_nodename) {
      log("Proceeding with nodename %s", adv_nodename);
    }

    log("Transfer starts");
    status = 0;
    // This needs to be first as it validates that the other images are
    // correct.
    if (status == 0 && board_name) {
      snprintf(board_info_file, sizeof(board_info_file), board_info_template, tmpdir);
      const char* tmpfile = mktemp(board_info_file);
      status = xfer2(&ra, tmpfile, NB_BOARD_INFO_FILENAME);
      if (status == 0) {
        status = validate_board_name(board_name, tmpfile);
      }
      unlink(tmpfile);
    }

    if (status == 0 && fvm_image) {
      status = xfer(&ra, fvm_image, NB_NAND_FVM_FILENAME);
    }

    if (status == 0) {
      log("Transfer ends successfully.");
      // Only reboot if we actually paved an image.
      if (fvm_image) {
        send_reboot_command(&ra);
      }
      close(sock);
      return 0;
    } else if (fail_fast) {
      close(sock);
      return -1;
    } else {
      log("Transfer ends incompletely.");
      log("Wait for %u secs before retrying...\n\n", RETRY_DELAY_SEC);
      sleep(RETRY_DELAY_SEC);
    }
    drain(sock);
  }

  close(sock);
  return 0;
}
