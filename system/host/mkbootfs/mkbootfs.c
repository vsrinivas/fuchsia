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
#include <lib/cksum.h>

#include <zircon/boot/bootdata.h>

#define MAXBUFFER (1024*1024)

int verbose = 0;

typedef struct fsentry fsentry_t;

struct fsentry {
    fsentry_t* next;

    char* name;
    size_t namelen;
    uint32_t offset;
    uint32_t length;

    char* srcpath;
};

typedef enum {
    ITEM_BOOTDATA,
    ITEM_BOOTFS_BOOT,
    ITEM_BOOTFS_SYSTEM,
    ITEM_RAMDISK,
    ITEM_KERNEL,
    ITEM_CMDLINE,
    ITEM_PLATFORM_ID,
} item_type_t;

typedef struct item item_t;

struct item {
    item_type_t type;
    item_t* next;

    fsentry_t* first;
    fsentry_t* last;

    // size of header and total output size
    // used by bootfs items
    size_t hdrsize;
    size_t outsize;

    // Used only by ITEM_PLATFORM_ID items.
    bootdata_platform_id_t platform_id;
};

typedef struct filter filter_t;
struct filter {
    filter_t* next;
    bool matched_any;
    char text[];
};

filter_t* group_filter = NULL;

int add_filter(filter_t** flist, const char* text) {
    size_t len = strlen(text) + 1;
    if (len == 1) {
        fprintf(stderr, "error: empty filter string\n");
        return -1;
    }
    filter_t* filter = malloc(sizeof(filter_t) + len);
    if (filter == NULL) {
        fprintf(stderr, "error: out of memory (filter string)\n");
        return -1;
    }
    filter->matched_any = false;
    memcpy(filter->text, text, len);
    filter->next = *flist;
    *flist = filter;
    return 0;
}

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

static FILE* depfile = NULL;

static void notice_dep(const char* dep) {
    if (depfile != NULL) {
        fprintf(depfile, " %s", dep);
    }
}

static item_t* first_item;
static item_t* last_item;

item_t* new_item(item_type_t type) {
    item_t* item = calloc(1, sizeof(item_t));
    if (item == NULL) {
        fprintf(stderr, "OUT OF MEMORY\n");
        exit(-1);
    }
    item->type = type;
    if (first_item) {
        last_item->next = item;
    } else {
        first_item = item;
    }
    last_item = item;

    return item;
}

fsentry_t* import_manifest_entry(const char* fn, int lineno, const char* dst, const char* src) {
    fsentry_t* e;
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
        fprintf(stderr, "%s:%d: file too large '%s': %jd > %jd\n",
                fn, lineno, src, (intmax_t)s.st_size, (intmax_t)INT32_MAX);
        return NULL;
    }

    if ((e = calloc(1, sizeof(*e))) == NULL) return NULL;
    if ((e->name = strdup(dst)) == NULL) goto fail;
    if ((e->srcpath = strdup(src)) == NULL) goto fail;
    e->namelen = strlen(e->name) + 1;
    e->length = s.st_size;
    notice_dep(e->srcpath);
    return e;
fail:
    free(e->name);
    free(e);
    return NULL;
}

fsentry_t* import_directory_entry(const char* dst, const char* src, struct stat* s) {
    fsentry_t* e;

    if (s->st_size > INT32_MAX) {
        fprintf(stderr, "error: file too large '%s': %jd > %jd\n",
                src, (intmax_t)s->st_size, (intmax_t)INT32_MAX);
        return NULL;
    }

    if ((e = calloc(1, sizeof(*e))) == NULL) return NULL;
    if ((e->name = strdup(dst)) == NULL) goto fail;
    if ((e->srcpath = strdup(src)) == NULL) goto fail;
    e->namelen = strlen(e->name) + 1;
    e->length = s->st_size;
    notice_dep(e->srcpath);
    return e;
fail:
    free(e->name);
    free(e);
    return NULL;
}

void add_entry(item_t* fs, fsentry_t* e) {
    e->next = NULL;
    if (fs->last) {
        fs->last->next = e;
    } else {
        fs->first = e;
    }
    fs->last = e;
    fs->hdrsize += sizeof(bootfs_entry_t) + BOOTFS_ALIGN(e->namelen);
}

