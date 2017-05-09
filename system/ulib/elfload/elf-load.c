// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elfload/elfload.h>

#include <endian.h>
#include <limits.h>
#include <string.h>
#include <magenta/syscalls.h>

#if BYTE_ORDER == LITTLE_ENDIAN
# define MY_ELFDATA ELFDATA2LSB
#elif BYTE_ORDER == BIG_ENDIAN
# define MY_ELFDATA ELFDATA2MSB
#else
# error what byte order?
#endif

#if defined(__arm__)
# define MY_MACHINE EM_ARM
#elif defined(__aarch64__)
# define MY_MACHINE EM_AARCH64
#elif defined(__x86_64__)
# define MY_MACHINE EM_X86_64
#elif defined(__i386__)
# define MY_MACHINE EM_386
#else
# error what machine?
#endif

// NOTE!  All code in this file must maintain the invariants that it's
// purely position-independent and uses no writable memory other than
// its own stack.

// hdr_buf represents bytes already read from the start of the file.
mx_status_t elf_load_prepare(mx_handle_t vmo, const void* hdr_buf, size_t buf_sz,
                             elf_load_header_t* header, uintptr_t* phoff) {
    // Read the file header and validate basic format sanity.
    elf_ehdr_t ehdr;
    if (buf_sz >= sizeof(ehdr)) {
        memcpy(&ehdr, hdr_buf, sizeof(ehdr));
    } else {
        size_t n;
        mx_status_t status = mx_vmo_read(vmo, &ehdr, 0, sizeof(ehdr), &n);
        if (status != NO_ERROR)
            return status;
        if (n != sizeof(ehdr))
            return ERR_ELF_BAD_FORMAT;
    }
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr.e_ident[EI_MAG3] != ELFMAG3 ||
        ehdr.e_ident[EI_CLASS] != MY_ELFCLASS ||
        ehdr.e_ident[EI_DATA] != MY_ELFDATA ||
        ehdr.e_ident[EI_VERSION] != EV_CURRENT ||
        ehdr.e_phentsize != sizeof(elf_phdr_t) ||
        ehdr.e_phnum == PN_XNUM ||
        ehdr.e_machine != MY_MACHINE ||
        // This code could easily support loading fixed-address ELF files
        // (e_type == ET_EXEC).  But the system overall doesn't support
        // them.  It's Fuchsia policy that all executables must be PIEs.
        // So don't accept ET_EXEC files at all.
        ehdr.e_type != ET_DYN)
        return ERR_ELF_BAD_FORMAT;

    // Cache the few other bits we need from the header, and we're good to go.
    header->e_phnum = ehdr.e_phnum;
    header->e_entry = ehdr.e_entry;
    *phoff = ehdr.e_phoff;
    return NO_ERROR;
}

mx_status_t elf_load_read_phdrs(mx_handle_t vmo, elf_phdr_t phdrs[],
                                uintptr_t phoff, size_t phnum) {
    size_t phdrs_size = (size_t)phnum * sizeof(elf_phdr_t);
    size_t n;
    mx_status_t status = mx_vmo_read(vmo, phdrs, phoff, phdrs_size, &n);
    if (status != NO_ERROR)
        return status;
    if (n != phdrs_size)
        return ERR_ELF_BAD_FORMAT;
    return NO_ERROR;
}

// An ET_DYN file can be loaded anywhere, so choose where.  This
// allocates a VMAR to hold the image, and returns its handle and
// absolute address.  This also computes the "load bias", which is the
// difference between p_vaddr values in this file and actual runtime
// addresses.  (Usually the lowest p_vaddr in an ET_DYN file will be 0
// and so the load bias is also the load base address, but ELF does
// not require that the lowest p_vaddr be 0.)
static mx_status_t choose_load_bias(mx_handle_t root_vmar,
                                    const elf_load_header_t* header,
                                    const elf_phdr_t phdrs[],
                                    mx_handle_t* vmar,
                                    uintptr_t* vmar_base,
                                    uintptr_t* bias) {
    // This file can be loaded anywhere, so the first thing is to
    // figure out the total span it will need and reserve a span
    // of address space that big.  The kernel decides where to put it.

    uintptr_t low = 0, high = 0;
    for (uint_fast16_t i = 0; i < header->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint_fast16_t j = header->e_phnum;
            do {
                --j;
            } while (j > i && phdrs[j].p_type != PT_LOAD);
            low = phdrs[i].p_vaddr & -PAGE_SIZE;
            high = ((phdrs[j].p_vaddr +
                     phdrs[j].p_memsz + PAGE_SIZE - 1) & -PAGE_SIZE);
            break;
        }
    }
    // Sanity check.  ELF requires that PT_LOAD phdrs be sorted in
    // ascending p_vaddr order.
    if (low > high)
        return ERR_ELF_BAD_FORMAT;

    const size_t span = high - low;
    if (span == 0)
        return NO_ERROR;

    // Allocate a VMAR to reserve the whole address range.
    mx_status_t status = mx_vmar_allocate(root_vmar, 0, span,
                                          MX_VM_FLAG_CAN_MAP_READ |
                                          MX_VM_FLAG_CAN_MAP_WRITE |
                                          MX_VM_FLAG_CAN_MAP_EXECUTE |
                                          MX_VM_FLAG_CAN_MAP_SPECIFIC,
                                          vmar, vmar_base);
    if (status == NO_ERROR)
        *bias = *vmar_base - low;
    return status;
}

