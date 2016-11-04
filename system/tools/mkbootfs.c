// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <lz4frame.h>

#include <magenta/bootdata.h>

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

int import_manifest_entry(const char *fn, int lineno, const char *dst, const char *src, fsentry **entry) {
    fsentry *e;
    struct stat s;

    if (dst[0] == 0) {
        fprintf(stderr, "%s:%d: illegal filename\n", fn, lineno);
        return -1;
    }
    if (stat(src, &s) < 0) {
        fprintf(stderr, "%s:%d: cannot stat '%s'\n", fn, lineno, src);
        return -1;
    }
    if (s.st_size > INT32_MAX) {
        fprintf(stderr, "%s:%d: file too large '%s'\n", fn, lineno, src);
        return -1;
    } else if (s.st_size == 0) {
        // TODO(tkilbourn): Add support for empty files, rather than dropping
        // them.
        fprintf(stderr, "Warning: %s:%d: file empty '%s'\n", fn, lineno, src);
        *entry = NULL;
        return 0;
    }

    if ((e = calloc(1, sizeof(*e))) == NULL) return -1;
    if ((e->name = strdup(dst)) == NULL) goto fail;
    if ((e->srcpath = strdup(src)) == NULL) goto fail;
    e->namelen = strlen(e->name) + 1;
    e->length = s.st_size;
    *entry = e;
    return 0;
fail:
    free(e->name);
    free(e);
    return -1;
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
        if (import_manifest_entry(fn, lineno, dstfn, srcfn, &e)) {
            return -1;
        } else if (e != NULL) {
            sz += add_entry(fs, e);
        }
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

typedef struct {
    ssize_t (*copy_setup)(void* dst, void** cookie);
    ssize_t (*copy_data)(void* dst, const void* src, size_t len, void* cookie);
    ssize_t (*copy_file)(void* dst, const char* fn, size_t len, void* cookie);
    ssize_t (*copy_finish)(void* dst, void* cookie);
} copy_ops;

ssize_t copydata(void* dst, const void* src, size_t len, void* cookie) {
    memcpy(dst, src, len);
    return len;
}

ssize_t copyfile(void* dst, const char *fn, size_t len, void* cookie) {
    char buf[4*1024*1024];
    int r, fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
    size_t remaining = len;
    while (remaining > 0) {
        r = read(fdi, buf, sizeof(buf));
        if (r < 0) {
            fprintf(stderr, "error: failed reading '%s'\n", fn);
            goto oops;
        }
        if ((r == 0) || (r > remaining)) {
            fprintf(stderr, "error: file '%s' changed size!\n", fn);
            goto oops;
        }
        memcpy(dst, buf, r);
        dst += r;
        remaining -= r;
    }
    close(fdi);
    return len;
oops:
    close(fdi);
    return -1;
}

static const copy_ops copy_passthrough = {
    .copy_data = copydata,
    .copy_file = copyfile,
};

static LZ4F_preferences_t lz4_prefs = {
    .frameInfo = {
        .blockSizeID = LZ4F_max64KB,
        .blockMode = LZ4F_blockIndependent,
    },
    // LZ4 compression levels 1-3 are for "fast" compression, and 4-16 are for
    // higher compression. The additional compression going from 4 to 16 is not
    // worth the extra time needed during compression.
    .compressionLevel = 4,
};

static bool check_and_log_lz4_error(LZ4F_errorCode_t code, const char* msg) {
    if (LZ4F_isError(code)) {
        fprintf(stderr, "%s: %s\n", msg, LZ4F_getErrorName(code));
        return true;
    }
    return false;
}

ssize_t compress_setup(void* dst, void** cookie) {
    LZ4F_compressionContext_t cctx;
    LZ4F_errorCode_t errc = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
    if (check_and_log_lz4_error(errc, "could not initialize compression context")) {
        return -1;
    }
    size_t wrote = LZ4F_compressBegin(cctx, dst, 16, &lz4_prefs);
    if (check_and_log_lz4_error(wrote, "could not begin compression")) {
        return wrote;
    }
    // Note: LZ4F_compressionContext_t is a typedef to a pointer, so this is
    // "safe".
    *cookie = (void*)cctx;
    return wrote;
}

ssize_t compress_data(void* dst, const void* src, size_t len, void* cookie) {
    // Since we're compressing to an mmap'd file, we don't have to worry about
    // the max write size. But LZ4 still requires a valid size, so we just pass
    // in the size that it's looking for.
    size_t maxWrite = LZ4F_compressBound(len, &lz4_prefs);
    size_t wrote = LZ4F_compressUpdate((LZ4F_compressionContext_t)cookie, dst, maxWrite,
            src, len, NULL);
    check_and_log_lz4_error(wrote, "could not compress data");
    return wrote;
}

ssize_t compress_file(void* dst, const char* fn, size_t len, void* cookie) {
    int fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
    void* src = mmap(NULL, len, PROT_READ, MAP_SHARED, fdi, 0);
    if (src == MAP_FAILED) {
        fprintf(stderr, "error cannot map '%s'\n", fn);
        close(fdi);
        return -1;
    }
    ssize_t ret = compress_data(dst, src, len, cookie);
    munmap(src, len);
    close(fdi);
    return ret;
}

ssize_t compress_finish(void* dst, void* cookie) {
    // Max write is one block (64kB uncompressed) plus 8 bytes of footer.
    size_t maxWrite = LZ4F_compressBound(65536, &lz4_prefs) + 8;
    size_t wrote = LZ4F_compressEnd((LZ4F_compressionContext_t)cookie, dst, maxWrite, NULL);
    check_and_log_lz4_error(wrote, "could not finish compression");

    LZ4F_errorCode_t errc = LZ4F_freeCompressionContext((LZ4F_compressionContext_t)cookie);
    check_and_log_lz4_error(errc, "could not free compression context");
    return wrote;
}

static const copy_ops copy_compress = {
    .copy_setup = compress_setup,
    .copy_data = compress_data,
    .copy_file = compress_file,
    .copy_finish = compress_finish,
};

#define PAGEALIGN(n) (((n) + 4095) & (~4095))
#define PAGEFILL(n) (PAGEALIGN(n) - (n))

char fill[4096];

#define CHECK_WRITE(w) if ((w) < 0) goto fail

int export_userfs(const char *fn, fs *fs, unsigned hsz, uint64_t outsize, bool compressed) {
    uint32_t n;
    fsentry *e;
    int fd;
    const copy_ops* op = compressed ? &copy_compress : &copy_passthrough;

    fd = open(fn, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create '%s'\n", fn);
        return -1;
    }

    size_t dstsize = outsize;
    if (compressed) {
        // Update the LZ4 content size to be original size without the bootdata
        // header which isn't being compressed.
        lz4_prefs.frameInfo.contentSize = outsize - sizeof(bootdata_t);
        // Get an upperbound on the resulting compressed file size.
        dstsize = LZ4F_compressBound(outsize, &lz4_prefs);
    }
    ftruncate(fd, dstsize);

    void* dst_start = mmap(NULL, dstsize, PROT_WRITE, MAP_SHARED, fd, 0);
    if (dst_start == MAP_FAILED) {
        fprintf(stderr, "error: cannot map '%s' (err=%d)\n", fn, errno);
        goto fail2;
    }
    uint8_t* dst = dst_start;
    // Increment past the bootdata header which will be filled out later.
    ssize_t wrote = sizeof(bootdata_t);
    dst += wrote;

    void* cookie = NULL;
    if (op->copy_setup) {
        CHECK_WRITE(wrote = op->copy_setup(dst, &cookie));
        dst += wrote;
    }

    CHECK_WRITE(wrote = op->copy_data(dst, FSMAGIC, sizeof(FSMAGIC), cookie));
    dst += wrote;

    for (e = fs->first; e != NULL; e = e->next) {
        uint32_t hdr[3];
        hdr[0] = e->namelen;
        hdr[1] = e->length;
        hdr[2] = e->offset;
        CHECK_WRITE(wrote = op->copy_data(dst, hdr, sizeof(hdr), cookie));
        dst += wrote;
        CHECK_WRITE(wrote = op->copy_data(dst, e->name, e->namelen, cookie));
        dst += wrote;
    }

    // null terminator record
    CHECK_WRITE(wrote = op->copy_data(dst, fill, 12, cookie));
    dst += wrote;

    n = PAGEFILL(hsz);
    if (n) {
        CHECK_WRITE(wrote = op->copy_data(dst, fill, n, cookie));
        dst += wrote;
    }

    for (e = fs->first; e != NULL; e = e->next) {
        if (verbose) {
            fprintf(stderr, "%08x %08x %s\n", e->offset, e->length, e->name);
        }
        CHECK_WRITE(wrote = op->copy_file(dst, e->srcpath, e->length, cookie));
        dst += wrote;
        n = PAGEFILL(e->length);
        if (n) {
            CHECK_WRITE(wrote = op->copy_data(dst, fill, n, cookie));
            dst += wrote;
        }
    }

    if (op->copy_finish) {
        CHECK_WRITE(wrote = op->copy_finish(dst, cookie));
        dst += wrote;
    }

    // Find the final output size
    wrote = dst - (uint8_t*)dst_start;
    if (wrote > dstsize) {
        fprintf(stderr, "INTERNAL ERROR!! wrote %zd bytes > %zu bytes!\n",
                wrote, dstsize);
        goto fail;
    }

    // Write the bootheader
    dst = dst_start;
    bootdata_t boothdr = {
        .magic = BOOTDATA_MAGIC,
        .type = BOOTDATA_TYPE_BOOTFS,
        .insize = wrote,
        .outsize = compressed ? outsize : wrote,
        .flags = compressed ? BOOTDATA_BOOTFS_FLAG_COMPRESSED : 0
    };
    // Note: this is a memcpy rather than an op->copy_data, since it's written
    // outside the area that's potentially compressed.
    memcpy(dst, &boothdr, sizeof(boothdr));

    // Cleanup and set the output file to the final size.
    if (munmap(dst_start, dstsize) < 0) {
        fprintf(stderr, "error: failed to unmap '%s' (err=%d)\n", fn, errno);
        goto fail2;
    }
    if (ftruncate(fd, wrote) < 0) {
        fprintf(stderr, "error: could not resize '%s' (err=%d)\n", fn, errno);
        goto fail2;
    }
    close(fd);
    return 0;

fail:
    fprintf(stderr, "error: failed writing '%s'\n", fn);
    munmap(dst_start, dstsize);
fail2:
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
    bool compressed = false;

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
        } else if (!strcmp(cmd,"-c")) {
            compressed = true;
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

    // account for bootdata
    hsz += sizeof(bootdata_t);

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
    return export_userfs(output_file, &fs, hsz, off, compressed);
}
