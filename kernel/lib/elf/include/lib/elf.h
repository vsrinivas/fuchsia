// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <lib/elf_defines.h>
#include <string.h>
#include <sys/types.h>
#include <utils/array.h>
#include <utils/ref_ptr.h>

// based on our bitness, support 32 or 64 bit elf
#if IS_64BIT
#define WITH_ELF64 1
#else
#define WITH_ELF32 1
#endif

// forward declaration
struct bdev;

namespace ElfLoader {

// some local typedefs to help disambiguate between 32 and 64bit
#if WITH_ELF32
using elf_ehdr = Elf32_Ehdr;
using elf_phdr = Elf32_Phdr;
#else
using elf_ehdr = Elf64_Ehdr;
using elf_phdr = Elf64_Phdr;
#endif

// pure virtual base class for loading a file from a generic place
class File {
public:
    File(const char* name = nullptr) {
        strlcpy(name_, name ? name : "unnamed", sizeof(name_));
    }
    File(const File &) = delete;
    File& operator=(const File &) = delete;

    virtual ~File() {}

    status_t Load();

    bool loaded() const {
        return loaded_;
    }
    addr_t load_address() const {
        return load_address_;
    }
    addr_t entry() const {
        return entry_;
    }

    // subclass overloaded versions for allocating and reading data

    // Called by the loader to allocate memory for a segment.
    // Handle holds a pointer to the allocated memory
    virtual status_t AllocateSegment(vaddr_t vaddr, size_t len, uint seg_num) = 0;

    // memory from the source into the segment at a particular offset
    virtual status_t Read(uint seg_num, size_t seg_offset, size_t source_offset, size_t len,
                          size_t* bytes_read) = 0;

    // memory from the source into memory
    virtual status_t Read(void* ptr, size_t source_offset, size_t len, size_t* bytes_read) = 0;

    // called when done loading, subclass can free any resources it had
    virtual void FreeResources() {}

    // zero memory within a particular segment, if overridden
    virtual status_t ZeroSegment(uint seg_num, size_t seg_offset, size_t len) {
        return NO_ERROR;
    }

protected:
    char name_[32];

private:
    bool loaded_ = false;

    // after loading, these hold the load address and entry point
    addr_t load_address_ = 0;
    addr_t entry_ = 0;

    // loaded info about the elf file
    elf_ehdr eheader_ = {};           // a copy of the main elf header
    utils::Array<elf_phdr> pheaders_; // a pointer to a buffer of program headers
};

// partial implementation for loading from something to vmm allocated memory
class ToMemFile : public File {
public:
    ToMemFile(const char* name, utils::RefPtr<VmAspace> aspace)
        : File(name), aspace_(utils::move(aspace)) {}
    ToMemFile(const ToMemFile &) = delete;
    ToMemFile& operator=(const ToMemFile &) = delete;
    virtual ~ToMemFile() override {}

    // Called by the loader to allocate memory for a segment.
    // Handle holds a pointer to the allocated memory
    virtual status_t AllocateSegment(vaddr_t vaddr, size_t len, uint seg_num) override;

    // zero memory within a particular segment, if overridden
    virtual status_t ZeroSegment(uint seg_num, size_t seg_offset, size_t len) override;

    // a loaded segment
    struct segment {
        utils::RefPtr<VmObject> vmo;
        vaddr_t va;
        size_t len;
    };

    // return a segment
    segment GetSegment(uint seg_num);

protected:
    utils::RefPtr<VmAspace> aspace_;

    static const size_t SEG_COUNT = 16;
    segment seg_[SEG_COUNT] = {};
};

// fully overloaded implementation for loading from memory to vmm allocated memory
class MemFile : public ToMemFile {
public:
    MemFile(const char* name, utils::RefPtr<VmAspace> aspace, const void* src_ptr, size_t src_len);
    MemFile(const MemFile &) = delete;
    MemFile& operator=(const MemFile &) = delete;
    virtual ~MemFile() override;

    // memory from the source into the segment at a particular offset
    virtual status_t Read(uint seg_num, size_t seg_offset, size_t source_offset, size_t len,
                          size_t* bytes_read) override;

    // memory from the source into a buffer used by the loader
    virtual status_t Read(void* ptr, size_t source_offset, size_t len, size_t* bytes_read) override;

private:
    const void* src_ptr_;
    size_t src_len_;
};

// fully overloaded implementation for loading from a block device to vmm allocated memory
class BioToMemFile : public ToMemFile {
public:
    BioToMemFile(const char* name, utils::RefPtr<VmAspace> aspace, struct bdev* bdev,
                 uint64_t bdev_offset, uint64_t bdev_len);
    BioToMemFile(const BioToMemFile &) = delete;
    BioToMemFile& operator=(const BioToMemFile &) = delete;
    virtual ~BioToMemFile() override;

    // memory from the source into the segment at a particular offset
    virtual status_t Read(uint seg_num, size_t seg_offset, size_t source_offset, size_t len,
                          size_t* bytes_read) override;

    // memory from the source into a buffer used by the loader
    virtual status_t Read(void* ptr, size_t source_offset, size_t len, size_t* bytes_read) override;

private:
    bdev* bdev_;
    uint64_t bdev_offset_;
    uint64_t bdev_len_;
};

}; // namespace Elf
