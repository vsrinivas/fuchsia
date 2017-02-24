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

#define MAXBUFFER (1024*1024)


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
    fsentry* next;

    char* name;
    size_t namelen;
    uint32_t offset;
    uint32_t length;

    char* srcpath;
};
typedef struct fs {
    fsentry* first;
    fsentry* last;
} fs;

char* trim(char* str) {
    char* end;
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

fsentry* import_manifest_entry(const char* fn, int lineno, const char* dst, const char* src) {
    fsentry* e;
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

fsentry* import_directory_entry(const char* dst, const char* src, struct stat* s) {
    fsentry* e;

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

unsigned add_entry(fs* fs, fsentry* e) {
    e->next = NULL;
    if (fs->last) {
        fs->last->next = e;
    } else {
        fs->first = e;
    }
    fs->last = e;
    return e->namelen + FSENTRYSZ;
}

int import_manifest(const char* fn, unsigned* hdrsz, fs* fs) {
    unsigned sz = 0;
    int lineno = 0;
    fsentry* e;
    char* eq;
    char line[4096];
    FILE* fp;

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

int import_directory(const char* dpath, const char* spath, unsigned* hdrsz, fs* fs) {
#define MAX_BOOTFS_PATH_LEN 4096
    char dst[MAX_BOOTFS_PATH_LEN];
    char src[MAX_BOOTFS_PATH_LEN];
#undef MAX_BOOTFS_PATH_LEN
    struct stat s;
    unsigned sz = 0;
    struct dirent* de;
    DIR* dir;

    if ((dir = opendir(spath)) == NULL) {
        fprintf(stderr, "error: cannot open directory '%s'\n", spath);
        return -1;
    }
    while ((de = readdir(dir)) != NULL) {
        char* name = de->d_name;
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
            fsentry* e;
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

static int readx(int fd, void* ptr, size_t len) {
    size_t total = len;
    while (len > 0) {
        ssize_t r = read(fd, ptr, len);
        if (r <= 0) {
            return -1;
        }
        ptr += r;
        len -= r;
    }
    return total;
}

static int writex(int fd, const void* ptr, size_t len) {
    size_t total = len;
    while (len > 0) {
        ssize_t r = write(fd, ptr, len);
        if (r <= 0) {
            return -1;
        }
        ptr += r;
        len -= r;
    }
    return total;
}

typedef struct {
    ssize_t (*setup)(int fd, void** cookie);
    ssize_t (*write)(int fd, const void* src, size_t len, void* cookie);
    ssize_t (*write_file)(int fd, const char* fn, size_t len, void* cookie);
    ssize_t (*finish)(int fd, void* cookie);
} io_ops;

ssize_t copydata(int fd, const void* src, size_t len, void* cookie) {
    if (writex(fd, src, len) < 0) {
        return -1;
    } else {
        return len;
    }
}

ssize_t copyfile(int fd, const char* fn, size_t len, void* cookie) {
    char buf[MAXBUFFER];
    int r, fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }

    r = 0;
    size_t total = len;
    while (len > 0) {
        size_t xfer = (len > sizeof(buf)) ? sizeof(buf) : len;
        if ((r = readx(fdi, buf, xfer)) < 0) {
            break;
        }
        if ((r = writex(fd, buf, xfer)) < 0) {
            break;
        }
        len -= xfer;
    }
    close(fdi);
    return (r < 0) ? r : total;
}

static const io_ops io_plain = {
    .write = copydata,
    .write_file = copyfile,
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

ssize_t compress_setup(int fd, void** cookie) {
    LZ4F_compressionContext_t cctx;
    LZ4F_errorCode_t errc = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
    if (check_and_log_lz4_error(errc, "could not initialize compression context")) {
        return -1;
    }
    uint8_t buf[128];
    size_t r = LZ4F_compressBegin(cctx, buf, sizeof(buf), &lz4_prefs);
    if (check_and_log_lz4_error(r, "could not begin compression")) {
        return r;
    }

    // Note: LZ4F_compressionContext_t is a typedef to a pointer, so this is
    // "safe".
    *cookie = (void*)cctx;

    return writex(fd, buf, r);
}

ssize_t compress_data(int fd, const void* src, size_t len, void* cookie) {
    // max will be, worst case, a bit larger than MAXBUFFER
    size_t max = LZ4F_compressBound(len, &lz4_prefs);
    uint8_t buf[max];
    size_t r = LZ4F_compressUpdate((LZ4F_compressionContext_t)cookie, buf, max, src, len, NULL);
    if (check_and_log_lz4_error(r, "could not compress data")) {
        return -1;
    }
    return writex(fd, buf, r);
}

ssize_t compress_file(int fd, const char* fn, size_t len, void* cookie) {
    if (len == 0) {
        // Don't bother trying to compress empty files
        return 0;
    }

    char buf[MAXBUFFER];
    int r, fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }

    r = 0;
    size_t total = len;
    while (len > 0) {
        size_t xfer = (len > sizeof(buf)) ? sizeof(buf) : len;
        if ((r = readx(fdi, buf, xfer)) < 0) {
            break;
        }
        if ((r = compress_data(fd, buf, xfer, cookie)) < 0) {
            break;
        }
        len -= xfer;
    }
    close(fdi);
    return (r < 0) ? -1 : total;
}

ssize_t compress_finish(int fd, void* cookie) {
    // Max write is one block (64kB uncompressed) plus 8 bytes of footer.
    size_t max = LZ4F_compressBound(65536, &lz4_prefs) + 8;
    uint8_t buf[max];
    size_t r = LZ4F_compressEnd((LZ4F_compressionContext_t)cookie, buf, max, NULL);
    if (check_and_log_lz4_error(r, "could not finish compression")) {
        r = -1;
    } else {
        r = writex(fd, buf, r);
    }

    LZ4F_errorCode_t errc = LZ4F_freeCompressionContext((LZ4F_compressionContext_t)cookie);
    if (check_and_log_lz4_error(errc, "could not free compression context")) {
        r = -1;
    }

    return r;
}

static const io_ops io_compressed = {
    .setup = compress_setup,
    .write = compress_data,
    .write_file = compress_file,
    .finish = compress_finish,
};

#define PAGEALIGN(n) (((n) + 4095) & (~4095))
#define PAGEFILL(n) (PAGEALIGN(n) - (n))

char fill[4096];

#define CHECK(w) do { if ((w) < 0) goto fail; } while (0)

int export_userfs(const char* fn, fs* fs, unsigned hsz, uint64_t outsize, bool compressed) {
    uint32_t n;
    fsentry* e;
    int fd;
    const io_ops* op = compressed ? &io_compressed : &io_plain;

    fd = open(fn, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create '%s'\n", fn);
        return -1;
    }

    if (compressed) {
        // Update the LZ4 content size to be original size without the bootdata
        // header which isn't being compressed.
        lz4_prefs.frameInfo.contentSize = outsize - sizeof(bootdata_t);
    }

    // Increment past the bootdata header which will be filled out later.
    if (lseek(fd, sizeof(bootdata_t), SEEK_SET) != sizeof(bootdata_t)) {
        fprintf(stderr, "error: cannot seek\n");
        goto fail;
    }

    void* cookie = NULL;
    if (op->setup) {
        CHECK(op->setup(fd, &cookie));
    }

    CHECK(op->write(fd, FSMAGIC, sizeof(FSMAGIC), cookie));

    fsentry* last_entry = NULL;
    for (e = fs->first; e != NULL; e = e->next) {
        uint32_t hdr[3];
        hdr[0] = e->namelen;
        hdr[1] = e->length;
        hdr[2] = e->offset;
        CHECK(op->write(fd, hdr, sizeof(hdr), cookie));
        CHECK(op->write(fd, e->name, e->namelen, cookie));
        last_entry = e;
    }
    // Record length of last file
    uint32_t last_length = last_entry ? last_entry->length : 0;

    // null terminator record
    CHECK(op->write(fd, fill, 12, cookie));

    if ((n = PAGEFILL(hsz))) {
        CHECK(op->write(fd, fill, n, cookie));
    }

    for (e = fs->first; e != NULL; e = e->next) {
        if (verbose) {
            fprintf(stderr, "%08x %08x %s\n", e->offset, e->length, e->name);
        }
        CHECK(op->write_file(fd, e->srcpath, e->length, cookie));
        if ((n = PAGEFILL(e->length))) {
            CHECK(op->write(fd, fill, n, cookie));
        }
    }
    // If the last entry has length zero, add an extra zero page at the end.
    // This prevents the possibility of trying to read/map past the end of the
    // bootfs at runtime.
    if (last_length == 0) {
        CHECK(op->write(fd, fill, sizeof(fill), cookie));
    }

    if (op->finish) {
        CHECK(op->finish(fd, cookie));
    }

    off_t wrote = lseek(fd, 0, SEEK_CUR);
    if (wrote < 0) {
        fprintf(stderr, "error: couldn't seek\n");
        goto fail;
    }

    // Write the bootheader
    if (lseek(fd, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: couldn't seek\n");
        goto fail;
    }
    bootdata_t boothdr = {
        .magic = BOOTDATA_MAGIC,
        .type = BOOTDATA_TYPE_BOOTFS,
        .insize = wrote,
        .outsize = compressed ? outsize : wrote,
        .flags = compressed ? BOOTDATA_BOOTFS_FLAG_COMPRESSED : 0
    };
    if (writex(fd, &boothdr, sizeof(boothdr)) < 0) {
        goto fail;
    }

    close(fd);
    return 0;

fail:
    fprintf(stderr, "error: failed writing '%s'\n", fn);
    close(fd);
    return -1;
}

int main(int argc, char **argv) {
    const char* output_file = "user.bootfs";
    fs fs = { 0 };
    fsentry* e = NULL;
    int i;
    unsigned hsz = 0;
    uint64_t off;
    bool compressed = false;

    argc--;
    argv++;
    while (argc > 0) {
        const char* cmd = argv[0];
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
        char* path = argv[i];
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
    fsentry* last_entry = NULL;
    for (e = fs.first; e != NULL; e = e->next) {
        e->offset = off;
        off += PAGEALIGN(e->length);
        if (off > INT32_MAX) {
            fprintf(stderr, "error: userfs too large\n");
            return -1;
        }
        last_entry = e;
    }
    if (last_entry && last_entry->length == 0) {
        off += sizeof(fill);
    }
    return export_userfs(output_file, &fs, hsz, off, compressed);
}
