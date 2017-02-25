// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ddk/binding.h>

typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[0];
} notehdr;

typedef Elf64_Ehdr elfhdr;
typedef Elf64_Phdr elfphdr;

static mx_status_t find_note(const char* name, uint32_t type,
                             void* data, size_t size,
                             mx_status_t (*func)(void* note, size_t sz, void* cookie),
                             void* cookie) {
    size_t nlen = strlen(name);
    while (size >= sizeof(notehdr)) {
        // ignore padding between notes
        if (*((uint32_t*) data) == 0) {
            size -= sizeof(uint32_t);
            data += sizeof(uint32_t);
            continue;
        }
        notehdr* hdr = data;
        uint32_t nsz = (hdr->namesz + 3) & (~3);
        if (nsz > (size - sizeof(notehdr))) {
            return ERR_INTERNAL;
        }
        data += sizeof(notehdr) + nsz;
        size -= sizeof(notehdr) + nsz;

        uint32_t dsz = (hdr->descsz + 3) & (~3);
        if (dsz > size) {
            return ERR_INTERNAL;
        }

        if ((hdr->type == type) &&
            (hdr->namesz == nlen) &&
            (memcmp(name, hdr->name, nlen) == 0)) {
            return func(data, hdr->descsz, cookie);
        }

        data += dsz;
        size -= dsz;
    }
    return ERR_NOT_FOUND;
}

static mx_status_t for_each_note(int fd, const char* name, uint32_t type,
                                 void* data, size_t dsize,
                                 mx_status_t (*func)(void* note, size_t sz, void* cookie),
                                 void* cookie) {
    elfphdr ph[64];
    elfhdr eh;
    if ((lseek(fd, 0, SEEK_SET) != 0)) {
        printf("for_each_note: seek(0) failed\n");
        return ERR_IO;
    }
    if (read(fd, &eh, sizeof(eh)) != sizeof(eh)) {
        printf("for_each_note: read(eh) failed\n");
        return ERR_IO;
    }
    if (memcmp(&eh, ELFMAG, 4) ||
        (eh.e_ehsize != sizeof(elfhdr)) ||
        (eh.e_phentsize != sizeof(elfphdr))) {
        printf("for_each_note: bad elf magic\n");
        return ERR_INTERNAL;
    }
    size_t sz = sizeof(elfphdr) * eh.e_phnum;
    if (sz > sizeof(ph)) {
        printf("for_each_note: too many phdrs\n");
        return ERR_INTERNAL;
    }
    if ((lseek(fd, eh.e_phoff, SEEK_SET) != (off_t)eh.e_phoff) ||
        (read(fd, ph, sz) != (ssize_t)sz)){
        printf("for_each_note: seek(eh.e_phoff) failed\n");
        return ERR_IO;
    }
    for (int i = 0; i < eh.e_phnum; i++) {
        if ((ph[i].p_type != PT_NOTE) ||
            (ph[i].p_filesz > dsize)) {
            continue;
        }
        if ((lseek(fd, ph[i].p_offset, SEEK_SET) != (off_t)ph[i].p_offset) ||
            (read(fd, data, ph[i].p_filesz) != (ssize_t)ph[i].p_filesz)) {
            printf("for_each_note: read() failed\n");
            return ERR_IO;
        }
        int r = find_note(name, type, data, ph[i].p_filesz, func, cookie);
        if (r == NO_ERROR) {
            return r;
        }
    }
    return ERR_NOT_FOUND;
}

typedef struct {
    magenta_note_driver_t drv;
    mx_bind_inst_t bi[0];
} drivernote;

typedef struct {
    void* cookie;
    void (*func)(magenta_note_driver_t*, mx_bind_inst_t*, void*);
} context;

static mx_status_t callback(void* note, size_t sz, void* _ctx) {
    context* ctx = _ctx;
    drivernote* dn = note;
    if (sz < sizeof(drivernote)) {
        return ERR_INTERNAL;
    }
    size_t max = (sz - sizeof(drivernote)) / sizeof(mx_bind_inst_t);
    if (dn->drv.bindcount > max) {
        return ERR_INTERNAL;
    }
    ctx->func(&dn->drv, dn->bi, ctx->cookie);
    return NO_ERROR;
}

mx_status_t read_driver_info(int fd, void *cookie,
                             void (*func)(magenta_note_driver_t* note,
                                          mx_bind_inst_t* binding,
                                          void *cookie)) {
    context ctx = {
        .cookie = cookie,
        .func = func,
    };
    uint8_t data[4096];
    return for_each_note(fd, "Magenta", MAGENTA_NOTE_DRIVER,
                         data, sizeof(data), callback, &ctx);
}