int import_manifest(FILE* fp, const char* fn, item_t* fs) {
    int lineno = 0;
    fsentry_t* e;
    char* eq;
    char line[4096];

    notice_dep(fn);

    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;
        if ((eq = strchr(line, '=')) == NULL) {
            continue;
        }
        *eq++ = 0;
        char* dstfn = trim(line);
        char* srcfn = trim(eq);
        char* group = "default";

        if (dstfn[0] == '{') {
            char* end = strchr(dstfn + 1, '}');
            if (end) {
                *end = 0;
                group = dstfn + 1;
                dstfn = end + 1;
            } else {
                fprintf(stderr, "%s:%d: unterminated group designator\n", fn, lineno);
                return -1;
            }
        }
        if (group_filter) {
            filter_t* filter;
            for (filter = group_filter; filter != NULL; filter = filter->next) {
                if (!strcmp(filter->text, group)) {
                    filter->matched_any = true;
                    goto okay;
                }
            }
            if (verbose) {
                fprintf(stderr, "excluding: %s (group '%s')\n", dstfn, group);
            }
            continue;
        }
okay:
        if ((e = import_manifest_entry(fn, lineno, dstfn, srcfn)) == NULL) {
            return -1;
        }
        add_entry(fs, e);
    }
    fclose(fp);
    return 0;
}

int import_file_as(const char* fn, item_type_t type, uint32_t hdrlen,
                   bootdata_t* hdr) {
    // bootdata file
    struct stat s;
    if (stat(fn, &s) != 0) {
        fprintf(stderr, "error: cannot stat '%s'\n", fn);
        return -1;
    }

    notice_dep(fn);

    if (type == ITEM_BOOTDATA) {
        if (!(hdr->flags & BOOTDATA_FLAG_V2)) {
            fprintf(stderr, "error: v1 bootdata no longer supported '%s'\n", fn);
            return -1;
        }
        if (s.st_size < sizeof(bootdata_t)) {
            fprintf(stderr, "error: bootdata file too small '%s'\n", fn);
            return -1;
        }
        if (s.st_size & 7) {
            fprintf(stderr, "error: bootdata file misaligned '%s'\n", fn);
            return -1;
        }
        if (s.st_size != (hdrlen + sizeof(bootdata_t))) {
            fprintf(stderr, "error: bootdata header size mismatch '%s'\n", fn);
            return -1;
        }
    }

    fsentry_t* e;
    if ((e = import_directory_entry("bootdata", fn, &s)) < 0) {
        return -1;
    }
    item_t* item = new_item(type);
    add_entry(item, e);
    return 0;
}

int import_file(const char* fn, bool system, bool ramdisk) {
    if (ramdisk) {
        struct stat s;
        if (stat(fn, &s) != 0) {
            fprintf(stderr, "error: cannot stat '%s'\n", fn);
            return -1;
        }
        notice_dep(fn);
        fsentry_t* e = import_directory_entry("ramdisk", fn, &s);
        if (e == NULL) {
            return -1;
        }
        item_t* item = new_item(ITEM_RAMDISK);
        item->outsize = s.st_size;
        add_entry(item, e);
        return 0;
    }

    FILE* fp;
    if ((fp = fopen(fn, "r")) == NULL) {
        return -1;
    }

    bootdata_t hdr;
    if ((fread(&hdr, sizeof(hdr), 1, fp) != 1) ||
        (hdr.type != BOOTDATA_CONTAINER) ||
        (hdr.extra != BOOTDATA_MAGIC)) {
        // not a bootdata file, must be a manifest...
        rewind(fp);

        item_t* item = new_item(system ? ITEM_BOOTFS_SYSTEM : ITEM_BOOTFS_BOOT);
        return import_manifest(fp, fn, item);
    } else {
        fclose(fp);
        return import_file_as(fn, ITEM_BOOTDATA, hdr.length, &hdr);
    }
}


int import_directory(const char* dpath, const char* spath, item_t* item, bool system) {
#define MAX_BOOTFS_PATH_LEN 4096
    char dst[MAX_BOOTFS_PATH_LEN];
    char src[MAX_BOOTFS_PATH_LEN];
#undef MAX_BOOTFS_PATH_LEN
    struct stat s;
    struct dirent* de;
    DIR* dir;

    if ((dir = opendir(spath)) == NULL) {
        fprintf(stderr, "error: cannot open directory '%s'\n", spath);
        return -1;
    }

    notice_dep(spath);

    if (item == NULL) {
        item = new_item(system ? ITEM_BOOTFS_SYSTEM : ITEM_BOOTFS_BOOT);
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
            fsentry_t* e;
            if (snprintf(dst, sizeof(dst), "%s%s", dpath, name) > sizeof(dst)) {
                fprintf(stderr, "error: name '%s%s' is too long\n", dpath, name);
                goto fail;
            }
            if ((e = import_directory_entry(dst, src, &s)) < 0) {
                goto fail;
            }
            add_entry(item, e);
        } else if (S_ISDIR(s.st_mode)) {
            if (snprintf(dst, sizeof(dst), "%s%s/", dpath, name) > sizeof(dst)) {
                fprintf(stderr, "error: name '%s%s/' is too long\n", dpath, name);
                goto fail;
            }
            import_directory(dst, src, item, system);
        } else {
            fprintf(stderr, "error: unsupported filetype '%s'\n", src);
            goto fail;
        }
    }
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

