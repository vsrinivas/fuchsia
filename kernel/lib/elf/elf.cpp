// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/elf.h>

#include <arch/ops.h>
#include <assert.h>
#include <debug.h>
#include <endian.h>
#include <err.h>
#include <kernel/vm/vm_aspace.h>
#include <lib/bio.h>
#include <new.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

namespace ElfLoader {

// conditionally define a 32 or 64 bit version of the data structures
// we care about, based on our bitness.
#if WITH_ELF32
#define ELF_OFF_PRINT_U "%u"
#define ELF_OFF_PRINT_X "%x"
#define ELF_ADDR_PRINT_U "%u"
#define ELF_ADDR_PRINT_X "%x"
#else
#define ELF_OFF_PRINT_U "%llu"
#define ELF_OFF_PRINT_X "%llx"
#define ELF_ADDR_PRINT_U "%llu"
#define ELF_ADDR_PRINT_X "%llx"
#endif

static int verify_eheader(const elf_ehdr* eheader) {
    if (memcmp(eheader->e_ident, ELF_MAGIC, 4) != 0) return ERR_NOT_FOUND;

#if WITH_ELF32
    if (eheader->e_ident[EI_CLASS] != ELFCLASS32) return ERR_NOT_FOUND;
#else
    if (eheader->e_ident[EI_CLASS] != ELFCLASS64) return ERR_NOT_FOUND;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
    if (eheader->e_ident[EI_DATA] != ELFDATA2LSB) return ERR_NOT_FOUND;
#elif BYTE_ORDER == BIG_ENDIAN
    if (eheader->e_ident[EI_DATA] != ELFDATA2MSB) return ERR_NOT_FOUND;
#endif

    if (eheader->e_ident[EI_VERSION] != EV_CURRENT) return ERR_NOT_FOUND;

    if (eheader->e_phoff == 0) return ERR_NOT_FOUND;

    if (eheader->e_phentsize < sizeof(elf_phdr)) return ERR_NOT_FOUND;

#if ARCH_ARM
    if (eheader->e_machine != EM_ARM) return ERR_NOT_FOUND;
#elif ARCH_ARM64
    if (eheader->e_machine != EM_AARCH64) return ERR_NOT_FOUND;
#elif ARCH_X86_64
    if (eheader->e_machine != EM_X86_64) return ERR_NOT_FOUND;
#elif ARCH_X86_32
    if (eheader->e_machine != EM_386) return ERR_NOT_FOUND;
#elif ARCH_MICROBLAZE
    if (eheader->e_machine != EM_MICROBLAZE) return ERR_NOT_FOUND;
#else
#error find proper EM_ define for your machine
#endif

    return NO_ERROR;
}

status_t File::Load() {
    // validate that this is an ELF file
    size_t bytes_read;
    status_t err = Read(&eheader_, 0, sizeof(eheader_), &bytes_read);
    if (err < 0 || bytes_read < sizeof(eheader_)) {
        LTRACEF("couldn't read elf header\n");
        return ERR_NOT_FOUND;
    }

    if (verify_eheader(reinterpret_cast<const elf_ehdr*>(&eheader_))) {
        LTRACEF("header not valid\n");
        return ERR_NOT_FOUND;
    }

    // sanity check number of program headers
    LTRACEF("number of program headers %u, entry size %u\n", eheader_.e_phnum,
            eheader_.e_phentsize);
    if (eheader_.e_phnum > 16 || eheader_.e_phentsize != sizeof(elf_phdr)) {
        LTRACEF("too many program headers or bad size\n");
        return ERR_NO_MEMORY;
    }

    // at this point e_phentsize == sizeof(elf_phdr) so we can use the sizeof

    AllocChecker ac;

    // allocate and read in the program headers
    size_t buffer_size = eheader_.e_phnum * sizeof(elf_phdr);
    pheaders_.reset(new (&ac) elf_phdr[eheader_.e_phnum], eheader_.e_phnum);
    if (!ac.check()) {
        LTRACEF("failed to allocate memory for program headers\n");
        return ERR_NO_MEMORY;
    }

    err = Read(pheaders_.get(), eheader_.e_phoff, buffer_size, &bytes_read);
    if (err < 0 || bytes_read < buffer_size) {
        LTRACEF("failed to read program headers\n");
        return ERR_NO_MEMORY;
    }

    LTRACEF("program headers:\n");
    uint seg_num = 0;
    for (uint i = 0; i < eheader_.e_phnum; i++) {
        // parse the program headers
        elf_phdr* pheader = &pheaders_[i];

        LTRACEF("%u: type %u offset 0x" ELF_OFF_PRINT_X " vaddr " ELF_ADDR_PRINT_X
                " paddr " ELF_ADDR_PRINT_X " memsiz " ELF_ADDR_PRINT_U " filesize " ELF_ADDR_PRINT_U
                "\n",
                i, pheader->p_type, pheader->p_offset, pheader->p_vaddr, pheader->p_paddr,
                pheader->p_memsz, pheader->p_filesz);

        // we only care about PT_LOAD segments at the moment
        if (pheader->p_type == PT_LOAD) {
            // allocate a block of memory to back the segment
            status_t err = AllocateSegment((vaddr_t)pheader->p_vaddr, pheader->p_memsz, seg_num);
            if (err < 0) {
                LTRACEF("mem hook failed, abort\n");
                // XXX clean up what we got so far
                return err;
            }

            void* ptr = (void*)(uintptr_t)pheader->p_vaddr;

            // read the file portion of the segment into memory at vaddr
            LTRACEF("reading segment at offset 0x" ELF_OFF_PRINT_X " to address %p\n",
                    pheader->p_offset, ptr);
            err = Read(seg_num, 0, pheader->p_offset, pheader->p_filesz, &bytes_read);
            if (err < 0 || bytes_read < pheader->p_filesz) {
                LTRACEF("error %d (bytes_read %zu) reading program header %u\n", err, bytes_read,
                        i);
                return (err < 0) ? err : ERR_IO;
            }

            // zero out he difference between memsz and filesz
            size_t tozero = pheader->p_memsz - pheader->p_filesz;
            if (tozero > 0) {
                LTRACEF("zeroing memory in segment %u, offset 0x%zx, size 0x%zx\n", seg_num,
                        (size_t)pheader->p_filesz, tozero);

                ZeroSegment(seg_num, pheader->p_filesz, tozero);
            }

            // track the number of load segments we have seen to pass the mem alloc hook
            seg_num++;
        }
    }

    // save the entry point
    entry_ = eheader_.e_entry;

    loaded_ = true;

    FreeResources();

    return NO_ERROR;
}

status_t ToMemFile::AllocateSegment(vaddr_t vaddr, size_t len, uint seg_num) {
    LTRACEF("%p, vaddr 0x%lx, len %zu, seg_num %u\n", this, vaddr, len, seg_num);

    if (seg_num >= SEG_COUNT) return ERR_INVALID_ARGS;

    // make sure the segment isn't already loaded
    if (seg_[seg_num].va) return ERR_INVALID_ARGS;

    char name[32];
    snprintf(name, sizeof(name), "%s_seg%u", name_, seg_num);

    // allocate a vm object to back this segment
    auto vmo = VmObject::Create(PMM_ALLOC_FLAG_ANY, ROUNDUP(len, PAGE_SIZE));
    if (!vmo) return ERR_NO_MEMORY;

    // map it into the address space
    void* ptr = (void*)vaddr;
    auto err =
        aspace_->MapObject(vmo, name, 0, len, &ptr, 0, VMM_FLAG_VALLOC_SPECIFIC | VMM_FLAG_COMMIT,
                           aspace_->is_user() ? ARCH_MMU_FLAG_PERM_USER : 0);
    if (err < 0) return err;

    // save the pointer
    seg_[seg_num].vmo = mxtl::move(vmo);
    seg_[seg_num].va = (vaddr_t)ptr;
    seg_[seg_num].len = len;

    return NO_ERROR;
}

// zero memory within a particular segment, if overridden
// XXX temporary until pmm gives us zeroed pages
status_t ToMemFile::ZeroSegment(uint seg_num, size_t seg_offset, size_t len) {
    LTRACEF("%p, seg num %u, seg_offset 0x%zx, len %zu\n", this, seg_num, seg_offset, len);

    size_t offset = seg_offset;
    VmObject* vmo = seg_[seg_num].vmo.get();

    size_t end = offset + len;
    while (offset < end) {
        size_t page_offset = offset % PAGE_SIZE;
        size_t tozero = MIN(end - offset, PAGE_SIZE - page_offset);

        LTRACEF("offset 0x%zx, tozero 0x%zx\n", offset, tozero);

        // TODO: move this logic to using a write method on the vm object directly

        // get a pointer to the underlying page in the object
        auto p = vmo->FaultPage(ROUNDDOWN(offset, PAGE_SIZE), VMM_PF_FLAG_WRITE);
        LTRACEF("page %p\n", p);
        if (!p) {
            panic("unhandled bad fault while reading into vm object\n");
        }

        // compute the kernel mapping of this page
        paddr_t pa = vm_page_to_paddr(p);
        LTRACEF("pa 0x%lx\n", pa);

        uint8_t* ptr = (uint8_t*)paddr_to_kvaddr(pa);
        ptr += page_offset;
        LTRACEF("ptr %p\n", ptr);

        // memset it
        memset(ptr, 0, tozero);

        // get to the next page boundary
        size_t tonext = PAGE_SIZE - page_offset;
        offset += tonext;
    }

    return NO_ERROR;
}

ToMemFile::segment ToMemFile::GetSegment(uint seg_num) {
    if (seg_num >= SEG_COUNT) return {};

    return seg_[seg_num];
}

// memory to memory loader

MemFile::MemFile(const char* name, mxtl::RefPtr<VmAspace> aspace, const void* src_ptr,
                 size_t src_len)
    : ToMemFile(name, aspace), src_ptr_(src_ptr), src_len_(src_len) {}

MemFile::~MemFile() {}

// memory from the source into the segment at a particular offset
status_t MemFile::Read(uint seg_num, size_t seg_offset, size_t source_offset, size_t len,
                       size_t* bytes_read) {
    LTRACEF("%p, seg %u, seg_offset %zu source_offset 0x%zx, len %zu\n", this, seg_num, seg_offset,
            source_offset, len);

    *bytes_read = 0;
    if (seg_num >= SEG_COUNT) return ERR_INVALID_ARGS;
    if (!seg_[seg_num].va) return ERR_INVALID_ARGS;

    size_t toread = len;
    if (source_offset >= src_len_) toread = 0;
    if (source_offset + len >= src_len_) toread = src_len_ - source_offset;

    VmObject* vmo = seg_[seg_num].vmo.get();
    DEBUG_ASSERT(vmo);

    size_t bytes_written;
    status_t err = vmo->Write((const uint8_t*)src_ptr_ + source_offset, seg_offset, toread, &bytes_written);
    if (err < 0) return err;
    if (bytes_written != toread) return ERR_IO;

    *bytes_read = toread;
    return NO_ERROR;
}

// memory from the source into memory
status_t MemFile::Read(void* ptr, size_t source_offset, size_t len, size_t* bytes_read) {
    LTRACEF("%p, ptr %p source_offset 0x%zx, len %zu\n", this, ptr, source_offset, len);

    *bytes_read = 0;

    ssize_t toread = len;
    if (source_offset >= src_len_) toread = 0;
    if (source_offset + len >= src_len_) toread = src_len_ - source_offset;

    memcpy(ptr, (const uint8_t*)src_ptr_ + source_offset, toread);

    *bytes_read = toread;
    return NO_ERROR;
}

// block device to memory loader
BioToMemFile::BioToMemFile(const char* name, mxtl::RefPtr<VmAspace> aspace, bdev* bdev,
                           uint64_t bdev_offset, uint64_t bdev_len)
    : ToMemFile(name, aspace), bdev_(bdev), bdev_offset_(bdev_offset), bdev_len_(bdev_len) {}

BioToMemFile::~BioToMemFile() {}

// memory from the source into the segment at a particular offset
status_t BioToMemFile::Read(uint seg_num, size_t seg_offset, size_t source_offset, size_t len,
                            size_t* bytes_read) {
    LTRACEF("%p, seg %u, seg_offset %zu source_offset 0x%zx, len %zu\n", this, seg_num, seg_offset,
            source_offset, len);

    *bytes_read = 0;
    if (seg_num >= SEG_COUNT) return ERR_INVALID_ARGS;
    if (!seg_[seg_num].va) return ERR_INVALID_ARGS;

    size_t offset = seg_offset;
    VmObject* vmo = seg_[seg_num].vmo.get();
    DEBUG_ASSERT(vmo);

    AllocChecker ac;
    // allocate a temporary buffer for the read operation
    mxtl::Array<uint8_t> temp_buf(new (&ac) uint8_t[PAGE_SIZE], PAGE_SIZE);
    if (!ac.check()) return ERR_NO_MEMORY;

    size_t end = offset + len;
    while (offset < end) {
        size_t toread = MIN(end - offset, PAGE_SIZE);
        size_t page_offset = offset % PAGE_SIZE;

        LTRACEF("offset 0x%zx, toread 0x%zx\n", offset, toread);

        // to the bio read into our temporary buffer
        ssize_t readerr = bio_read(bdev_, temp_buf.get(), source_offset + bdev_offset_, toread);
        if (readerr < 0 || static_cast<size_t>(readerr) != toread) {
            return ERR_IO;
        }

        // write the data out to the object
        size_t bytes_written;
        status_t err = vmo->Write(temp_buf.get(), offset, toread, &bytes_written);
        if (err < 0) return err;
        if (bytes_written != toread) return ERR_IO;

        // get to the next page boundary
        size_t tonext = PAGE_SIZE - page_offset;
        offset += tonext;
        source_offset += tonext;
        *bytes_read += tonext;
    }

    return NO_ERROR;
}

// memory from the source into memory
status_t BioToMemFile::Read(void* ptr, size_t source_offset, size_t len, size_t* bytes_read) {
    LTRACEF("%p, ptr %p source_offset 0x%zx, len %zu\n", this, ptr, source_offset, len);

    *bytes_read = 0;
    ssize_t ret = bio_read(bdev_, ptr, source_offset + bdev_offset_, len);
    if (ret < 0) return static_cast<status_t>(ret);
    *bytes_read = static_cast<size_t>(ret);
    return NO_ERROR;
}

} // ElfLoader
