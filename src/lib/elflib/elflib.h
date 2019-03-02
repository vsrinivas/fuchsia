// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_ELFLIB_ELFLIB_H_
#define GARNET_LIB_ELFLIB_ELFLIB_H_

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "garnet/public/lib/fxl/macros.h"
#include "garnet/third_party/llvm/include/llvm/BinaryFormat/ELF.h"

namespace elflib {

using namespace llvm::ELF;

class ElfLib {
 public:
  // Essentially just a pointer with a bound.
  struct MemoryRegion {
    const uint8_t* ptr;
    size_t size;
  };

  // Proxy object for whatever address space we're exploring.
  class MemoryAccessor {
   public:
    virtual ~MemoryAccessor() = default;

    // Retrieve the header for this ELF object. Create() will fail if this
    // returns an empty optional.
    virtual std::optional<Elf64_Ehdr> GetHeader() = 0;

    // Retrieve the section header table. The recorded offset and size are
    // passed. The callee just has to derference.
    virtual std::optional<std::vector<Elf64_Shdr>> GetSectionHeaders(
        uint64_t offset, size_t count) = 0;

    // Retrieve the program header table. The recorded offset and size are
    // passed. The callee just has to dereference.
    virtual std::optional<std::vector<Elf64_Phdr>> GetProgramHeaders(
        uint64_t offset, size_t count) = 0;

    // Get memory for a mapped area as specified by a section or segment. We're
    // given the dimensions both as we'd find them in the file and as we'd find
    // them in address space. The returned pointer can be nullptr on error, and
    // is otherwise expected to be valid for the lifetime of the MemoryAccessor
    // object. The size_t in the returned pair should be the size of the
    // addressable range at the returned pointer, and should be equal to either
    // mapped_address or file_size when the pointer is not nullptr. It is
    // otherwise undefined.
    virtual MemoryRegion GetLoadableMemory(uint64_t offset,
                                           uint64_t mapped_address,
                                           size_t file_size,
                                           size_t mapped_size) = 0;

    // Get memory for a mapped area specified by a segment. The memory is
    // assumed to only be accessible at the desired address after loading and
    // you should return std::nullopt if this isn't a loaded ELF object.
    // The returned pointer can be nullptr on error, and is otherwise expected
    // to be valid for the lifetime of the address space.
    virtual const uint8_t* GetLoadedMemory(uint64_t mapped_address,
                                           size_t mapped_size) = 0;
  };

  class MemoryAccessorForFile : public MemoryAccessor {
   public:
    // Get memory from the file based on its offset.
    virtual const uint8_t* GetMemory(uint64_t offset, size_t size) = 0;

    const uint8_t* GetLoadedMemory(uint64_t mapped_address,
                                   size_t mapped_size) override {
      return nullptr;
    }

    MemoryRegion GetLoadableMemory(uint64_t offset, uint64_t mapped_address,
                                   size_t file_size,
                                   size_t mapped_size) override {
      return MemoryRegion{.ptr = GetMemory(offset, file_size),
                          .size = file_size};
    }

    std::optional<Elf64_Ehdr> GetHeader() override;
    std::optional<std::vector<Elf64_Shdr>> GetSectionHeaders(
        uint64_t offset, size_t count) override;
    std::optional<std::vector<Elf64_Phdr>> GetProgramHeaders(
        uint64_t offset, size_t count) override;
  };

  class MemoryAccessorForAddressSpace : public MemoryAccessor {
   public:
    MemoryRegion GetLoadableMemory(uint64_t offset, uint64_t mapped_address,
                                   size_t file_size,
                                   size_t mapped_size) override {
      return MemoryRegion{.ptr = GetLoadedMemory(mapped_address, mapped_size),
                          .size = file_size};
    }

    std::optional<std::vector<Elf64_Shdr>> GetSectionHeaders(
        uint64_t offset, size_t count) override {
      return std::nullopt;
    }
  };

  // Do not use. See Create.
  explicit ElfLib(std::unique_ptr<MemoryAccessor>&& memory);

  virtual ~ElfLib();

