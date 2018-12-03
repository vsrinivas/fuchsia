// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_ELFLIB_ELFLIB_H_
#define GARNET_LIB_ELFLIB_ELFLIB_H_

#include <map>
#include <memory>
#include <vector>

#include "garnet/lib/elflib/third_party/musl/include/elf.h"
#include "garnet/public/lib/fxl/macros.h"

namespace elflib {

class ElfLib {
 public:
  // Proxy object for whatever address space we're exploring.
  class MemoryAccessor {
   public:
    virtual ~MemoryAccessor() = default;
    // Get memory from the process relative to the base of this lib. That means
    // offset 0 should point to the Elf64_Ehdr. The vector should be sized to
    // the amount of data you want to read.
    virtual bool GetMemory(uint64_t offset, std::vector<uint8_t>* out);
  };

  // Do not use. See Create.
  explicit ElfLib(std::unique_ptr<MemoryAccessor>&& memory);

  virtual ~ElfLib();

  // Get the header for a section by its name.
  template <typename T>
  const std::vector<T>* GetSectionData(const std::string& name);

 private:
  // Create a new ElfLib object.
  static bool Create(std::unique_ptr<MemoryAccessor>&& memory,
                     std::unique_ptr<ElfLib> out);

  // Get memory and cast it to a struct.
  template <typename T>
  bool GetDatum(uint64_t offset, T* out);

  // Get memory and cast it to a series of structs.
  template <typename T>
  bool GetData(uint64_t offset, size_t count, std::vector<T>* out);

  // Get the header for a section by its index.
  const Elf64_Shdr* GetSectionHeader(size_t section);

  // Get the contents of a section by its index.
  template <typename T>
  const std::vector<T>* GetSectionData(size_t section);

  // Get the section offset from the name. Returns true unless a lookup failed.
  bool GetSectionOffsetFromName(const std::string& name, size_t* out);

  // Get the header for a section by its name.
  const Elf64_Shdr* GetSectionHeader(const std::string& name);

  std::unique_ptr<MemoryAccessor> memory_;
  Elf64_Ehdr header_;
  std::vector<Elf64_Shdr> sections_;
  std::map<size_t, std::vector<uint8_t>> section_data_;
  std::map<std::string, size_t> section_names_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ElfLib);
};

}  // namespace elflib

#endif  // GARNET_LIB_ELFLIB_ELFLIB_H_