static mx_status_t finish_load_segment(
    mx_handle_t vmar, mx_handle_t vmo, const elf_phdr_t* ph,
    size_t start_offset, size_t size,
    uintptr_t file_start, uintptr_t file_end, size_t partial_page) {
    const uint32_t flags = MX_VM_FLAG_SPECIFIC |
        ((ph->p_flags & PF_R) ? MX_VM_FLAG_PERM_READ : 0) |
        ((ph->p_flags & PF_W) ? MX_VM_FLAG_PERM_WRITE : 0) |
        ((ph->p_flags & PF_X) ? MX_VM_FLAG_PERM_EXECUTE : 0);

    uintptr_t start;
    if (ph->p_filesz == ph->p_memsz)
        // Straightforward segment, map all the whole pages from the file.
        return mx_vmar_map(vmar, start_offset, vmo, file_start, size, flags,
                           &start);

    const size_t file_size = file_end - file_start;

    // This segment has some bss, so things are more complicated.
    // Only the leading portion is directly mapped in from the file.
    if (file_size > 0) {
        mx_status_t status = mx_vmar_map(vmar, start_offset, vmo, file_start,
                                         file_size, flags, &start);
        if (status != NO_ERROR)
            return status;

        start_offset += file_size;
        size -= file_size;
    }

    // The rest of the segment will be backed by anonymous memory.
    mx_handle_t bss_vmo;
    mx_status_t status = mx_vmo_create(size, 0, &bss_vmo);
    if (status != NO_ERROR)
        return status;

    // The final partial page of initialized data falls into the
    // region backed by bss_vmo rather than (the file) vmo.  We need
    // to read that data out of the file and copy it into bss_vmo.
    if (partial_page > 0) {
        char buffer[PAGE_SIZE];
        size_t n;
        status = mx_vmo_read(vmo, buffer, file_end, partial_page, &n);
        if (status != NO_ERROR) {
            mx_handle_close(bss_vmo);
            return status;
        }
        if (n != partial_page) {
            mx_handle_close(bss_vmo);
            return ERR_ELF_BAD_FORMAT;
        }
        status = mx_vmo_write(bss_vmo, buffer, 0, n, &n);
        if (status != NO_ERROR) {
            mx_handle_close(bss_vmo);
            return status;
        }
        if (n != partial_page) {
            mx_handle_close(bss_vmo);
            return ERR_IO;
        }
    }

    status = mx_vmar_map(vmar, start_offset, bss_vmo, 0, size, flags, &start);
    mx_handle_close(bss_vmo);

    return status;
}

static mx_status_t load_segment(mx_handle_t vmar, size_t vmar_offset,
                                mx_handle_t vmo, const elf_phdr_t* ph) {
    // The p_vaddr can start in the middle of a page, but the
    // semantics are that all the whole pages containing the
    // p_vaddr+p_filesz range are mapped in.
    size_t start = (size_t)ph->p_vaddr + vmar_offset;
    size_t end = start + ph->p_memsz;
    start &= -PAGE_SIZE;
    end = (end + PAGE_SIZE - 1) & -PAGE_SIZE;
    size_t size = end - start;

    // Nothing to do for an empty segment (degenerate case).
    if (size == 0)
        return NO_ERROR;

    uintptr_t file_start = (uintptr_t)ph->p_offset;
    uintptr_t file_end = file_start + ph->p_filesz;
    const size_t partial_page = file_end & (PAGE_SIZE - 1);
    file_start &= -PAGE_SIZE;
    file_end &= -PAGE_SIZE;

    uintptr_t data_end =
        (ph->p_offset + ph->p_filesz + PAGE_SIZE - 1) & -PAGE_SIZE;
    const size_t data_size = data_end - file_start;

    // With no writable data, it's the simple case.
    if (!(ph->p_flags & PF_W) || data_size == 0)
        return finish_load_segment(vmar, vmo, ph, start, size,
                                   file_start, file_end, partial_page);

    // For a writable segment, we need a writable VMO.
    mx_handle_t writable_vmo;
    mx_status_t status = mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                                      file_start, data_size, &writable_vmo);
    if (status == NO_ERROR) {
        status = finish_load_segment(vmar, writable_vmo, ph, start, size,
                                     0, file_end - file_start, partial_page);
        mx_handle_close(writable_vmo);
    }
    return status;
}

mx_status_t elf_load_map_segments(mx_handle_t root_vmar,
                                  const elf_load_header_t* header,
                                  const elf_phdr_t phdrs[],
                                  mx_handle_t vmo,
                                  mx_handle_t* segments_vmar,
                                  mx_vaddr_t* base, mx_vaddr_t* entry) {
    uintptr_t vmar_base = 0;
    uintptr_t bias = 0;
    mx_handle_t vmar = MX_HANDLE_INVALID;
    mx_status_t status = choose_load_bias(root_vmar, header, phdrs,
                                          &vmar, &vmar_base, &bias);

    size_t vmar_offset = bias - vmar_base;
    for (uint_fast16_t i = 0; status == NO_ERROR && i < header->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD)
            status = load_segment(vmar, vmar_offset, vmo, &phdrs[i]);
    }

    if (status == NO_ERROR && segments_vmar != NULL)
        *segments_vmar = vmar;
    else
        mx_handle_close(vmar);

    if (status == NO_ERROR) {
        if (base != NULL)
            *base = vmar_base;
        if (entry != NULL)
            *entry = header->e_entry != 0 ? header->e_entry + bias : 0;
    }
    return status;
}

bool elf_load_find_interp(const elf_phdr_t phdrs[], size_t phnum,
                          uintptr_t* interp_off, size_t* interp_len) {
    for (size_t i = 0; i < phnum; ++i) {
        if (phdrs[i].p_type == PT_INTERP) {
            *interp_off = phdrs[i].p_offset;
            *interp_len = phdrs[i].p_filesz;
            return true;
        }
    }
    return false;
}