  // Get the contents of a section by its name. Return nullptr if there is no
  // section by that name.
  MemoryRegion GetSectionData(const std::string& name);

  // Get a note from the notes section.
  std::optional<std::vector<uint8_t>> GetNote(const std::string& name,
                                              uint64_t type);

  // Get a symbol from the symbol table. Return nullptr if there is no such
  // symbol. Pointer should live as long as the memory accessor.
  const Elf64_Sym* GetSymbol(const std::string& name);

  // Get a map of all symbols and their string names. Returns nullopt if the
  // symbols could not be loaded.
  std::optional<std::map<std::string, Elf64_Sym>> GetAllSymbols();

  // Get a symbol from the symbol table. Return nullptr if there is no such
  // symbol. Pointer should live as long as the memory accessor.
  const Elf64_Sym* GetDynamicSymbol(const std::string& name);

  // Get a map of all symbols and their string names. Returns nullopt if the
  // symbols could not be loaded.
  std::optional<std::map<std::string, Elf64_Sym>> GetAllDynamicSymbols();

  // Create a new ElfLib object.
  static std::unique_ptr<ElfLib> Create(
      std::unique_ptr<MemoryAccessor>&& memory);

  // Returns a map from symbol names to the locations of their PLT entries.
  // Returns an empty map if the data is inaccessible.
  //
  // Getting this information is architecture-specific and involves reading and
  // decoding the actual jump table instructions in the .plt section. Once
  // we've done that decoding we can quickly get relocation indices and then
  // symbol table mappings.
  std::map<std::string, uint64_t> GetPLTOffsets();

 private:
  // Location of a section specified by data gleaned from the dynamic segment.
  struct DynamicSection {
    std::optional<uint64_t> offset;
    std::optional<size_t> size;

    bool IsValid() { return offset && size; }
  };

  // x64-specific implementation of GetPLTOffsets
  std::map<std::string, uint64_t> GetPLTOffsetsX64();

  // Get the header for a section by its index. Return nullptr if the index is
  // invalid.
  const Elf64_Shdr* GetSectionHeader(size_t section);

  // Load the program header table into the cache in segments_. Return true
  // unless a read error occurred.
  bool LoadProgramHeaders();

  // Load the section name-to-index mappings and cache them in section_names_.
  bool LoadSectionNames();

  // Get the contents of a section by its index. Return nullptr if the index is
  // invalid.
  MemoryRegion GetSectionData(size_t section);

  // Get the contents of a segment by its index. Return nullptr if the index is
  // invalid.
  MemoryRegion GetSegmentData(size_t segment);

  // Get the contents of the symbol table. Return nullptr if it is not present
  // or we do not have the means to locate it. Size is number of structs, not
  // number of bytes.
  std::pair<const Elf64_Sym*, size_t> GetSymtab();

  // Get the contents of the dynamic symbol table. Return nullptr if it is not
  // present or we do not have the means to locate it. Size is number of
  // structs, not number of bytes.
  std::pair<const Elf64_Sym*, size_t> GetDynamicSymtab();

  // Get a string from the .strtab section. Return nullptr if the index is
  // invalid.
  std::optional<std::string> GetString(size_t index);

  // Get a string from the .dynstr section. Return nullptr if the index is
  // invalid.
  std::optional<std::string> GetDynamicString(size_t index);

  // Load symbols from the dynamic segment of the target. We only do this when
  // the section data isn't available and we can't use the regular .symtab
  // information. Returns true unless an error occurred.
  bool LoadDynamicSymbols();

  bool did_load_dynamic_symbols_ = false;

  std::unique_ptr<MemoryAccessor> memory_;
  Elf64_Ehdr header_;
  std::optional<bool> dynamic_plt_use_rela_;

  DynamicSection dynsym_;
  DynamicSection dynstr_;
  std::vector<Elf64_Shdr> sections_;
  std::vector<Elf64_Phdr> segments_;
  std::map<size_t, std::vector<uint8_t>> section_data_;
  std::map<std::string, size_t> section_names_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ElfLib);
};

}  // namespace elflib

#endif  // GARNET_LIB_ELFLIB_ELFLIB_H_