int readcrc32(int fd, size_t len, uint32_t* crc) {
    uint8_t buf[MAXBUFFER];
    while (len > 0) {
        size_t xfer = (len > sizeof(buf)) ? sizeof(buf) : len;
        if (readx(fd, buf, xfer) < 0) {
            return -1;
        }
        *crc = crc32(*crc, buf, xfer);
        len -= xfer;
    }
    return 0;
}

typedef struct {
    ssize_t (*setup)(int fd, void** cookie, uint32_t* crc);
    ssize_t (*write)(int fd, const void* src, size_t len, void* cookie, uint32_t* crc);
    ssize_t (*write_file)(int fd, const char* fn, size_t len, void* cookie, uint32_t* crc);
    ssize_t (*finish)(int fd, void* cookie, uint32_t* crc);
} io_ops;

ssize_t copydata(int fd, const void* src, size_t len, void* cookie, uint32_t* crc) {
    if (crc) {
        *crc = crc32(*crc, src, len);
    }
    if (writex(fd, src, len) < 0) {
        return -1;
    } else {
        return len;
    }
}

ssize_t copyfile(int fd, const char* fn, size_t len, void* cookie, uint32_t* crc) {
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
        if (crc) {
            *crc = crc32(*crc, (void*)buf, xfer);
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

ssize_t compress_setup(int fd, void** cookie, uint32_t* crc) {
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

    if (crc && (r > 0)) {
        *crc = crc32(*crc, buf, r);
    }
    return writex(fd, buf, r);
}

ssize_t compress_data(int fd, const void* src, size_t len, void* cookie, uint32_t* crc) {
    // max will be, worst case, a bit larger than MAXBUFFER
    size_t max = LZ4F_compressBound(len, &lz4_prefs);
    uint8_t buf[max];
    size_t r = LZ4F_compressUpdate((LZ4F_compressionContext_t)cookie, buf, max, src, len, NULL);
    if (check_and_log_lz4_error(r, "could not compress data")) {
        return -1;
    }
    if (crc) {
        *crc = crc32(*crc, buf, r);
    }
    return writex(fd, buf, r);
}

ssize_t compress_file(int fd, const char* fn, size_t len, void* cookie, uint32_t* crc) {
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
        if ((r = compress_data(fd, buf, xfer, cookie, crc)) < 0) {
            break;
        }
        len -= xfer;
    }
    close(fdi);
    return (r < 0) ? -1 : total;
}

ssize_t compress_finish(int fd, void* cookie, uint32_t* crc) {
    // Max write is one block (64kB uncompressed) plus 8 bytes of footer.
    size_t max = LZ4F_compressBound(65536, &lz4_prefs) + 8;
    uint8_t buf[max];
    size_t r = LZ4F_compressEnd((LZ4F_compressionContext_t)cookie, buf, max, NULL);
    if (check_and_log_lz4_error(r, "could not finish compression")) {
        r = -1;
    } else {
        if (crc) {
            *crc = crc32(*crc, buf, r);
        }
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

ssize_t copybootdatafile(int fd, const char* fn, size_t len) {
    char buf[MAXBUFFER];
    int r, fdi;
    if ((fdi = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }

    bootdata_t hdr;
    if ((r = readx(fdi, &hdr, sizeof(hdr))) < 0) {
        fprintf(stderr, "error: '%s' cannot read file header\n", fn);
        goto fail;
    }
    if ((hdr.type != BOOTDATA_CONTAINER) ||
        (hdr.extra != BOOTDATA_MAGIC)) {
        fprintf(stderr, "error: '%s' is not a bootdata file\n", fn);
        goto fail;
    }
    len -= sizeof(hdr);
    if (!(hdr.flags & BOOTDATA_FLAG_V2)) {
        fprintf(stderr, "error: '%s' is a v1 (unsupported) bootdata file\n", fn);
        goto fail;
    }
    if ((hdr.length != len)) {
        fprintf(stderr, "error: '%s' header length (%u) != %zd\n", fn, hdr.length, len);
        goto fail;
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

fail:
    close(fdi);
    return -1;
}

#define PAGEALIGN(n) (((n) + 4095) & (~4095))
#define PAGEFILL(n) (PAGEALIGN(n) - (n))

static const char fill[4096];

#define CHECK(w) do { if ((w) < 0) goto fail; } while (0)

int write_bootfs(int fd, item_t* item, bool compressed) {
    const io_ops* op = compressed ? &io_compressed : &io_plain;

    uint32_t n;
    fsentry_t* e;

    uint32_t crc = 0;

    const size_t hdrsize = sizeof(bootdata_t);

    // Make note of where we started
    off_t start = lseek(fd, 0, SEEK_CUR);

    if (start < 0) {
        fprintf(stderr, "error: couldn't seek\n");
fail:
        return -1;
    }

    if (compressed) {
        // Set the LZ4 content size to be original size
        lz4_prefs.frameInfo.contentSize = item->outsize;
    }

    // Increment past the bootdata header which will be filled out later.
    if (lseek(fd, (start + hdrsize), SEEK_SET) != (start + hdrsize)) {
        fprintf(stderr, "error: cannot seek\n");
        return -1;
    }

    void* cookie = NULL;
    if (op->setup) {
        CHECK(op->setup(fd, &cookie, &crc));
    }

    // write directory size entry
    {
        bootfs_header_t hdr = {
            .magic = BOOTFS_MAGIC,
            .dirsize = item->hdrsize - sizeof(bootfs_header_t),
        };
        CHECK(op->write(fd, &hdr, sizeof(hdr), cookie, &crc));
    }
    fsentry_t* last_entry = NULL;
    for (e = item->first; e != NULL; e = e->next) {
        bootfs_entry_t entry = {
            .name_len = e->namelen,
            .data_len = e->length,
            .data_off = e->offset,
        };
        CHECK(op->write(fd, &entry, sizeof(entry), cookie, &crc));
        CHECK(op->write(fd, e->name, e->namelen, cookie, &crc));
        if ((n = BOOTFS_ALIGN(e->namelen) - e->namelen) > 0) {
            CHECK(op->write(fd, fill, n, cookie, &crc));
        }
        last_entry = e;
    }
    // Record length of last file
    uint32_t last_length = last_entry ? last_entry->length : 0;

    if ((n = PAGEFILL(item->hdrsize))) {
        CHECK(op->write(fd, fill, n, cookie, &crc));
    }

    for (e = item->first; e != NULL; e = e->next) {
        if (verbose) {
            fprintf(stderr, "%08x %08x %s\n", e->offset, e->length, e->name);
        }
        CHECK(op->write_file(fd, e->srcpath, e->length, cookie, &crc));
        if ((n = PAGEFILL(e->length))) {
            CHECK(op->write(fd, fill, n, cookie, &crc));
        }
    }
    // If the last entry has length zero, add an extra zero page at the end.
    // This prevents the possibility of trying to read/map past the end of the
    // bootfs at runtime.
    if (last_length == 0) {
        CHECK(op->write(fd, fill, sizeof(fill), cookie, &crc));
    }

    if (op->finish) {
        CHECK(op->finish(fd, cookie, &crc));
    }

    off_t end = lseek(fd, 0, SEEK_CUR);
    if (end < 0) {
        fprintf(stderr, "error: couldn't seek\n");
        return -1;
    }

    // pad bootdata_t records to 8 byte boundary
    size_t pad = BOOTDATA_ALIGN(end) - end;
    if (pad) {
        if (writex(fd, fill, pad) < 0) {
            return -1;
        }
    }

    // Write the bootheader
    if (lseek(fd, start, SEEK_SET) != start) {
        fprintf(stderr, "error: couldn't seek to bootdata header\n");
        return -1;
    }

    size_t wrote = (end - start) - hdrsize;

    bootdata_t boothdr = {
        .type = (item->type == ITEM_BOOTFS_SYSTEM) ?
                BOOTDATA_BOOTFS_SYSTEM : BOOTDATA_BOOTFS_BOOT,
        .length = wrote,
        .extra = wrote,
        .flags = BOOTDATA_FLAG_V2 | BOOTDATA_FLAG_CRC32,
        .reserved0 = 0,
        .reserved1 = 0,
        .magic = BOOTITEM_MAGIC,
        .crc32 = 0,
    };
    if (compressed) {
        boothdr.extra = item->outsize;
        boothdr.flags |= BOOTDATA_BOOTFS_FLAG_COMPRESSED;
    }
    uint32_t hdrcrc = crc32(0, (void*) &boothdr, sizeof(boothdr));
    boothdr.crc32 = crc32_combine(hdrcrc, crc, boothdr.length);
    if (writex(fd, &boothdr, sizeof(boothdr)) < 0) {
        return -1;
    }

    if (lseek(fd, end + pad, SEEK_SET) != (end + pad)) {
        fprintf(stderr, "error: couldn't seek to end of item\n");
        return -1;
    }

    return 0;
}

int write_bootitem(int fd, bool compressed,
                   item_t* item, uint32_t type, size_t nulls) {
    const io_ops* op = compressed ? &io_compressed : &io_plain;

    uint32_t crc = 0;

    const size_t hdrsize = sizeof(bootdata_t);

    // Make note of where we started
    off_t start = lseek(fd, 0, SEEK_CUR);
    if (start < 0) {
        fprintf(stderr, "error: couldn't seek\n");
        return -1;
    }

    if (compressed) {
        // Set the LZ4 content size to be original size
        lz4_prefs.frameInfo.contentSize = item->outsize;
    }

    // Increment past the bootdata header which will be filled out later.
    if (lseek(fd, (start + hdrsize), SEEK_SET) != (start + hdrsize)) {
        fprintf(stderr, "error: cannot seek\n");
        return -1;
    }

    void* cookie = NULL;
    if (op->setup && op->setup(fd, &cookie, &crc) < 0) {
        return -1;
    }

    if (item->type == ITEM_PLATFORM_ID) {
        if (op->write(fd, &item->platform_id, sizeof(item->platform_id),
                      cookie, &crc) < 0) {
            return -1;
        }
    } else if (op->write_file(fd, item->first->srcpath, item->first->length,
                              cookie, &crc) < 0) {
        return -1;
    }
    if (nulls && (op->write(fd, fill, nulls, cookie, &crc) < 0)) {
        return -1;
    }

    if (op->finish && op->finish(fd, cookie, &crc) < 0) {
        return -1;
    }

    off_t end = lseek(fd, 0, SEEK_CUR);
    if (end < 0) {
        fprintf(stderr, "error: couldn't seek\n");
        return -1;
    }

    // pad bootdata_t records to 8 byte boundary
    size_t pad = BOOTDATA_ALIGN(end) - end;
    if (pad) {
        if (writex(fd, fill, pad) < 0) {
            return -1;
        }
    }

    // Write the bootheader
    if (lseek(fd, start, SEEK_SET) != start) {
        fprintf(stderr, "error: couldn't seek to bootdata header\n");
        return -1;
    }

    size_t wrote = (end - start) - hdrsize;

    bootdata_t boothdr = {
        .type = type,
        .length = wrote,
        .extra = wrote,
        .flags = BOOTDATA_FLAG_V2 | BOOTDATA_FLAG_CRC32,
        .reserved0 = 0,
        .reserved1 = 0,
        .magic = BOOTITEM_MAGIC,
        .crc32 = 0,
    };
    if (compressed) {
        boothdr.extra = item->outsize;
        boothdr.flags |= BOOTDATA_BOOTFS_FLAG_COMPRESSED;
    }
    uint32_t hdrcrc = crc32(0, (void*) &boothdr, sizeof(boothdr));
    boothdr.crc32 = crc32_combine(hdrcrc, crc, boothdr.length);
    if (writex(fd, &boothdr, sizeof(boothdr)) < 0) {
        return -1;
    }

    if (lseek(fd, end + pad, SEEK_SET) != (end + pad)) {
        fprintf(stderr, "error: couldn't seek to end of item\n");
        return -1;
    }

    return 0;
}

int write_bootdata(const char* fn, item_t* item, const char* header_path, int header_align) {
    //TODO: re-enable for debugging someday
    bool compressed = true;

    int fd;

    fd = open(fn, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create '%s'\n", fn);
        return -1;
    }

    off_t header_length = 0;
    if (header_path) {
        int header_fd = open(header_path, O_RDONLY);
        if (header_fd < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", header_path);
            return -1;
        }

        char buffer[4096];
        while (1) {
            int rd = read(header_fd, buffer, sizeof(buffer));
            if (rd < 0) {
                fprintf(stderr, "error: cannot read '%s'\n", header_path);
                return -1;
            } else if (rd == 0) {
                break;
            }
            int written = write(fd, buffer, rd);
            if (written != rd) {
                fprintf(stderr, "error: cannot write '%s'\n", fn);
                return -1;
            }
            header_length += rd;
        }

        close(header_fd);
    }

    if (header_align > 0) {
        // round up to next multiple of header_align
        header_length = ((header_length + header_align - 1) / header_align) * header_align;
        // pad zeroes after the header
        if (lseek(fd, header_length, SEEK_SET) != header_length) {
            fprintf(stderr, "error: cannot seek\n");
            goto fail;
        }
    }

    // Leave room for file header
    if (lseek(fd, sizeof(bootdata_t), SEEK_CUR) != header_length + sizeof(bootdata_t)) {
        fprintf(stderr, "error: cannot seek\n");
        goto fail;
    }

    while (item != NULL) {
        switch (item->type) {
        case ITEM_BOOTDATA:
            CHECK(copybootdatafile(fd, item->first->srcpath, item->first->length));
            break;
        case ITEM_KERNEL:
            CHECK(write_bootitem(fd, false, item, BOOTDATA_KERNEL, 0));
            break;
        case ITEM_CMDLINE:
            CHECK(write_bootitem(fd, false, item, BOOTDATA_CMDLINE, 1));
            break;
        case ITEM_PLATFORM_ID:
            CHECK(write_bootitem(fd, false, item, BOOTDATA_PLATFORM_ID, 0));
            break;
        case ITEM_BOOTFS_BOOT:
        case ITEM_BOOTFS_SYSTEM:
            CHECK(write_bootfs(fd, item, compressed));
            break;
        case ITEM_RAMDISK:
            CHECK(write_bootitem(fd, compressed, item, BOOTDATA_RAMDISK, 0));
            break;
        default:
            fprintf(stderr, "error: internal: type %08x unknown\n", item->type);
            goto fail;
        }

        item = item->next;
    }

    off_t file_end = lseek(fd, 0, SEEK_CUR);
    if (file_end < 0) {
        fprintf(stderr, "error: couldn't seek\n");
        goto fail;
    }

    // Write the file header
    if (lseek(fd, header_length, SEEK_SET) != header_length) {
        fprintf(stderr, "error: couldn't seek to bootdata file header\n");
        goto fail;
    }

    bootdata_t filehdr = {
        .type = BOOTDATA_CONTAINER,
        .length = file_end - sizeof(bootdata_t) - header_length,
        .extra = BOOTDATA_MAGIC,
        .flags = BOOTDATA_FLAG_V2,
        .reserved0 = 0,
        .reserved1 = 0,
        .magic = BOOTITEM_MAGIC,
        .crc32 = BOOTITEM_NO_CRC32,
    };
    if (writex(fd, &filehdr, sizeof(filehdr)) < 0) {
        goto fail;
    }
    close(fd);
    return 0;

fail:
    fprintf(stderr, "error: failed writing '%s'\n", fn);
    close(fd);
    return -1;
}

int dump_bootdata(const char* fn) {
    int fd;
    if ((fd = open(fn, O_RDONLY)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }

    bootdata_t hdr;
    if (readx(fd, &hdr, sizeof(hdr)) < 0) {
        fprintf(stderr, "error: cannot read header\n");
        goto fail;
    }

    if ((hdr.type != BOOTDATA_CONTAINER) ||
        (hdr.extra != BOOTDATA_MAGIC) ||
        (hdr.length < sizeof(hdr))) {
        fprintf(stderr, "error: invalid bootdata header\n");
        goto fail;
    }
    size_t off = sizeof(hdr);
    if (!(hdr.flags & BOOTDATA_FLAG_V2)) {
        fprintf(stderr, "error: bootdata v1 no longer supported\n");
        goto fail;
    }
    size_t end = off + hdr.length;

    while (off < end) {
        if (readx(fd, &hdr, sizeof(hdr)) < 0) {
            fprintf(stderr, "error: cannot read section header\n");
            goto fail;
        }
        switch (hdr.type) {
        case BOOTDATA_BOOTFS_BOOT:
            printf("%08zx: %08x BOOTFS @/boot (size=%08x)\n",
                   off, hdr.length, hdr.extra);
            break;
        case BOOTDATA_BOOTFS_SYSTEM:
            printf("%08zx: %08x BOOTFS @/system (size=%08x)\n",
                   off, hdr.length, hdr.extra);
            break;
        case BOOTDATA_RAMDISK:
            printf("%08zx: %08x RAMDISK (size=%08x)\n",
                   off, hdr.length, hdr.extra);
            break;

#define BOOTDATA_CASE(type)                                         \
        case BOOTDATA_##type:                                       \
            printf("%08zx: %08x %s\n", off, hdr.length, #type);     \
            break
        BOOTDATA_CASE(KERNEL);
        BOOTDATA_CASE(CMDLINE);
        BOOTDATA_CASE(ACPI_RSDP);
        BOOTDATA_CASE(FRAMEBUFFER);
        BOOTDATA_CASE(DEBUG_UART);
        BOOTDATA_CASE(PLATFORM_ID);
        BOOTDATA_CASE(LASTLOG_NVRAM);
        BOOTDATA_CASE(LASTLOG_NVRAM2);
        BOOTDATA_CASE(E820_TABLE);
        BOOTDATA_CASE(EFI_MEMORY_MAP);
        BOOTDATA_CASE(EFI_SYSTEM_TABLE);
        BOOTDATA_CASE(LAST_CRASHLOG);
        BOOTDATA_CASE(IGNORE);
#undef BOOTDATA_CASE

        default:
            printf("%08zx: %08x UNKNOWN (type=%08x)\n", off, hdr.length, hdr.type);
            break;
        }
        off += sizeof(hdr);

        size_t pad = BOOTDATA_ALIGN(hdr.length) - hdr.length;
        if (hdr.flags & BOOTDATA_FLAG_CRC32) {
            printf("        :          MAGIC=%08x CRC=%08x\n", hdr.magic, hdr.crc32);
            uint32_t tmp = hdr.crc32;
            hdr.crc32 = 0;
            uint32_t crc = crc32(0, (void*) &hdr, sizeof(hdr));
            hdr.crc32 = tmp;

            if (readcrc32(fd, hdr.length, &crc) < 0) {
                fprintf(stderr, "error: failed to read data for crc\n");
                goto fail;
            }
            if (crc != hdr.crc32) {
                fprintf(stderr, "error: CRC %08x does not match header\n", crc);
            }
            if (pad && (lseek(fd, pad, SEEK_CUR) < 0)) {
                fprintf(stderr, "error: seeking\n");
                goto fail;
            }
        } else {
            printf("        :          MAGIC=%08x NO CRC\n", hdr.magic);
            if (lseek(fd, hdr.length + pad, SEEK_CUR) < 0) {
                fprintf(stderr, "error: seeking\n");
                goto fail;
            }
        }
        off += hdr.length + pad;
    }
    close(fd);
    return 0;
fail:
    close(fd);
    return -1;
}


void usage(void) {
    fprintf(stderr,
    "usage: mkbootfs <option-or-input>*\n"
    "\n"
    "       mkbootfs creates a bootdata image consisting of the inputs\n"
    "       provided in the specified order.\n"
    "\n"
    "options: -o <filename>         output bootdata file name\n"
    "         --depfile <filename>  output make/ninja dependencies file name\n"
    "         -C <filename>         include kernel command line\n"
    "         -c                    compress bootfs image (default)\n"
    "         --empty               create output even if empty\n"
    "         -v                    verbose output\n"
    "         -t <filename>         dump bootdata contents\n"
    "         -g <group>            select allowed groups for manifest items\n"
    "                               (multiple groups may be comma separated)\n"
    "                               (the value 'all' resets to include all groups)\n"
    "         --uncompressed        don't compress bootfs image (debug only)\n"
    "         --target=system       bootfs to be unpacked at /system\n"
    "         --target=boot         bootfs to be unpacked at /boot\n"
    "         --vid <vid>           specify VID for platform ID record\n"
    "         --pid <vid>           specify PID for platform ID record\n"
    "         --board <board-name>  specify board name for platform ID record\n"
    "         --ramdisk             files are raw disk images, not bootdata\n"
    "         --header <filename>   optional binary header to prepend at beginning of output file\n"
    "         --header-align <val>  optional alignment for the binary header.\n"
    "                               header will be padded to align beginning\n"
    "                               of bootdata to this alignment boundary\n"
    "\n"
    "inputs:  <filename>            file containing bootdata (binary)\n"
    "                               or a manifest (target=srcpath lines)\n"
    "         @<directory>          directory to recursively import\n"
    "\n"
    "notes:   Each manifest or directory is imported as a distinct bootfs\n"
    "         section, tagged for unpacking at /boot or /system based on\n"
    "         the most recent --target= directive.\n"
    );
}

static bool parse_uint32(const char* string, uint32_t* out_value) {
    return (sscanf(string, "0x%x", out_value) == 1 ||
            sscanf(string, "%u", out_value) == 1);
}

int main(int argc, char **argv) {
    const char* output_file = "user.bootfs";

    bool compressed = true;
    bool empty_ok = false;
    bool have_cmdline = false;
    const char* vid_arg = NULL;
    const char* pid_arg = NULL;
    const char* board_arg = NULL;
    const char* header_path = NULL;
    int header_align = 0;

    if (argc == 1) {
        usage();
        return -1;
    }
    bool system = true;
    bool ramdisk = false;

    if ((argc == 3) && (!strcmp(argv[1],"-t"))) {
        return dump_bootdata(argv[2]);
    }

    argc--;
    argv++;
    while (argc > 0) {
        const char* cmd = argv[0];
        if (!strcmp(cmd,"-v")) {
            verbose = 1;
        } else if (!strcmp(cmd,"-o")) {
            if (argc < 2) {
                fprintf(stderr, "error: no output filename given\n");
                return -1;
            }
            output_file = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd, "-C")) {
            if (have_cmdline) {
                fprintf(stderr, "error: only one command line may be included\n");
                return -1;
            }
            if (argc < 2) {
                fprintf(stderr, "error: no kernel command line file given\n");
                return -1;
            }
            have_cmdline = true;

            if (import_file_as(argv[1], ITEM_CMDLINE, 0, NULL) < 0) {
                return -1;
            }
            argc--;
            argv++;
        } else if (!strcmp(cmd,"-g")) {
            if (argc < 2) {
                fprintf(stderr, "error: no group specified\n");
                return -1;
            }
            group_filter = NULL;
            if (strcmp(argv[1], "all")) {
                char* group = argv[1];
                while (group) {
                    char* next = strchr(group, ',');
                    if (next) {
                        *next++ = 0;
                    }
                    if (add_filter(&group_filter, group) < 0) {
                        return -1;
                    }
                    group = next;
                }
            }
            argc--;
            argv++;
        } else if (!strcmp(cmd,"-h") || !strcmp(cmd, "--help")) {
            usage();
            fprintf(stderr, "usage: mkbootfs [-v] [-o <fsimage>] <manifests>...\n");
            return 0;
        } else if (!strcmp(cmd,"-t")) {
            fprintf(stderr, "error: -t option must be used alone, with one filename.\n");
            return 0;
        } else if (!strcmp(cmd,"-c")) {
            compressed = true;
        } else if (!strcmp(cmd,"--uncompressed")) {
            compressed = false;
        } else if (!strcmp(cmd,"--target=system")) {
            system = true;
        } else if (!strcmp(cmd,"--target=boot")) {
            system = false;
        } else if (!strcmp(cmd,"--ramdisk")) {
            ramdisk = true;
        } else if (!strcmp(cmd, "--header")) {
            if (header_path) {
                fprintf(stderr, "error: only one header can be included\n");
                return -1;
            }
            if (argc < 2) {
                fprintf(stderr, "error: no header file given\n");
                return -1;
            }
            header_path = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd, "--header-align")) {
            if (header_align) {
                fprintf(stderr, "error: only one header alignment valuecan be specified\n");
                return -1;
            }
            if (argc < 2) {
                fprintf(stderr, "error: no header alignement given\n");
                return -1;
            }
            header_align = atoi(argv[1]);
            if (header_align <= 0) {
                fprintf(stderr, "error: bad --header-align value %s\n", argv[1]);
                return -1;
            }
            argc--;
            argv++;
        } else if (!strcmp(cmd,"--vid")) {
            if (argc < 2) {
                fprintf(stderr, "error: no value given for --vid\n");
                return -1;
            }
            vid_arg = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd,"--pid")) {
            if (argc < 2) {
                fprintf(stderr, "error: no value given for --pid\n");
                return -1;
            }
            pid_arg = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd,"--board")) {
            if (argc < 2) {
                fprintf(stderr, "error: no value given for --board\n");
                return -1;
            }
            board_arg = argv[1];
            argc--;
            argv++;
        } else if (!strcmp(cmd,"--depfile")) {
            if (argc < 2) {
                fprintf(stderr, "error: no value given for --depfile\n");
                return -1;
            }
            depfile = fopen(argv[1], "w");
            if (depfile == NULL) {
                fprintf(stderr, "cannot write '%s': %s\n",
                        argv[1], strerror(errno));
                return -1;
            }
            fprintf(depfile, "%s:", output_file);
            argc--;
            argv++;
        } else if (!strcmp(cmd,"--empty")) {
            empty_ok = true;
        } else if (cmd[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", cmd);
            return -1;
        } else {
            // input file
            char* path = argv[0];
            if (path[0] == '@') {
                path++;
                int len = strlen(path);
                if (path[len - 1] == '/') {
                    // remove trailing slash
                    path[len - 1] = 0;
                }
                if (import_directory("", path, NULL, system) < 0) {
                    fprintf(stderr, "error: failed to import directory %s\n", path);
                    return -1;
                }
            } else if (import_file(path, system, ramdisk) < 0) {
                fprintf(stderr, "error: failed to import file %s\n", path);
                return -1;
            }
        }
        argc--;
        argv++;
    }

    if (vid_arg || pid_arg || board_arg) {
        bootdata_platform_id_t platform_id = {};
        if (vid_arg) {
            if (!parse_uint32(vid_arg, &platform_id.vid)) {
                fprintf(stderr, "error: could not parse --vid %s\n", vid_arg);
                return -1;
            }
        }
        if (pid_arg) {
            if (!parse_uint32(pid_arg, &platform_id.pid)) {
                fprintf(stderr, "error: could not parse --pid %s\n", pid_arg);
                return -1;
            }
        }
        if (board_arg) {
            if (strlen(board_arg) >= sizeof(platform_id.board_name)) {
                fprintf(stderr, "error: board name too long\n");
                return -1;
            }
            strncpy(platform_id.board_name, board_arg,
                    sizeof(platform_id.board_name));
        }
        new_item(ITEM_PLATFORM_ID)->platform_id = platform_id;
    }

    for (filter_t* f = group_filter; f != NULL; f = f->next) {
        if (!f->matched_any) {
            fprintf(stderr, "error: group '%s' not used in any manifest\n",
                    f->text);
            return -1;
        }
    }

    if (first_item == NULL && !empty_ok) {
        fprintf(stderr, "error: no inputs given\n");
        return -1;
    }

    if (depfile != NULL) {
        putc('\n', depfile);
        fclose(depfile);
        depfile = NULL;
    }

    // preflight calculations for bootfs items
    for (item_t* item = first_item; item != NULL; item = item->next) {
        switch (item->type) {
        case ITEM_BOOTFS_BOOT:
        case ITEM_BOOTFS_SYSTEM:
            // account for the bootfs header record
            item->hdrsize += sizeof(bootfs_header_t);
            size_t off = PAGEALIGN(item->hdrsize);
            fsentry_t* last_entry = NULL;
            for (fsentry_t* e = item->first; e != NULL; e = e->next) {
                e->offset = off;
                off += PAGEALIGN(e->length);
                if (off > INT32_MAX) {
                    fprintf(stderr, "error: userfs too large: %jd > %jd\n",
                            (intmax_t)off, (intmax_t)INT32_MAX);
                    return -1;
                }
                last_entry = e;
            }
            if (last_entry && last_entry->length == 0) {
                off += sizeof(fill);
            }
            item->outsize = off;
            break;
        default:
            break;
        }
    }

    return write_bootdata(output_file, first_item, header_path, header_align);
}
