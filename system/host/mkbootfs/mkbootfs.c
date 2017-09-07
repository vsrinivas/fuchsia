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

#include <magenta/boot/bootdata.h>

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

#define ITEM_BOOTDATA 0
#define ITEM_BOOTFS_BOOT 1
#define ITEM_BOOTFS_SYSTEM 2
#define ITEM_KERNEL 3
#define ITEM_CMDLINE 4

typedef struct item item_t;

struct item {
    uint32_t type;
    item_t* next;

    fsentry_t* first;
    fsentry_t* last;

    // size of header and total output size
    // used by bootfs items
    size_t hdrsize;
    size_t outsize;
};

typedef struct filter filter_t;
struct filter {
    filter_t* next;
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

static item_t* first_item;
static item_t* last_item;

item_t* new_item(uint32_t type) {
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

fsentry_t* import_directory_entry(const char* dst, const char* src, struct stat* s) {
    fsentry_t* e;

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

int import_file_as(const char* fn, uint32_t type, uint32_t hdrlen, bootdata_t* hdr) {
    // bootdata file
    struct stat s;
    if (stat(fn, &s) != 0) {
        fprintf(stderr, "error: cannot stat '%s'\n", fn);
        return -1;
    }

    if (type == ITEM_BOOTDATA) {
        size_t hsz = sizeof(bootdata_t);
        if (hdr->flags & BOOTDATA_FLAG_EXTRA) {
            hsz += sizeof(bootextra_t);
        }
        if (s.st_size < hsz) {
            fprintf(stderr, "error: bootdata file too small '%s'\n", fn);
            return -1;
        }
        if (s.st_size & 7) {
            fprintf(stderr, "error: bootdata file misaligned '%s'\n", fn);
            return -1;
        }
        if (s.st_size != (hdrlen + hsz)) {
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

int import_file(const char* fn, bool system) {
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
    if (hdr.flags & BOOTDATA_FLAG_EXTRA) {
        bootextra_t extra;
        if ((r = readx(fdi, &extra, sizeof(extra))) < 0) {
            fprintf(stderr, "error: '%s' cannot read extra header\n", fn);
            goto fail;
        }
        len -= sizeof(extra);
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

char fill[4096];

#define CHECK(w) do { if ((w) < 0) goto fail; } while (0)

int write_bootfs(int fd, const io_ops* op, item_t* item, bool compressed, bool extra) {
    uint32_t n;
    fsentry_t* e;

    uint32_t crcval = 0;
    uint32_t* crc = NULL;

    size_t hdrsize = sizeof(bootdata_t);

    if (extra) {
        hdrsize += sizeof(bootextra_t);
        crc = &crcval;
    }

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
        CHECK(op->setup(fd, &cookie, crc));
    }

    // write directory size entry
    {
        bootfs_header_t hdr = {
            .magic = BOOTFS_MAGIC,
            .dirsize = item->hdrsize - sizeof(bootfs_header_t),
        };
        CHECK(op->write(fd, &hdr, sizeof(hdr), cookie, crc));
    }
    fsentry_t* last_entry = NULL;
    for (e = item->first; e != NULL; e = e->next) {
        bootfs_entry_t entry = {
            .name_len = e->namelen,
            .data_len = e->length,
            .data_off = e->offset,
        };
        CHECK(op->write(fd, &entry, sizeof(entry), cookie, crc));
        CHECK(op->write(fd, e->name, e->namelen, cookie, crc));
        if ((n = BOOTFS_ALIGN(e->namelen) - e->namelen) > 0) {
            CHECK(op->write(fd, fill, n, cookie, crc));
        }
        last_entry = e;
    }
    // Record length of last file
    uint32_t last_length = last_entry ? last_entry->length : 0;

    if ((n = PAGEFILL(item->hdrsize))) {
        CHECK(op->write(fd, fill, n, cookie, crc));
    }

    for (e = item->first; e != NULL; e = e->next) {
        if (verbose) {
            fprintf(stderr, "%08x %08x %s\n", e->offset, e->length, e->name);
        }
        CHECK(op->write_file(fd, e->srcpath, e->length, cookie, crc));
        if ((n = PAGEFILL(e->length))) {
            CHECK(op->write(fd, fill, n, cookie, crc));
        }
    }
    // If the last entry has length zero, add an extra zero page at the end.
    // This prevents the possibility of trying to read/map past the end of the
    // bootfs at runtime.
    if (last_length == 0) {
        CHECK(op->write(fd, fill, sizeof(fill), cookie, crc));
    }

    if (op->finish) {
        CHECK(op->finish(fd, cookie, crc));
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
        .extra = compressed ? item->outsize : wrote,
        .flags = compressed ? BOOTDATA_BOOTFS_FLAG_COMPRESSED : 0
    };
    if (extra) {
        boothdr.flags |= BOOTDATA_FLAG_EXTRA | BOOTDATA_FLAG_CRC32;
    }
    if (writex(fd, &boothdr, sizeof(boothdr)) < 0) {
        return -1;
    }
    if (extra) {
        bootextra_t extra = {
            .reserved0 = 0,
            .reserved1 = 0,
            .magic = BOOTITEM_MAGIC,
            .crc32 = 0,
        };
        uint32_t hdrcrc = crc32(0, (void*) &boothdr, sizeof(boothdr));
        hdrcrc = crc32(hdrcrc, (void*) &extra, sizeof(extra));
        extra.crc32 = crc32_combine(hdrcrc, *crc, boothdr.length);
        if (writex(fd, &extra, sizeof(extra)) < 0) {
            return -1;
        }
    }

    if (lseek(fd, end + pad, SEEK_SET) != (end + pad)) {
        fprintf(stderr, "error: couldn't seek to end of item\n");
        return -1;
    }

    return 0;
}

int write_bootitem(int fd, item_t* item, uint32_t type, size_t nulls, bool extra) {
    uint32_t* crc = NULL;

    bootdata_t hdr = {
        .type = type,
        .length = item->first->length + nulls,
        .extra = 0,
        .flags = 0,
    };
    bootextra_t ehdr = {
        .reserved0 = 0,
        .reserved1 = 0,
        .magic = BOOTITEM_MAGIC,
        .crc32 = 0,
    };
    if (extra) {
        hdr.flags |= BOOTDATA_FLAG_EXTRA | BOOTDATA_FLAG_CRC32;
    }
    if (writex(fd, &hdr, sizeof(hdr)) < 0) {
        return -1;
    }
    off_t eoff = 0;
    if (extra) {
        if ((eoff = lseek(fd, 0, SEEK_CUR)) < 0) {
            return -1;
        }
        // placeholder header
        if (writex(fd, &ehdr, sizeof(ehdr)) < 0) {
            return -1;
        }
        crc = &ehdr.crc32;
        uint32_t tmp = crc32(0, (void*) &hdr, sizeof(hdr));
        *crc = crc32(tmp, (void*) &ehdr, sizeof(ehdr));
    }
    if (copyfile(fd, item->first->srcpath, item->first->length, NULL, crc) < 0) {
        return -1;
    }
    if (nulls && (copydata(fd, fill, nulls, NULL, crc) < 0)) {
        return -1;
    }
    size_t pad = BOOTDATA_ALIGN(hdr.length) - hdr.length;
    if (pad) {
        if (writex(fd, fill, pad) < 0) {
            return -1;
        }
    }
    if (extra) {
        // patch computed crc into extra header
        off_t save;
        if ((save = lseek(fd, 0, SEEK_CUR)) < 0) {
            return -1;
        }
        if (lseek(fd, eoff, SEEK_SET) != eoff) {
            return -1;
        }
        if (writex(fd, &ehdr, sizeof(ehdr)) < 0) {
            return -1;
        }
        if (lseek(fd, save, SEEK_SET) != save) {
            return -1;
        }
    }
    return 0;
}

int write_bootdata(const char* fn, item_t* item, bool extra) {
    //TODO: re-enable for debugging someday
    bool compressed = true;

    int fd;
    const io_ops* op = compressed ? &io_compressed : &io_plain;

    fd = open(fn, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create '%s'\n", fn);
        return -1;
    }

    size_t hdrsize = sizeof(bootdata_t);
    if (extra) {
        hdrsize += sizeof(bootextra_t);
    }

    // Leave room for file header
    if (lseek(fd, hdrsize, SEEK_SET) != hdrsize) {
        fprintf(stderr, "error: cannot seek\n");
        goto fail;
    }

    while (item != NULL) {
        switch (item->type) {
        case ITEM_BOOTDATA:
            CHECK(copybootdatafile(fd, item->first->srcpath, item->first->length));
            break;
        case ITEM_KERNEL:
            CHECK(write_bootitem(fd, item, BOOTDATA_KERNEL, 0, extra));
            break;
        case ITEM_CMDLINE:
            CHECK(write_bootitem(fd, item, BOOTDATA_CMDLINE, 1, extra));
            break;
        case ITEM_BOOTFS_BOOT:
        case ITEM_BOOTFS_SYSTEM:
            CHECK(write_bootfs(fd, op, item, compressed, extra));
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
    if (lseek(fd, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: couldn't seek to bootdata file header\n");
        goto fail;
    }

    bootdata_t filehdr = {
        .type = BOOTDATA_CONTAINER,
        .length = file_end - hdrsize,
        .extra = BOOTDATA_MAGIC,
        .flags = extra ? BOOTDATA_FLAG_EXTRA : 0,
    };
    if (writex(fd, &filehdr, sizeof(filehdr)) < 0) {
        goto fail;
    }
    if (extra) {
        bootextra_t fileextra = {
            .reserved0 = 0,
            .reserved1 = 1,
            .magic = BOOTITEM_MAGIC,
            .crc32 = BOOTITEM_NO_CRC32,
        };
        if (writex(fd, &fileextra, sizeof(fileextra)) < 0) {
            goto fail;
        }
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
    if (hdr.flags & BOOTDATA_FLAG_EXTRA) {
        bootextra_t extra;
        if (readx(fd, &extra, sizeof(extra)) < 0) {
            fprintf(stderr, "error: cannot read extra header\n");
            goto fail;
        }
        if (extra.magic != BOOTITEM_MAGIC) {
            fprintf(stderr, "error: invalid extra header\n");
            goto fail;
        }
        off += sizeof(bootextra_t);
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
        case BOOTDATA_KERNEL:
            printf("%08zx: %08x KERNEL\n", off, hdr.length);
            break;
        case BOOTDATA_MDI:
            printf("%08zx: %08x MDI\n", off, hdr.length);
            break;
        case BOOTDATA_CMDLINE:
            printf("%08zx: %08x CMDLINE\n", off, hdr.length);
            break;
        default:
            printf("%08zx: %08x UNKNOWN (type=%08x)\n", off, hdr.length, hdr.type);
            break;
        }
        off += sizeof(hdr);

        bootextra_t ehdr;
        uint32_t crc = 0;
        if (hdr.flags & BOOTDATA_FLAG_EXTRA) {
            if (readx(fd, &ehdr, sizeof(ehdr)) < 0) {
                fprintf(stderr, "error: cannot read extra header data\n");
                goto fail;
            }
            printf("%08zx:          MAGIC=%08x CRC=%08x\n", off, ehdr.magic, ehdr.crc32);
            if (ehdr.magic != BOOTITEM_MAGIC) {
                fprintf(stderr, "error: bad bootitem magic\n");
            }
            uint32_t tmp = ehdr.crc32;
            ehdr.crc32 = 0;
            crc = crc32(crc, (void*) &hdr, sizeof(hdr));
            crc = crc32(crc, (void*) &ehdr, sizeof(ehdr));
            ehdr.crc32 = tmp;
            off += sizeof(ehdr);
        }
        size_t pad = BOOTDATA_ALIGN(hdr.length) - hdr.length;
        if (hdr.flags & BOOTDATA_FLAG_CRC32) {
            if (!(hdr.flags & BOOTDATA_FLAG_EXTRA)) {
                fprintf(stderr, "error: crc32 indicated w/out extra data!\n");
                goto fail;
            }
            if (readcrc32(fd, hdr.length, &crc) < 0) {
                fprintf(stderr, "error: failed to read data for crc\n");
                goto fail;
            }
            if (crc != ehdr.crc32) {
                fprintf(stderr, "error: CRC %08x does not match header\n", crc);
            }
            if (pad && (lseek(fd, pad, SEEK_CUR) < 0)) {
                fprintf(stderr, "error: seeking\n");
                goto fail;
            }
        } else {
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
    "options: -o <filename>    output bootdata file name\n"
    "         -k <filename>    include kernel (must be first)\n"
    "         -C <filename>    include kernel command line\n"
    "         -c               compress bootfs image (default)\n"
    "         -v               verbose output\n"
    "         -x               enable bootextra data (crc32)\n"
    "         -t <filename>    dump bootdata contents\n"
    "         -g <group>       select allowed groups for manifest items\n"
    "                          (multiple groups may be comma separated)\n"
    "                          (the value 'all' resets to include all groups)\n"
    "         --uncompressed   don't compress bootfs image (debug only)\n"
    "         --target=system  bootfs to be unpacked at /system\n"
    "         --target=boot    bootfs to be unpacked at /boot\n"
    "\n"
    "inputs:  <filename>       file containing bootdata (binary)\n"
    "                          or a manifest (target=srcpath lines)\n"
    "         @<directory>     directory to recursively import\n"
    "\n"
    "notes:   Each manifest or directory is imported as a distinct bootfs\n"
    "         section, tagged for unpacking at /boot or /system based on\n"
    "         the most recent --target= directive.\n"
    );
}

int main(int argc, char **argv) {
    const char* output_file = "user.bootfs";

    bool compressed = true;
    bool have_kernel = false;
    bool have_cmdline = false;
    bool extra = false;
    unsigned incount = 0;

    if (argc == 1) {
        usage();
        return -1;
    }
    bool system = true;

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
        } else if (!strcmp(cmd,"-k")) {
            if (have_kernel) {
                fprintf(stderr, "error: only one kernel may be included\n");
                return -1;
            }
            if (argc < 2) {
                fprintf(stderr, "error: no kernel filename given\n");
                return -1;
            }
            if (first_item != NULL) {
                fprintf(stderr, "error: kernel must be the first input\n");
                return -1;
            }
            have_kernel = 1;
            if (import_file_as(argv[1], ITEM_KERNEL, 0, NULL) < 0) {
                return -1;
            }
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
        } else if (!strcmp(cmd,"-x")) {
            extra = true;
        } else if (!strcmp(cmd,"-c")) {
            compressed = true;
        } else if (!strcmp(cmd,"--uncompressed")) {
            compressed = false;
        } else if (!strcmp(cmd,"--target=system")) {
            system = true;
        } else if (!strcmp(cmd,"--target=boot")) {
            system = false;
        } else if (cmd[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", cmd);
            return -1;
        } else {
            // input file
            incount++;
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
            } else if (import_file(path, system) < 0) {
                fprintf(stderr, "error: failed to import file %s\n", path);
                return -1;
            }
        }
        argc--;
        argv++;
    }
    if (first_item == NULL) {
        fprintf(stderr, "error: no inputs given\n");
        return -1;
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
                    fprintf(stderr, "error: userfs too large\n");
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

    return write_bootdata(output_file, first_item, extra);
}
