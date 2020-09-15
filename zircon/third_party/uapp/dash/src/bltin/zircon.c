// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ddk/device.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/hardware/power/statecontrol/c/fidl.h>
#include <fuchsia/kernel/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/txn_header.h>
#include <pretty/hexdump.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

int zxc_dump(int argc, char** argv) {
    int fd;
    ssize_t len;
    off_t off;
    char buf[4096];

    if (argc != 2) {
        fprintf(stderr, "usage: dump <filename>\n");
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    off = 0;
    for (;;) {
        len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            if (len)
                fprintf(stderr, "error: io\n");
            break;
        }
        hexdump8_ex(buf, len, off);
        off += len;
    }
    close(fd);
    return len;
}

int zxc_msleep(int argc, char** argv) {
    if (argc == 2) {
        zx_nanosleep(zx_deadline_after(ZX_MSEC(atoi(argv[1]))));
    }
    return 0;
}

static const char* modestr(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG:
        return "-";
    case S_IFCHR:
        return "c";
    case S_IFBLK:
        return "b";
    case S_IFDIR:
        return "d";
    default:
        return "?";
    }
}

int zxc_ls(int argc, char** argv) {
    const char* dirn;
    struct stat s;
    char tmp[2048];
    size_t dirln;
    struct dirent* de;
    DIR* dir;

    if ((argc > 1) && !strcmp(argv[1], "-l")) {
        argc--;
        argv++;
    }
    if (argc < 2) {
        dirn = ".";
    } else {
        dirn = argv[1];
    }
    dirln = strlen(dirn);

    if (argc > 2) {
        fprintf(stderr, "usage: ls [ <file_or_directory> ]\n");
        return -1;
    }
    if ((dir = opendir(dirn)) == NULL) {
        if(stat(dirn, &s) == -1) {
            fprintf(stderr, "error: cannot stat '%s'\n", dirn);
            return -1;
        }
        printf("%s %8jd %s\n", modestr(s.st_mode), (intmax_t)s.st_size, dirn);
        return 0;
    }
    while((errno = 0, de = readdir(dir)) != NULL) {
        memset(&s, 0, sizeof(struct stat));
        if ((strlen(de->d_name) + dirln + 2) <= sizeof(tmp)) {
            snprintf(tmp, sizeof(tmp), "%s/%s", dirn, de->d_name);
            stat(tmp, &s);
        }
        printf("%s %2ju %8jd %s\n", modestr(s.st_mode), s.st_nlink,
               (intmax_t)s.st_size, de->d_name);
    }
    if (errno != 0) {
        perror("readdir");
    }
    closedir(dir);
    return 0;
}

int zxc_list(int argc, char** argv) {
    char line[1024];
    FILE* fp;
    int num = 1;

    if (argc != 2) {
        printf("usage: list <filename>\n");
        return -1;
    }

    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    while (fgets(line, 1024, fp) != NULL) {
        printf("%5d | %s", num, line);
        num++;
    }
    fclose(fp);
    return 0;
}

static bool file_exists(const char *filename)
{
    struct stat statbuf;
    return stat(filename, &statbuf) == 0;
}

static bool verify_file(bool is_mv, const char *filename)
{
    struct stat statbuf;

    if (stat(filename, &statbuf) != 0) {
        fprintf(stderr, "%s: Unable to stat %s\n", is_mv ? "mv" : "cp",
                filename);
        return false;
    }

    if (!is_mv && S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "cp: Recursive copy not supported\n");
        return false;
    }

    return true;
}

// Copy into the destination location, which is not a directory
static int cp_here(const char *src_name, const char *dest_name,
                   bool dest_exists, bool force)
{
    if (! verify_file(false, src_name)) {
        return -1;
    }

    char data[4096];
    int fdi = -1, fdo = -1;
    int r, wr;
    int count = 0;
    if ((fdi = open(src_name, O_RDONLY)) < 0) {
        fprintf(stderr, "cp: cannot open '%s'\n", src_name);
        return fdi;
    }
    if ((fdo = open(dest_name, O_WRONLY | O_CREAT)) < 0) {
        if (! force ||
            unlink(dest_name) != 0 ||
            (fdo = open(dest_name, O_WRONLY | O_CREAT)) < 0) {
            fprintf(stderr, "cp: cannot open '%s'\n", dest_name);
            close(fdi);
            return fdo;
        }
    }
    for (;;) {
        if ((r = read(fdi, data, sizeof(data))) < 0) {
            fprintf(stderr, "cp: failed reading from '%s'\n", src_name);
            break;
        }
        if (r == 0) {
            break;
        }
        if ((wr = write(fdo, data, r)) != r) {
            fprintf(stderr, "cp: failed writing to '%s'\n", dest_name);
            r = wr;
            break;
        }
        count += r;
    }
    close(fdi);
    close(fdo);
    return r;
}

