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

#define MAX_FVM_IMAGES 4
#define MAX_FIRMWARE_IMAGES 4

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

static bool use_tftp = true;
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

struct firmware {
  const char* type;
  const char* image;
};

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
  if (use_tftp) {
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

// Similar to xfer, but reads from remote to local.
static int xfer2(struct sockaddr_in6* addr, const char* local_name, const char* remote_name) {
  int result;
  is_redirected = !isatty(fileno(stdout));
  gettimeofday(&start_time, NULL);
  file_info_printed = false;
  if (use_tftp) {
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
  } else {
    log("Skipping read operation. Only supported using tftp.");
    result = 0;
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
      "  --board_name <name>      name of the board files are meant for\n"
      "  --boot <file>            use the supplied file as a kernel\n"
      "  --fvm <file>             use the supplied file as a sparse FVM image (up to 4 times)\n"
      "  --bootloader <file>      use the supplied file as a BOOTLOADER image\n"
      "  --firmware <file>        use the supplied file as a FIRMWARE image of default type\n"
      "  --firmware-<type> <file> use the supplied file as a FIRMWARE image of the given type\n"
      "  --zircona <file>         use the supplied file as a ZIRCON-A ZBI\n"
      "  --zirconb <file>         use the supplied file as a ZIRCON-B ZBI\n"
      "  --zirconr <file>         use the supplied file as a ZIRCON-R ZBI\n"
      "  --vbmetaa <file>         use the supplied file as a AVB vbmeta_a image\n"
      "  --vbmetab <file>         use the supplied file as a AVB vbmeta_b image\n"
      "  --vbmetar <file>         use the supplied file as a AVB vbmeta_r image\n"
      "  --authorized-keys <file> use the supplied file as an authorized_keys file\n"
      "  --init-partition-tables <path>  initialize block device specified with partition tables\n"
      "  --wipe-partition-tables <path>  wipe partition tables from block device specified\n"
      "  --fail-fast  exit on first error\n"
      "  --netboot    use the netboot protocol\n"
      "  --tftp       use the tftp protocol (default)\n"
      "  --nocolor    disable ANSI color (false)\n"
      "  --allow-zedboot-version-mismatch warn on zedboot version mismatch rather than fail\n"
      "  --fail-fast-if-version-mismatch  error if zedboot version does not match\n"
      "  --no-bind    do not bind to bootserver port. Should be used with -a <IPV6>\n",
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
  char cmdline[4096];
  char* cmdnext = cmdline;
  char* nodename = NULL;
  int sock = 1;
  size_t num_fvms = 0;
  size_t num_firmware = 0;
  char* tmpdir = getenv("TMPDIR");
  char board_info_template[] = "%s/board_info.XXXXXX";
  char board_info_file[PATH_MAX];
  const char block_device_path_template[] = "%s/block_device_path.XXXXXX";
  char block_device_path[PATH_MAX];
  const char* board_name = NULL;
  const char* bootloader_image = NULL;
  struct firmware firmware_images[MAX_FIRMWARE_IMAGES];
  const char* zircona_image = NULL;
  const char* zirconb_image = NULL;
  const char* zirconr_image = NULL;
  const char* vbmetaa_image = NULL;
  const char* vbmetab_image = NULL;
  const char* vbmetar_image = NULL;
  const char* authorized_keys = NULL;
  const char* fvm_images[MAX_FVM_IMAGES] = {NULL, NULL, NULL, NULL};
  const char* kernel_fn = NULL;
  const char* ramdisk_fn = NULL;
  const char* init_partition_tables_device_path = NULL;
  const char* wipe_partition_tables_device_path = NULL;
  int once = 0;
  bool allow_zedboot_version_mismatch = false;
  int status;

  if (tmpdir == NULL) {
    tmpdir = "/tmp";
  }

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
    } else if (!strcmp(argv[1], "--bootloader")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--bootloader' option requires an argument (BOOTLOADER image)\n");
        return -1;
      }
      bootloader_image = argv[1];
    } else if (!strncmp(argv[1], "--firmware", strlen("--firmware"))) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--firmware' options require an argument (FIRMWARE image)\n");
        return -1;
      }
      if (num_firmware == MAX_FIRMWARE_IMAGES) {
        // It's fine to increase MAX_FIRMWARE_IMAGES if necessary, it's just
        // used for simplicity to avoid implementing a growable list in C.
        fprintf(stderr, "'--firmware' supplied too many times\n");
        return -1;
      }

      // Extract the type from the argument name.
      const char* type = argv[0] + strlen("--firmware");
      if (type[0] == '\0') {
        // No type given, use the current (empty) string.
      } else if (type[0] == '-') {
        // Skip the '-' delimiter and use the remainder as the type.
        type++;

        if (strlen(type) > NB_FIRMWARE_TYPE_MAX_LENGTH) {
          fprintf(stderr, "firmware type '%s' is too long (max %d characters)\n", type,
                  NB_FIRMWARE_TYPE_MAX_LENGTH);
          return -1;
        }
      } else {
        fprintf(stderr, "invalid argument '%s', use '--firmware[-type]'\n", argv[0]);
        fprintf(stderr, "examples: '--firmware', '--firmware-foo'\n");
        return -1;
      }

      firmware_images[num_firmware].type = type;
      firmware_images[num_firmware].image = argv[1];
      num_firmware++;
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
    } else if (!strcmp(argv[1], "--vbmetaa")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--vbmetaa' option requires an argument (vbmeta_a image)\n");
        return -1;
      }
      vbmetaa_image = argv[1];
    } else if (!strcmp(argv[1], "--vbmetab")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--vbmetab' option requires an argument (vbmeta_b image)\n");
        return -1;
      }
      vbmetab_image = argv[1];
    } else if (!strcmp(argv[1], "--vbmetar")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--vbmetar' option requires an argument (vbmeta_r image)\n");
        return -1;
      }
      vbmetar_image = argv[1];
    } else if (!strcmp(argv[1], "--authorized-keys")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--authorized-keys' option requires an argument (authorized_keys)\n");
        return -1;
      }
      authorized_keys = argv[1];
    } else if (!strcmp(argv[1], "--fail-fast")) {
      fail_fast = true;
    } else if (!strcmp(argv[1], "--fail-fast-if-version-mismatch")) {
      fail_fast_if_version_mismatch = true;
    } else if (!strcmp(argv[1], "--boot")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--boot' option requires an argument (a kernel image)\n");
        return -1;
      }
      kernel_fn = argv[1];
    } else if (!strcmp(argv[1], "-1")) {
      once = 1;
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
    } else if (!strcmp(argv[1], "--netboot")) {
      use_tftp = false;
    } else if (!strcmp(argv[1], "--tftp")) {
      use_tftp = true;
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
    } else if (!strcmp(argv[1], "--init-partition-tables")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--init-partition-tables' option requires a valid board name\n");
        return -1;
      }
      init_partition_tables_device_path = argv[1];
    } else if (!strcmp(argv[1], "--wipe-partition-tables")) {
      argc--;
      argv++;
      if (argc <= 1) {
        fprintf(stderr, "'--wipe-partition-tables' option requires a valid board name\n");
        return -1;
      }
      wipe_partition_tables_device_path = argv[1];
    } else if (!strcmp(argv[1], "--")) {
      while (argc > 2) {
        argc--;
        argv++;
        size_t len = strlen(argv[1]);
        if (len > (sizeof(cmdline) - 2 - (cmdnext - cmdline))) {
          fprintf(stderr, "[%s] commandline too large\n", appname);
          return -1;
        }
        if (cmdnext != cmdline) {
          *cmdnext++ = ' ';
        }
        memcpy(cmdnext, argv[1], len + 1);
        cmdnext += len;
      }
      break;
    } else {
      usage();
    }
    argc--;
    argv++;
  }
  if (!kernel_fn && !bootloader_image && !num_firmware && !zircona_image && !zirconb_image &&
      !zirconr_image && !vbmetaa_image && !vbmetab_image && !fvm_images[0] &&
      !init_partition_tables_device_path && !wipe_partition_tables_device_path) {
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

  if (!IN6_IS_ADDR_UNSPECIFIED(&allowed_addr) || nodename) {
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof 1);
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
      log("Ex: -a fe80::5054:4d:fe12:3456/4 \nHint: use netls to get the address");
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

      ssize_t send_result = sendto(sock, buf, sizeof(nbmsg), 0, (struct sockaddr*)&addr, sizeof(addr));
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
    if ((use_tftp && (msg->arg < NB_VERSION_1_3)) || (!use_tftp && (msg->arg < NB_VERSION_1_1))) {
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
    if (status == 0 && cmdline[0]) {
      status = xfer(&ra, "(cmdline)", cmdline);
    }
    if (status == 0 && ramdisk_fn) {
      status = xfer(&ra, ramdisk_fn, NB_RAMDISK_FILENAME);
    }
    // Wipe partition tables before writing anything to persistent storage.
    if (status == 0 && wipe_partition_tables_device_path) {
      snprintf(block_device_path, sizeof(block_device_path), block_device_path_template, tmpdir);
      int fd = mkstemp(block_device_path);
      modify_partition_table_info_t info = {};
      strncpy(info.block_device_path, wipe_partition_tables_device_path, ZX_MAX_NAME_LEN);
      int written = write(fd, &info, sizeof(info));
      status = written == sizeof(info) ? 0 : -1;
      if (status == 0) {
        status = xfer(&ra, block_device_path, NB_WIPE_PARTITION_TABLES_FILENAME);
      }
      unlink(block_device_path);
      close(fd);
    }
    // Initialize partition tables before writing anything to persistent storage.
    if (status == 0 && init_partition_tables_device_path) {
      snprintf(block_device_path, sizeof(block_device_path), block_device_path_template, tmpdir);
      int fd = mkstemp(block_device_path);
      modify_partition_table_info_t info = {};
      strncpy(info.block_device_path, init_partition_tables_device_path, ZX_MAX_NAME_LEN);
      int written = write(fd, &info, sizeof(info));
      status = written == sizeof(info) ? 0 : -1;
      if (status == 0) {
        status = xfer(&ra, block_device_path, NB_INIT_PARTITION_TABLES_FILENAME);
      }
      unlink(block_device_path);
      close(fd);
    }
    for (size_t i = 0; i < num_fvms; i++) {
      if (status == 0 && fvm_images[i]) {
        status = xfer(&ra, fvm_images[i], NB_FVM_FILENAME);
      }
    }
    if (status == 0 && bootloader_image) {
      status = xfer(&ra, bootloader_image, NB_BOOTLOADER_FILENAME);
    }
    for (size_t i = 0; i < num_firmware; i++) {
      if (status == 0) {
        char filename[strlen(NB_FIRMWARE_FILENAME_PREFIX) + NB_FIRMWARE_TYPE_MAX_LENGTH + 1];
        int result = snprintf(filename, sizeof(filename), "%s%s", NB_FIRMWARE_FILENAME_PREFIX,
                              firmware_images[i].type);
        if (result < 0 || (size_t)result >= sizeof(filename)) {
          fprintf(stderr, "error creating firmware filename for type '%s'\n",
                  firmware_images[i].type);
          status = -1;
        } else {
          status = xfer(&ra, firmware_images[i].image, filename);

          // In order to keep updates as stable as possible in the near future,
          // keep going even if we fail to flash the firmware. It's OK to have
          // older bootloaders on a newer OS, and this will allow paving to
          // succeed even if the device netsvc doesn't yet know how to handle
          // firmware files.
          //
          // TODO(fxbug.dev/45606): once we bump the version past "0.7.22" and force a
          // hard-transition anyway we can remove this workaround.
          if (status != 0) {
            fprintf(
                stderr,
                "Failed to transfer firmware type '%s' (err=%d), skipping and continuing.\n"
                "This is expected until zedboot has been updated to the newest version.\n"
                "If you continue to see this after updating zedboot, please file a Firmware bug.\n",
                firmware_images[i].type, status);
            status = 0;
          }
        }
      }
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
    if (status == 0 && vbmetaa_image) {
      status = xfer(&ra, vbmetaa_image, NB_VBMETAA_FILENAME);
    }
    if (status == 0 && vbmetab_image) {
      status = xfer(&ra, vbmetab_image, NB_VBMETAB_FILENAME);
    }
    if (status == 0 && vbmetar_image) {
      status = xfer(&ra, vbmetar_image, NB_VBMETAR_FILENAME);
    }
    if (status == 0 && authorized_keys) {
      status = xfer(&ra, authorized_keys, NB_SSHAUTH_FILENAME);
    }
    if (status == 0 && kernel_fn) {
      status = xfer(&ra, kernel_fn, NB_KERNEL_FILENAME);
    }
    if (status == 0) {
      log("Transfer ends successfully.");
      // Only reboot if we actually paved an image.
      if (kernel_fn || bootloader_image || num_firmware || zircona_image || zirconb_image ||
          zirconr_image || vbmetaa_image || vbmetab_image || fvm_images[0]) {
        if (kernel_fn) {
          send_boot_command(&ra);
        } else {
          send_reboot_command(&ra);
        }
      }
      if (once) {
        close(sock);
        return 0;
      }
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
