// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
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

#include <hexdump/hexdump.h>
#include <magenta/syscalls.h>

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
        mx_nanosleep(MX_MSEC(atoi(argv[1])));
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
        printf("%s %8jd %s\n", modestr(s.st_mode), (intmax_t)s.st_size, de->d_name);
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
        printf("error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    while (fgets(line, 1024, fp) != NULL) {
        printf("%5d | %s", num, line);
        num++;
    }
    fclose(fp);
    return 0;
}

int mxc_cp(int argc, char** argv) {
    char data[4096];
    int fdi = -1, fdo = -1;
    int r, wr;
    int count = 0;
    if (argc != 3) {
        fprintf(stderr, "usage: cp <srcfile> <dstfile>\n");
        return -1;
    }
    if ((fdi = open(argv[1], O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    if ((fdo = open(argv[2], O_WRONLY | O_CREAT)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[2]);
        r = fdo;
        goto done;
    }
    for (;;) {
        if ((r = read(fdi, data, sizeof(data))) < 0) {
            fprintf(stderr, "error: failed reading from '%s'\n", argv[1]);
            break;
        }
        if (r == 0) {
            break;
        }
        if ((wr = write(fdo, data, r)) != r) {
            fprintf(stderr, "error: failed writing to '%s'\n", argv[2]);
            r = wr;
            break;
        }
        count += r;
    }
    fprintf(stderr, "[copied %d bytes]\n", count);
done:
    close(fdi);
    close(fdo);
    return r;
}

int mxc_mkdir(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mkdir <path>\n");
        return -1;
    }
    while (argc > 1) {
        argc--;
        argv++;
        if (mkdir(argv[0], 0755)) {
            fprintf(stderr, "error: failed to make directory '%s'\n", argv[0]);
        }
    }
    return 0;
}

int mxc_mv(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: mv <old path> <new path>\n");
        return -1;
    }
    if (rename(argv[1], argv[2])) {
        fprintf(stderr, "error: failed to rename '%s' to '%s'\n", argv[1], argv[2]);
    }
    return 0;
}

static int mxc_rm_recursive(int atfd, char* path) {
    struct stat st;
    if (fstatat(atfd, path, &st, 0)) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        int dfd = openat(atfd, path, 0, O_DIRECTORY | O_RDWR);
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
            if (mxc_rm_recursive(dfd, de->d_name) < 0) {
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
    bool recursive = false;
    if (argc > 1) {
        if (!strcmp(argv[1], "-r")) {
            recursive = true;
            argc--;
            argv++;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "usage: rm [-r] <filename>\n");
        return -1;
    }
    if (recursive) {
        if (mxc_rm_recursive(AT_FDCWD, argv[0])) {
            goto err;
        }
    } else {
        if (unlink(argv[0])) {
            goto err;
        }
    }
    return 0;
err:
    fprintf(stderr, "error: failed to delete '%s'\n", argv[0]);
    return -1;
}

int mxc_dm(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: dm <command>\n");
        return -1;
    }
    int fd = open("/dev/class/misc/dmctl", O_RDWR);
    if (fd >= 0) {
        int r = write(fd, argv[1], strlen(argv[1]));
        if (r < 0) {
            fprintf(stderr, "error: cannot write dmctl: %d\n", r);
        }
        close(fd);
        return r;
    } else {
        fprintf(stderr, "error: cannot open dmctl: %d\n", fd);
        return fd;
    }
}

