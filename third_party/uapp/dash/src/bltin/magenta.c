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

#include <magenta/syscalls.h>
#include <pretty/hexdump.h>

#include <magenta/device/dmctl.h>

int mxc_dump(int argc, char** argv) {
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

int mxc_msleep(int argc, char** argv) {
    if (argc == 2) {
        mx_nanosleep(mx_deadline_after(MX_MSEC(atoi(argv[1]))));
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

int mxc_ls(int argc, char** argv) {
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
    while((de = readdir(dir)) != NULL) {
        memset(&s, 0, sizeof(struct stat));
        if ((strlen(de->d_name) + dirln + 2) <= sizeof(tmp)) {
            snprintf(tmp, sizeof(tmp), "%s/%s", dirn, de->d_name);
            stat(tmp, &s);
        }
        printf("%s %2ju %8jd %s\n", modestr(s.st_mode), s.st_nlink,
               (intmax_t)s.st_size, de->d_name);
    }
    closedir(dir);
    return 0;
}

int mxc_list(int argc, char** argv) {
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
done:
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

int mxc_mv_or_cp(int argc, char** argv) {
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

int mxc_mkdir(int argc, char** argv) {
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

static int mxc_rm_recursive(int atfd, char* path, bool force) {
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
            if (mxc_rm_recursive(dfd, de->d_name, force) < 0) {
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

int mxc_rm(int argc, char** argv) {
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
            if (mxc_rm_recursive(AT_FDCWD, argv[0], force)) {
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

static int send_dmctl(const char* command, size_t length) {
    int fd = open("/dev/misc/dmctl", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open dmctl: %d\n", fd);
        return fd;
    }

    // commands with ':' get passed through and don't use
    // socket for results (since there are none)
    const char* p;
    for (p = command; p < (command + length); p++) {
        if (*p == ':') {
            write(fd, command, length);
            return 0;
        }
    }

    dmctl_cmd_t cmd;
    if (length >= sizeof(cmd.name)) {
        fprintf(stderr, "error: dmctl command longer than %zu bytes: '%.*s'\n",
                sizeof(cmd.name), (int)length, command);
        return -1;
    }
    snprintf(cmd.name, sizeof(cmd.name), command);

    mx_handle_t h;
    if (mx_socket_create(0, &cmd.h, &h) < 0) {
        return -1;
    }

    int r = ioctl_dmctl_command(fd, &cmd);
    close(fd);
    if (r < 0) {
        mx_handle_close(h);
        return r;
    }

    for (;;) {
        mx_status_t status;
        char buf[32768];
        size_t actual;
        if ((status = mx_socket_read(h, 0, buf, sizeof(buf), &actual)) < 0) {
            if (status == MX_ERR_SHOULD_WAIT) {
                mx_object_wait_one(h, MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                   MX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }
        size_t written = 0;
        while (written < actual) {
            ssize_t count = write(1, buf + written, actual - written);
            if (count < 0) {
                break;
            } else {
                written += count;
            }
        }
    }
    mx_handle_close(h);

    return 0;
}

int mxc_dm(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: dm <command>\n");
        return -1;
    }
    return send_dmctl(argv[1], strlen(argv[1]));
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

int mxc_k(int argc, char** argv) {
    if (argc <= 1) {
        printf("usage: k <command>\n");
        return -1;
    }

    const char* prefix = "kerneldebug ";
    char buffer[256];

    size_t command_length = 0u;
    // If we detect someone trying to use the LK poweroff/reboot,
    // divert it to devmgr backed one instead.
    if (!strcmp(argv[1], "poweroff") || !strcmp(argv[1], "reboot")) {
        strcpy(buffer, argv[1]);
        command_length = strlen(buffer);
    } else {
        strcpy(buffer, prefix);
        size_t prefix_length = strlen(prefix);
        char* command_end = join(buffer + prefix_length, sizeof(buffer) - prefix_length, argc - 1, &argv[1]);
        if (!command_end) {
            fprintf(stderr, "error: kernel debug command too long\n");
            return -1;
        }
        command_length = command_end - buffer;
    }

    return send_dmctl(buffer, command_length);
}
