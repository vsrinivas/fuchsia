// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#include <dlfcn.h>

#include <ddk/binding.h>

#include <elf.h>


typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[0];
} notehdr;

typedef Elf64_Ehdr elfhdr;
typedef Elf64_Phdr elfphdr;

int find_note(const char* name, uint32_t type,
              void* data, size_t size,
              void (*func)(void* note, size_t sz, void* cookie),
              void* cookie) {
    size_t nlen = strlen(name) + 1;
    while (size >= sizeof(notehdr)) {
        // ignore padding between notes
        if (*((uint32_t*) data) == 0) {
            size -= sizeof(uint32_t);
            data += sizeof(uint32_t);
            continue;
        }
        notehdr* hdr = data;
        uint32_t nsz = (hdr->namesz + 3) & (~3);
        if (nsz > size) {
            break;
        }
        data += sizeof(notehdr) + nsz;
        size -= sizeof(notehdr) + nsz;

        uint32_t dsz = (hdr->descsz + 3) & (~3);
        if (dsz > size) {
            break;
        }

        if ((nsz >= nlen) && (memcmp(name, hdr->name, nlen) == 0)) {
            func(data, hdr->descsz, cookie);
        }

        data += dsz;
        size -= dsz;
    }
    return 0;
}

void for_each_note(int fd,
                   const char* name, uint32_t type,
                   void* data, size_t dsize,
                   void (*func)(void* note, size_t sz, void* cookie),
                   void* cookie) {
    elfphdr ph[64];
    elfhdr eh;
    if ((lseek(fd, 0, SEEK_SET) != 0)) {
        return;
    }
    if (read(fd, &eh, sizeof(eh)) != sizeof(eh)) {
        return;
    }
    if (memcmp(&eh, ELFMAG, 4) ||
        (eh.e_ehsize != sizeof(elfhdr)) ||
        (eh.e_phentsize != sizeof(elfphdr))) {
        return;
    }
    size_t sz = sizeof(elfphdr) * eh.e_phnum;
    if (sz > sizeof(ph)) {
        return;
    }
    if ((lseek(fd, eh.e_phoff, SEEK_SET) != (off_t)eh.e_phoff) ||
        (read(fd, ph, sz) != (ssize_t)sz)){
        return;
    }
    for (int i = 0; i < eh.e_phnum; i++) {
        if ((ph[i].p_type != PT_NOTE) ||
            (ph[i].p_filesz > dsize)) {
            continue;
        }
        if ((lseek(fd, ph[i].p_offset, SEEK_SET) != (off_t)ph[i].p_offset) ||
            (read(fd, data, ph[i].p_filesz) != (ssize_t)ph[i].p_filesz)) {
            return;
        }
        find_note(name, type, data, ph[i].p_filesz, func, cookie);
    }
}

typedef struct {
    magenta_note_driver_t drv;
    mx_bind_inst_t bi[0];
} drivernote;

void dump_note(void* note, size_t sz, void* cookie) {
    drivernote* dn = note;
    const char* fn = cookie;
    if (sz < sizeof(drivernote)) {
        return;
    }
    printf("\n[%s]\n", fn);
    printf("name:    %s\n", dn->drv.name);
    printf("vendor:  %s\n", dn->drv.vendor);
    printf("version: %s\n", dn->drv.version);
    size_t max = (sz - sizeof(drivernote)) / sizeof(mx_bind_inst_t);
    if (dn->drv.bindcount > max) {
        return;
    }
    printf("binding:\n");
    for (size_t n = 0; n < dn->drv.bindcount; n++) {
        printf(" %03zd: %08x %08x\n", n, dn->bi[n].op, dn->bi[n].arg);
    }
}

int main(int argc, char** argv) {
    while (argc > 1) {
        uint8_t data[4096];

        int fd;
        if ((fd = open(argv[1], O_RDONLY)) >= 0) {
            for_each_note(fd, "Magenta", 0x00010000,
                          data, sizeof(data), dump_note, argv[1]);
            close(fd);
        }
        argc--;
        argv++;
    }
}