// Move into the destination location, which is not a directory
static int mv_here(const char *src_name, const char *dest_name,
                   bool dest_exists, bool force)
{
    if (! verify_file(true, src_name)) {
        return -1;
    }

    if (rename(src_name, dest_name)) {
        if (! force ||
            unlink(dest_name) != 0 ||
            rename(src_name, dest_name)) {
            fprintf(stderr, "mv: failed to create '%s'\n", dest_name);
            return -1;
        }
  }
  return 0;
}

// Copy a source file into the destination location, which is a directory
static int mv_or_cp_to_dir(bool is_mv, const char *src_name,
                           const char *dest_name, bool force)
{
    if (! verify_file(is_mv, src_name)) {
        return -1;
    }

    const char *filename_start = strrchr(src_name, '/');
    if (filename_start == NULL) {
        filename_start = src_name;
    } else {
        filename_start++;
        if (*filename_start == '\0') {
            fprintf(stderr, "%s: Invalid filename \"%s\"\n",
                    is_mv ? "mv" : "cp", src_name);
            return -1;
        }
    }

    size_t path_len = strlen(dest_name);
    if (path_len == 0) {
        fprintf(stderr, "%s: Invalid filename \"%s\"\n", is_mv ? "mv" : "cp",
                dest_name);
        return -1;
    }
    char full_filename[PATH_MAX];
    if (dest_name[path_len - 1] == '/') {
        snprintf(full_filename, PATH_MAX, "%s%s", dest_name, filename_start);
    } else {
        snprintf(full_filename, PATH_MAX, "%s/%s", dest_name, filename_start);
    }
    if (is_mv) {
        return mv_here(src_name, full_filename, file_exists(full_filename),
                       force);
    } else {
        return cp_here(src_name, full_filename, file_exists(full_filename),
                       force);
    }
}

int zxc_mv_or_cp(int argc, char** argv) {
    bool is_mv = !strcmp(argv[0], "mv");
    int next_arg = 1;
    bool force = false;
    while ((next_arg < argc) && argv[next_arg][0] == '-') {
        char *next_opt_char = &argv[next_arg][1];
        if (*next_opt_char == '\0') {
            goto usage;
        }
        do {
            switch (*next_opt_char) {
            case 'f':
                force = true;
                break;
            default:
                goto usage;
            }
            next_opt_char++;
        } while (*next_opt_char);
        next_arg++;
    }

    // Make sure we have at least 2 non-option arguments
    int src_count = (argc - 1) - next_arg;
    if (src_count <= 0) {
        goto usage;
    }

    const char *dest_name = argv[argc - 1];
    bool dest_exists = false;
    bool dest_isdir = false;
    struct stat statbuf;

    if (stat(dest_name, &statbuf) == 0) {
        dest_exists = true;
        if (S_ISDIR(statbuf.st_mode)) {
            dest_isdir = true;
        }
    }

    if (dest_isdir) {
        do {
            int result = mv_or_cp_to_dir(is_mv, argv[next_arg], dest_name,
                                         force);
            if (result != 0) {
                return result;
            }
            next_arg++;
        } while (next_arg < argc - 1);
        return 0;
    } else if (src_count > 1) {
        fprintf(stderr, "%s: destination is not a directory\n", argv[0]);
        return -1;
    } else if (is_mv) {
        return mv_here(argv[next_arg], dest_name, dest_exists, force);
    } else {
        return cp_here(argv[next_arg], dest_name, dest_exists, force);
    }

usage:
    fprintf(stderr, "usage: %s [-f] <src>... <dst>\n", argv[0]);
    return -1;
}

