// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int verbose = 0;

char FSMAGIC[16] = "[BOOTFS]\0\0\0\0\0\0\0\0";

// BOOTFS is a trivial "filesystem" format
//
// It has a 16 byte magic/version value (FSMAGIC)
// Followed by a series of records of:
//   namelength (32bit le)
//   filesize   (32bit le)
//   fileoffset (32bit le)
//   namedata   (namelength bytes, includes \0)
//
// - fileoffsets must be page aligned (multiple of 4096)

#define FSENTRYSZ 12

typedef struct fsentry fsentry;
struct fsentry {
    fsentry *next;

    char *name;
    size_t namelen;
    uint32_t offset;
    uint32_t length;

    char *srcpath;
};
typedef struct fs {
    fsentry *first;
    fsentry *last;
} fs;

char *trim(char *str) {
    char *end;
    while (isspace(*str)) {
        str++;
    }
    end = str + strlen(str);
    while (end > str) {
        end--;
        if (isspace(*end)) {
            *end = 0;
        } else {
            break;
        }
    }
    return str;
}

fsentry *import_manifest_entry(const char *fn, int lineno, const char *dst, const char *src) {
    fsentry *e;
    struct stat s;

    if (dst[0] == 0) {
        fprintf(stderr, "%s:%d: illegal filename\n", fn, lineno);
        return NULL;
    }
    if (stat(src, &s) < 0) {
        fprintf(stderr, "%s:%d: cannot stat '%s'\n", fn, lineno, src);
        return NULL;
    }
    if (s.st_size > INT32_MAX) {
        fprintf(stderr, "%s:%d: file too large '%s'\n", fn, lineno, src);
        return NULL;
    }

    if ((e = calloc(1, sizeof(*e))) == NULL) return NULL;
    if ((e->name = strdup(dst)) == NULL) goto fail;
    if ((e->srcpath = strdup(src)) == NULL) goto fail;
    e->namelen = strlen(e->name) + 1;
    e->length = s.st_size;
    return e;
fail:
    free(e->name);
    free(e);
    return NULL;
}

fsentry *import_directory_entry(const char *dst, const char *src, struct stat *s) {
    fsentry *e;

    if (s->st_size > INT32_MAX) {
        fprintf(stderr, "error: file too large '%s'\n", src);
        return NULL;
    }

    if ((e = calloc(1, sizeof(*e))) == NULL) return NULL;
    if ((e->name = strdup(dst)) == NULL) goto fail;
    if ((e->srcpath = strdup(src)) == NULL) goto fail;
    e->namelen = strlen(e->name) + 1;
    e->length = s->st_size;
    return e;
fail:
    free(e->name);
    free(e);
    return NULL;
}

unsigned add_entry(fs *fs, fsentry *e) {
    if (!strcmp(e->name, "bin/userboot")) {
        // userboot must be the first entry
        e->next = fs->first;
        fs->first = e;
        if (!fs->last) {
            fs->last = e;
        }
    } else {
        e->next = NULL;
        if (fs->last) {
            fs->last->next = e;
        } else {
            fs->first = e;
        }
        fs->last = e;
    }
    return e->namelen + FSENTRYSZ;
}

int import_manifest(const char *fn, unsigned *hdrsz, fs *fs) {
    unsigned sz = 0;
    int lineno = 0;
    fsentry *e;
    char *eq;
    char line[4096];
    FILE *fp;

    if ((fp = fopen(fn, "r")) == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;
        if ((eq = strchr(line, '=')) == NULL) {
            continue;
        }
        *eq++ = 0;
        char* dstfn = trim(line);
        char* srcfn = trim(eq);
        if ((e = import_manifest_entry(fn, lineno, dstfn, srcfn)) == NULL) {
            return -1;
        }
        sz += add_entry(fs, e);
    }
    fclose(fp);
    *hdrsz += sz;
    return 0;
}

int import_directory(const char *dpath, const char *spath, unsigned *hdrsz, fs *fs) {
#define MAX_BOOTFS_PATH_LEN 4096
    char dst[MAX_BOOTFS_PATH_LEN];
    char src[MAX_BOOTFS_PATH_LEN];
#undef MAX_BOOTFS_PATH_LEN
    struct stat s;
    unsigned sz = 0;
    struct dirent *de;
    DIR *dir;

    if ((dir = opendir(spath)) == NULL) {
        fprintf(stderr, "error: cannot open directory '%s'\n", spath);
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        char *name = de->d_name;
        if (name[0] == '.') {
            if (name[1] == 0) {
                continue;
            }
            if ((name[1] == '.') && (name[2] == 0)) {
                continue;
            }
        }
        if (snprintf(src, sizeof(src), "%s/%s", spath, name) > sizeof(src)) {
            fprintf(stderr, "error: name '%s/%s' is too long\n", spath, name);
            goto fail;
        }
        if (stat(src, &s) < 0) {
            fprintf(stderr, "error: cannot stat '%s'\n", src);
            goto fail;
        }
        if (S_ISREG(s.st_mode)) {
            fsentry *e;
            if (snprintf(dst, sizeof(dst), "%s%s", dpath, name) > sizeof(dst)) {
                fprintf(stderr, "error: name '%s%s' is too long\n", dpath, name);
                goto fail;
            }
            if ((e = import_directory_entry(dst, src, &s)) < 0) {
                goto fail;
            }
            sz += add_entry(fs, e);
        } else if (S_ISDIR(s.st_mode)) {
            if (snprintf(dst, sizeof(dst), "%s%s/", dpath, name) > sizeof(dst)) {
                fprintf(stderr, "error: name '%s%s/' is too long\n", dpath, name);
                goto fail;
            }
            import_directory(dst, src, hdrsz, fs);
        } else {
            fprintf(stderr, "error: unsupported filetype '%s'\n", src);
            goto fail;
        }
    }
    *hdrsz += sz;
    closedir(dir);
    return 0;
fail:
    closedir(dir);
    return -1;
}

