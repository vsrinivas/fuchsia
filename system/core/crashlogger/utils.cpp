// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/status.h>

#include "utils.h"

// Same as basename, except will not modify |file|.
// This assumes there are no trailing /s. If there are then |file| is returned
// as is.

const char* cl_basename(const char* s) {
    // This implementation is copied from musl's basename.c.
    size_t i;
    if (!s || !*s)
        return ".";
    i = strlen(s) - 1;
    if (i > 0 && s[i] == '/')
        return s;
    for (; i && s[i - 1] != '/'; i--)
        ;
    return s + i;
}

int debug_level = 0;

void do_print_debug(const char* file, int line, const char* func, const char* fmt, ...) {
    fflush(stdout);
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d: %s: ", base, line, func);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr); // TODO: output is getting lost
}

void do_print_error(const char* file, int line, const char* fmt, ...) {
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "crashlogger: %s:%d: ", base, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void do_print_mx_error(const char* file, int line, const char* what, mx_status_t status) {
    do_print_error(file, line, "%s: %d (%s)",
                   what, status, mx_status_get_string(status));
}

// While this should never fail given a valid handle,
// returns MX_KOID_INVALID on failure.

mx_koid_t get_koid(mx_handle_t handle) {
    mx_info_handle_basic_t info;
    mx_size_t size = 0;
    mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, sizeof(info.rec),
                       &info, sizeof(info), &size);
    if (size == sizeof(info))
        return info.rec.koid;
    // This shouldn't ever happen, so don't just ignore it.
    fprintf(stderr, "Eh? MX_INFO_HANDLE_BASIC failed\n");
    // OTOH we can't just fail, we have to be robust about reporting back
    // to the kernel that we handled the exception.
    // TODO: Provide ability to safely terminate at any point (e.g., for assert
    // failures and such).
    return MX_KOID_INVALID;
}

mx_status_t read_mem(mx_handle_t h, mx_vaddr_t vaddr, void* ptr, size_t len) {
    size_t actual;
    mx_status_t status = mx_process_read_memory(h, vaddr, ptr, len, &actual);
    if (status < 0) {
        printf("read_mem @%p FAILED %zd\n", (void*) vaddr, len);
        return status;
    }
    if (len != actual) {
        printf("read_mem @%p FAILED, short read %zd\n", (void*) vaddr, len);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t fetch_string(mx_handle_t h, mx_vaddr_t vaddr, char* ptr, size_t max) {
    while (max > 1) {
        mx_status_t status;
        if ((status = read_mem(h, vaddr, ptr, 1)) < 0) {
            *ptr = 0;
            return status;
        }
        ptr++;
        vaddr++;
        max--;
    }
    *ptr = 0;
    return NO_ERROR;
}

#if UINT_MAX == ULONG_MAX

#define ehdr_off_phoff offsetof(Elf32_Ehdr, e_phoff)
#define ehdr_off_phnum offsetof(Elf32_Ehdr, e_phnum)

#define phdr_off_type offsetof(Elf32_Phdr, p_type)
#define phdr_off_offset offsetof(Elf32_Phdr, p_offset)
#define phdr_off_filesz offsetof(Elf32_Phdr, p_filesz)

typedef Elf32_Half elf_half_t;
typedef Elf32_Off elf_off_t;
// ELF used "word" for 32 bits, sigh.
typedef Elf32_Word elf_word_t;
typedef Elf32_Word elf_native_word_t;
typedef Elf32_Phdr elf_phdr_t;

#else

#define ehdr_off_phoff offsetof(Elf64_Ehdr, e_phoff)
#define ehdr_off_phnum offsetof(Elf64_Ehdr, e_phnum)

#define phdr_off_type offsetof(Elf64_Phdr, p_type)
#define phdr_off_offset offsetof(Elf64_Phdr, p_offset)
#define phdr_off_filesz offsetof(Elf64_Phdr, p_filesz)

typedef Elf64_Half elf_half_t;
typedef Elf64_Off elf_off_t;
typedef Elf64_Word elf_word_t;
typedef Elf64_Xword elf_native_word_t;
typedef Elf64_Phdr elf_phdr_t;

#endif

mx_status_t fetch_build_id(mx_handle_t h, mx_vaddr_t base, char* buf, size_t buf_size) {
    mx_vaddr_t vaddr = base;
    uint8_t tmp[4];
    mx_status_t status;

    if (buf_size < MAX_BUILDID_SIZE * 2 + 1)
        return ERR_INVALID_ARGS;

    status = read_mem(h, vaddr, tmp, 4);
    if (status != NO_ERROR)
        return status;
    if (memcmp(tmp, ELFMAG, SELFMAG))
        return ERR_WRONG_TYPE;

    elf_off_t phoff;
    elf_half_t num;
    status = read_mem(h, vaddr + ehdr_off_phoff, &phoff, sizeof(phoff));
    if (status != NO_ERROR)
        return status;
    status = read_mem(h, vaddr + ehdr_off_phnum, &num, sizeof(num));
    if (status != NO_ERROR)
        return status;

    for (unsigned n = 0; n < num; n++) {
        mx_vaddr_t phaddr = vaddr + phoff + (n * sizeof(elf_phdr_t));
        elf_word_t type;
        status = read_mem(h, phaddr + phdr_off_type, &type, sizeof(type));
        if (status != NO_ERROR)
            return status;
        if (type != PT_NOTE)
            continue;

        elf_off_t off;
        elf_native_word_t size;
        status = read_mem(h, phaddr + phdr_off_offset, &off, sizeof(off));
        if (status != NO_ERROR)
            return status;
        status = read_mem(h, phaddr + phdr_off_filesz, &size, sizeof(size));
        if (status != NO_ERROR)
            return status;

        struct {
            Elf32_Nhdr hdr;
            char name[sizeof("GNU")];
        } hdr;
        while (size > sizeof(hdr)) {
            status = read_mem(h, vaddr + off, &hdr, sizeof(hdr));
            if (status != NO_ERROR)
                return status;
            size_t header_size =
                sizeof(Elf32_Nhdr) + ((hdr.hdr.n_namesz + 3) & -4);
            size_t payload_size = (hdr.hdr.n_descsz + 3) & -4;
            off += header_size;
            size -= header_size;
            mx_vaddr_t payload_vaddr = vaddr + off;
            off += payload_size;
            size -= payload_size;
            if (hdr.hdr.n_type != NT_GNU_BUILD_ID ||
                hdr.hdr.n_namesz != sizeof("GNU") ||
                memcmp(hdr.name, "GNU", sizeof("GNU")) != 0) {
                continue;
            }
            if (hdr.hdr.n_descsz > MAX_BUILDID_SIZE) {
                snprintf(buf, buf_size,
                         "build_id_too_large_%u", hdr.hdr.n_descsz);
            } else {
                uint8_t buildid[MAX_BUILDID_SIZE];
                status = read_mem(h, payload_vaddr, buildid, hdr.hdr.n_descsz);
                if (status != NO_ERROR)
                    return status;
                for (uint32_t i = 0; i < hdr.hdr.n_descsz; ++i) {
                    snprintf(&buf[i * 2], 3, "%02x", buildid[i]);
                }
            }
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}