int zxc_mkdir(int argc, char** argv) {
    // skip "mkdir"
    argc--;
    argv++;
    bool parents = false;
    if (argc < 1) {
        fprintf(stderr, "usage: mkdir <path>\n");
        return -1;
    }
    if (!strcmp(argv[0], "-p")) {
        parents = true;
        argc--;
        argv++;
    }
    while (argc > 0) {
        char* dir = argv[0];
        if (parents) {
            for (size_t slash = 0u; dir[slash]; slash++) {
                if (slash != 0u && dir[slash] == '/') {
                    dir[slash] = '\0';
                    if (mkdir(dir, 0755) && errno != EEXIST) {
                        fprintf(stderr, "error: failed to make directory '%s'\n", dir);
                        return 0;
                    }
                    dir[slash] = '/';
                }
            }
        }
        if (mkdir(dir, 0755) && !(parents && errno == EEXIST)) {
            fprintf(stderr, "error: failed to make directory '%s'\n", dir);
        }
        argc--;
        argv++;
    }
    return 0;
}

static int zxc_rm_recursive(int atfd, char* path, bool force) {
    struct stat st;
    if (fstatat(atfd, path, &st, 0)) {
        return force ? 0 : -1;
    }
    if (S_ISDIR(st.st_mode)) {
        int dfd = openat(atfd, path, 0, O_RDONLY | O_DIRECTORY);
        if (dfd < 0) {
            return -1;
        }
        DIR* dir = fdopendir(dfd);
        if (!dir) {
            close(dfd);
            return -1;
        }
        struct dirent* de;
        while ((de = readdir(dir)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }
            if (zxc_rm_recursive(dfd, de->d_name, force) < 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
    }
    if (unlinkat(atfd, path, 0)) {
        return -1;
    } else {
        return 0;
    }
}

int zxc_rm(int argc, char** argv) {
    // skip "rm"
    argc--;
    argv++;
    bool recursive = false;
    bool force = false;
    while (argc >= 1 && argv[0][0] == '-') {
        char *args = &argv[0][1];
        if (*args == '\0') {
            goto usage;
        }
        do {
            switch (*args) {
            case 'r':
            case 'R':
                recursive = true;
                break;
            case 'f':
                force = true;
                break;
            default:
                goto usage;
            }
            args++;
        } while (*args != '\0');
        argc--;
        argv++;
    }
    if (argc < 1) {
        goto usage;
    }

    while (argc-- > 0) {
        if (recursive) {
            if (zxc_rm_recursive(AT_FDCWD, argv[0], force)) {
                goto err;
            }
        } else {
            if (unlink(argv[0])) {
                if (errno != ENOENT || !force) {
                    goto err;
                }
            }
        }
        argv++;
    }

    return 0;
err:
    fprintf(stderr, "error: failed to delete '%s'\n", argv[0]);
    return -1;
usage:
    fprintf(stderr, "usage: rm [-frR]... <filename>...\n");
    return -1;
}

//TODO(edcoyne): move "dm" command to its own file.
static int print_dm_help() {
    printf("dump                 - dump device tree\n"
           "poweroff             - power off the system\n"
           "shutdown             - power off the system\n"
           "suspend              - suspend the system to RAM\n"
           "reboot               - reboot the system\n"
           "reboot-bootloader/rb - reboot the system into bootloader\n"
           "reboot-recovery/rr   - reboot the system into recovery\n"
           "kerneldebug          - send a command to the kernel\n"
           "ktraceoff            - stop kernel tracing\n"
           "ktraceon             - start kernel tracing\n"
           "devprops             - dump published devices and their binding properties\n"
           "drivers              - list discovered drivers and their properties\n");
    return 0;
}

static const uint32_t kVmoBufferSize = 512*1024;

typedef struct {
    zx_handle_t vmo;
    zx_handle_t vmo_copy;
    size_t bytes_in_buffer;
    size_t bytes_available_on_service;
} VmoBuffer;

static zx_status_t initialize_vmo_buffer(VmoBuffer* buffer) {
    buffer->bytes_in_buffer = 0;
    buffer->bytes_available_on_service = 0;

    zx_status_t status = zx_vmo_create(kVmoBufferSize, 0, &buffer->vmo);
    if (status != ZX_OK) {
        return status;
    }

    status = zx_handle_duplicate(buffer->vmo, ZX_RIGHTS_IO | ZX_RIGHT_TRANSFER, &buffer->vmo_copy);
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

static zx_status_t print_vmo_buffer(VmoBuffer* buffer) {
    // It is possible this will be larger than kVmoBufferSize so we dynamically
    // allocate memory for buffer.
    char* to_print = (char*)malloc(buffer->bytes_in_buffer);
    if (to_print == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = zx_vmo_read(buffer->vmo, to_print, 0, buffer->bytes_in_buffer);
    if (status == ZX_OK) {
        size_t written = 0;
        while (written < buffer->bytes_in_buffer) {
            ssize_t count = write(STDOUT_FILENO, to_print + written,
                                  buffer->bytes_in_buffer - written);
            if (count < 0) {
                break;
            }
            written += count;
        }
    } else {
      printf("Call to service failed, status: %d\n", status);
    }

    if (buffer->bytes_in_buffer < buffer->bytes_available_on_service) {
      printf("\n-- OUTPUT TRUNCATED; %zu bytes available, %u buffer size --\n",
             buffer->bytes_available_on_service, kVmoBufferSize);
    }

    free(to_print);
    return ZX_OK;
}

static zx_status_t connect_to_service(const char* service, zx_handle_t* channel) {
    zx_handle_t channel_local, channel_remote;
    zx_status_t status = zx_channel_create(0, &channel_local, &channel_remote);
    if (status != ZX_OK) {
        fprintf(stderr, "failed to create channel: %d\n", status);
        return ZX_ERR_INTERNAL;
    }

    status = fdio_service_connect(service, channel_remote);
    if (status != ZX_OK) {
        zx_handle_close(channel_local);
        fprintf(stderr, "failed to connect to service: %d\n", status);
        return ZX_ERR_INTERNAL;
    }

    *channel = channel_local;
    return ZX_OK;

}

static int send_kernel_debug_command(const char* command, size_t length) {
    if (length > fuchsia_kernel_DEBUG_COMMAND_MAX) {
        fprintf(stderr, "error: kernel debug command longer than %u bytes: '%.*s'\n",
                fuchsia_kernel_DEBUG_COMMAND_MAX, (int)length, command);
        return -1;
    }

    zx_handle_t channel;
    zx_status_t status = connect_to_service("/svc/fuchsia.kernel.DebugBroker", &channel);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t call_status;
    status = fuchsia_kernel_DebugBrokerSendDebugCommand(channel, command, length, &call_status);
    zx_handle_close(channel);
    if (status != ZX_OK || call_status != ZX_OK) {
        return -1;
    }

    return 0;
}

static int send_kernel_tracing_enabled(bool enabled) {
    zx_handle_t channel;
    zx_status_t status = connect_to_service("/svc/fuchsia.kernel.DebugBroker", &channel);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t call_status;
    status = fuchsia_kernel_DebugBrokerSetTracingEnabled(channel, enabled, &call_status);
    zx_handle_close(channel);
    if (status != ZX_OK || call_status != ZX_OK) {
        return -1;
    }

    return 0;
}

static int send_dump(zx_status_t(*fidl_call)(zx_handle_t, zx_handle_t,
                                             zx_status_t*, size_t*, size_t*)) {
    zx_handle_t channel;
    zx_status_t status = connect_to_service("/svc/fuchsia.device.manager.DebugDumper", &channel);
    if (status != ZX_OK) {
        return status;
    }

    VmoBuffer buffer;
    status = initialize_vmo_buffer(&buffer);
    if (status != ZX_OK) {
        return status;
    }

    zx_status_t call_status;
    status = fidl_call(channel, buffer.vmo_copy,
                       &call_status, &buffer.bytes_in_buffer, &buffer.bytes_available_on_service);

    zx_handle_close(channel);
    if (status != ZX_OK || call_status != ZX_OK) {
        return -1;
    }

    status = print_vmo_buffer(&buffer);
    if (zx_handle_close(buffer.vmo) != ZX_OK) {
        printf("Failed to close vmo handle.\n");
    }
    return status;
}

typedef struct admin_resp {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_xunion_t data;
    zx_status_t err;
} admin_resp_t;

// Define a function to call to a method in fuchsia.hardware.power.statecontrol.Admin that does
// _not_ take a parameter.
#define DEFINE_PARAMETERLESS_ADMIN_CALL(name)                                                    \
  static int send_##name() {                                                                     \
    zx_handle_t channel;                                                                         \
    zx_status_t status =                                                                         \
        connect_to_service("/svc/fuchsia.hardware.power.statecontrol.Admin", &channel);          \
                                                                                                 \
    if (status != ZX_OK) {                                                                       \
      return status;                                                                             \
    }                                                                                            \
                                                                                                 \
    alignas(FIDL_ALIGNMENT) fuchsia_hardware_power_statecontrol_Admin##name##Request msg;        \
    fidl_init_txn_header(&msg.hdr, 0, fuchsia_hardware_power_statecontrol_Admin##name##Ordinal); \
                                                                                                 \
    const char* errs;                                                                            \
    void* msg_handle = (void*)&msg;                                                              \
    uint32_t actual_handle_count;                                                                \
                                                                                                 \
    zx_status_t call_status =                                                                    \
        fidl_encode(&fuchsia_hardware_power_statecontrol_Admin##name##RequestTable, msg_handle,  \
                    sizeof(msg), NULL, 0, &actual_handle_count, &errs);                          \
    if (call_status != ZX_OK) {                                                                  \
      return -1;                                                                                 \
    }                                                                                            \
                                                                                                 \
    admin_resp_t resp;                                                                           \
    zx_channel_call_args_t call_args;                                                            \
    call_args.wr_bytes = msg_handle;                                                             \
    call_args.wr_handles = NULL;                                                                 \
    call_args.rd_bytes = (void*)&resp;                                                           \
    call_args.rd_handles = NULL;                                                                 \
    call_args.wr_num_bytes = sizeof(fuchsia_hardware_power_statecontrol_Admin##name##Request);   \
    call_args.wr_num_handles = 0;                                                                \
    call_args.rd_num_bytes = sizeof(resp);                                                       \
    call_args.rd_num_handles = 0;                                                                \
    uint32_t bytes_rx;                                                                           \
    uint32_t handles_rx;                                                                         \
    call_status =                                                                                \
        zx_channel_call(channel, 0, ZX_TIME_INFINITE, &call_args, &bytes_rx, &handles_rx);       \
    if (call_status != ZX_OK) {                                                                  \
      printf("send_" #name ": zx_channel_call failed: %i\n", call_status);                       \
      return -1;                                                                                 \
    }                                                                                            \
                                                                                                 \
    switch (resp.data.tag) {                                                                     \
      case 1:                                                                                    \
        /* success! */                                                                           \
        break;                                                                                   \
      case 2:                                                                                    \
        /* service returned an error */                                                          \
        printf("send_" #name ": Failure with error: %s\n", zx_status_get_string(resp.err));      \
        break;                                                                                   \
      default:                                                                                   \
        printf("send_" #name ": Unexpected service response code: %lu\n", resp.data.tag);        \
        break;                                                                                   \
    }                                                                                            \
                                                                                                 \
    return 0;                                                                                    \
  }

DEFINE_PARAMETERLESS_ADMIN_CALL(Poweroff);
DEFINE_PARAMETERLESS_ADMIN_CALL(RebootToRecovery);
DEFINE_PARAMETERLESS_ADMIN_CALL(RebootToBootloader);
DEFINE_PARAMETERLESS_ADMIN_CALL(SuspendToRam);

static int send_Reboot() {
    zx_handle_t channel;
    zx_status_t status =
        connect_to_service("/svc/fuchsia.hardware.power.statecontrol.Admin", &channel);

    if (status != ZX_OK) {
        return status;
    }

    alignas(FIDL_ALIGNMENT) fuchsia_hardware_power_statecontrol_AdminRebootRequest msg;
    fidl_init_txn_header(&msg.hdr, 0, fuchsia_hardware_power_statecontrol_AdminRebootOrdinal);
    msg.reason = fuchsia_hardware_power_statecontrol_RebootReason_USER_REQUEST;

    const char* errs;
    void* msg_handle = (void*)&msg;
    uint32_t actual_handle_count;

    zx_status_t call_status =
        fidl_encode(&fuchsia_hardware_power_statecontrol_AdminRebootRequestTable, msg_handle,
                    sizeof(msg), NULL, 0, &actual_handle_count, &errs);
    if (call_status != ZX_OK) {
        return -1;
    }

    admin_resp_t resp;
    zx_channel_call_args_t call_args;
    call_args.wr_bytes = msg_handle;
    call_args.wr_handles = NULL;
    call_args.rd_bytes = (void*) &resp;
    call_args.rd_handles = NULL;
    call_args.wr_num_bytes = sizeof(fuchsia_hardware_power_statecontrol_AdminRebootRequest);
    call_args.wr_num_handles = 0;
    call_args.rd_num_bytes = sizeof(resp);
    call_args.rd_num_handles = 0;
    uint32_t bytes_rx;
    uint32_t handles_rx;
    call_status = zx_channel_call(channel, 0, ZX_TIME_INFINITE, &call_args, &bytes_rx,
                                  &handles_rx);
    if (call_status != ZX_OK) {
        printf("send_Reboot: zx_channel_call failed: %i\n", call_status);
        return -1;
    }

    switch (resp.data.tag) {
        case 1:
            // success!
            break;
        case 2:
            // service returned an error
            printf("send_Reboot: Failure with error: %s\n", zx_status_get_string(resp.err));
            break;
        default:
            printf("send_Reboot: Unexpected service response code: %lu", resp.data.tag);
            break;
    }

    return 0;
}

static bool command_cmp(const char* long_command, const char* short_command, const char* input,
                        int* command_length) {
  const size_t input_length = strlen(input);

  // Ensure that the first command_length chars of input match and that it is
  // either the whole input or there is a space after the command, we don't want
  // partial command matching.
  if (short_command) {
    const size_t short_length = strlen(short_command);
    if (input_length >= short_length && strncmp(short_command, input, short_length) == 0 &&
        ((input_length == short_length) || input[short_length] == ' ')) {
      *command_length = short_length;
      return true;
    }
  }

  const size_t long_length = strlen(long_command);
  if (input_length >= long_length && strncmp(long_command, input, long_length) == 0 &&
             ((input_length == long_length) || input[long_length] == ' ')) {
    *command_length = long_length;
    return true;
  }
  return false;
}

int zxc_dm(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: dm <command>\n");
        return -1;
    }

    // Handle service backed commands.
    int command_length = 0;
    if (command_cmp("kerneldebug", NULL, argv[1], &command_length)) {
        return send_kernel_debug_command(argv[1] + command_length,
                                         strlen(argv[1]) - command_length);
    } else if (command_cmp("ktraceon", NULL, argv[1], &command_length)) {
        return send_kernel_tracing_enabled(true);

    } else if (command_cmp("ktraceoff", NULL, argv[1], &command_length)) {
        return send_kernel_tracing_enabled(false);

    } else if (command_cmp("help", NULL, argv[1], &command_length)) {
        return print_dm_help();

    } else if (command_cmp("dump", NULL, argv[1], &command_length)) {
        return send_dump(fuchsia_device_manager_DebugDumperDumpTree);

    } else if (command_cmp("drivers", NULL, argv[1], &command_length)) {
        return send_dump(fuchsia_device_manager_DebugDumperDumpDrivers);

    } else if (command_cmp("devprops", NULL, argv[1], &command_length)) {
        return send_dump(fuchsia_device_manager_DebugDumperDumpBindingProperties);

    } else if (command_cmp("reboot", NULL, argv[1], &command_length)) {
        return send_Reboot();

    } else if (command_cmp("reboot-bootloader", "rb", argv[1], &command_length)) {
        return send_RebootToBootloader();

    } else if (command_cmp("reboot-recovery", "rr", argv[1], &command_length)) {
        return send_RebootToRecovery();

    } else if (command_cmp("suspend", NULL, argv[1], &command_length)) {
        return send_SuspendToRam();

    } else if (command_cmp("poweroff", NULL, argv[1], &command_length) ||
               command_cmp("shutdown", NULL, argv[1], &command_length)) {
        return send_Poweroff();

    } else {
        printf("Unknown command '%s'\n\n", argv[1]);
        printf("Valid commands:\n");
        print_dm_help();
    }

    return -1;
}

static char* join(char* buffer, size_t buffer_length, int argc, char** argv) {
    size_t total_length = 0u;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            if (total_length + 1 > buffer_length)
                return NULL;
            buffer[total_length++] = ' ';
        }
        const char* arg = argv[i];
        size_t arg_length = strlen(arg);
        if (total_length + arg_length + 1 > buffer_length)
            return NULL;
        strncpy(buffer + total_length, arg, buffer_length - total_length - 1);
        total_length += arg_length;
    }
    return buffer + total_length;
}

int zxc_k(int argc, char** argv) {
    if (argc <= 1) {
        printf("usage: k <command>\n");
        return -1;
    }

    char buffer[256];
    size_t command_length = 0u;

    // If we detect someone trying to use the LK poweroff/reboot,
    // divert it to devmgr backed one instead.
    if (!strcmp(argv[1], "poweroff") || !strcmp(argv[1], "reboot")
        || !strcmp(argv[1], "reboot-bootloader")) {
        return zxc_dm(argc, argv);
    }

    char* command_end = join(buffer, sizeof(buffer), argc - 1, &argv[1]);
    if (!command_end) {
        fprintf(stderr, "error: kernel debug command too long\n");
        return -1;
    }
    command_length = command_end - buffer;

    return send_kernel_debug_command(buffer, command_length);
}