int copydata(int fd, const char *fn, size_t len) {
    char buf[4*1024*1024];
    int r, fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
    while (len > 0) {
        r = read(fdi, buf, sizeof(buf));
        if (r < 0) {
            fprintf(stderr, "error: failed reading '%s'\n", fn);
            goto oops;
        }
        if ((r == 0) || (r > len)) {
            fprintf(stderr, "error: file '%s' changed size!\n", fn);
            goto oops;
        }
        if (write(fd, buf, r) != r) {
            fprintf(stderr, "error: failed writing '%s'\n", fn);
            goto oops;
        }
        len -= r;
    }
    close(fdi);
    return 0;
oops:
    close(fdi);
    return -1;
}

#define PAGEALIGN(n) (((n) + 4095) & (~4095))
#define PAGEFILL(n) (PAGEALIGN(n) - (n))

char fill[4096];

int export_userfs(const char *fn, fs *fs, unsigned hsz) {
    uint32_t n;
    fsentry *e;
    int fd;

    fd = open(fn, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create '%s'\n", fn);
        return -1;
    }

    if (write(fd, FSMAGIC, sizeof(FSMAGIC)) != sizeof(FSMAGIC)) goto ioerr;

    for (e = fs->first; e != NULL; e = e->next) {
        uint32_t hdr[3];
        hdr[0] = e->namelen;
        hdr[1] = e->length;
        hdr[2] = e->offset;
        if (write(fd, hdr, sizeof(hdr)) != sizeof(hdr)) goto ioerr;
        if (write(fd, e->name, e->namelen) != e->namelen) goto ioerr;
    }

    // null terminator record
    if (write(fd, fill, 12) != 12) goto ioerr;

    n = PAGEFILL(hsz);
    if (n) {
        if (write(fd, fill, n) != n) goto ioerr;
    }

    for (e = fs->first; e != NULL; e = e->next) {
        if (verbose) {
            fprintf(stderr, "%08x %08x %s\n", e->offset, e->length, e->name);
        }
        if (copydata(fd, e->srcpath, e->length)) {
            close(fd);
            return -1;
        }
        n = PAGEFILL(e->length);
        if (n) {
            if (write(fd, fill, n) != n) goto ioerr;
        }
    }
    close(fd);
    return 0;
ioerr:
    fprintf(stderr, "error: failed writing '%s'\n", fn);
    close(fd);
    return -1;
}

int main(int argc, char **argv) {
    const char *output_file = "user.bootfs";
    fs fs = { 0 };
    fsentry *e = NULL;
    int i;
    unsigned hsz = 0;
    uint64_t off;

    argc--;
    argv++;
    while (argc > 0) {
        const char *cmd = argv[0];
        if (cmd[0] != '-')
            break;
        if (!strcmp(cmd,"-v")) {
            verbose = 1;
        } else if (!strcmp(cmd,"-o")) {
            if (argc < 2) {
              fprintf(stderr, "no output file given\n");
              return -1;
            }
            output_file = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd,"-h")) {
            fprintf(stderr, "usage: mkbootfs [-v] [-o <fsimage>] <manifests>...\n");
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", cmd);
            return -1;
        }
        argc--;
        argv++;
    }
    if (argc < 1) {
        fprintf(stderr, "no manifest files given\n");
        return -1;
    }
    for (i = 0; i < argc; i++) {
        char *path = argv[i];
        if (path[0] == '@') {
            path++;
            int len = strlen(path);
            if (path[len - 1] == '/') {
                // remove trailing slash
                path[len - 1] = 0;
            }
            if (import_directory("", path, &hsz, &fs) < 0) {
                return -1;
            }
        } else if (import_manifest(path, &hsz, &fs) < 0) {
            return -1;
        }
    }

    // account for the magic
    hsz += sizeof(FSMAGIC);

    // account for the end-of-records record
    hsz += 12;

    off = PAGEALIGN(hsz);
    for (e = fs.first; e != NULL; e = e->next) {
        e->offset = off;
        off += PAGEALIGN(e->length);
        if (off > INT32_MAX) {
            fprintf(stderr, "error: userfs too large\n");
            return -1;
        }
    }
    return export_userfs(output_file, &fs, hsz);
}
