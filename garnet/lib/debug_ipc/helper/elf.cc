// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/elf.h"

#include "garnet/lib/elflib/elflib.h"

namespace debug_ipc {

namespace {

constexpr size_t kMaxBuildIDSize = 64;

constexpr uint64_t kNoteGnuBuildId = 3;

}  // namespace

using elflib::Elf64_Ehdr;
using elflib::Elf64_Phdr;
using elflib::Elf64_Shdr;
using elflib::ElfLib;

class Accessor : public ElfLib::MemoryAccessorForAddressSpace {
 public:
  Accessor(
      std::function<bool(uint64_t offset, void* buffer, size_t length)> read_fn)
      : read_fn_(std::move(read_fn)) {}

  const uint8_t* GetLoadedMemory(uint64_t offset, size_t size) override {
    auto& out = data_.emplace_back();
    out.resize(size);

    if (read_fn_(offset, out.data(), size)) {
      return out.data();
    }

    return nullptr;
  }

  std::optional<Elf64_Ehdr> GetHeader() override {
    auto data = GetLoadedMemory(0, sizeof(Elf64_Ehdr));

    if (!data) {
      return std::nullopt;
    }

    return *reinterpret_cast<const Elf64_Ehdr*>(data);
  }

  std::optional<std::vector<Elf64_Phdr>> GetProgramHeaders(
      uint64_t offset, size_t count) override {
    auto data = GetLoadedMemory(offset, sizeof(Elf64_Phdr) * count);

    if (!data) {
      return std::nullopt;
    }

    auto array = reinterpret_cast<const Elf64_Phdr*>(data);

    return std::vector<Elf64_Phdr>(array, array + count);
  }

  std::optional<std::vector<Elf64_Shdr>> GetSectionHeaders(
      uint64_t offset, size_t count) override {
    return std::nullopt;
  }

 private:
  std::vector<std::vector<uint8_t>> data_;
  std::function<bool(uint64_t offset, void* buffer, size_t length)> read_fn_;
};

std::string ExtractBuildID(
    std::function<bool(uint64_t offset, void* buffer, size_t length)> read_fn) {
  // The buffer will hold a hex version of the build ID (2 chars per byte)
  // plus the null terminator (1 more).
  constexpr size_t buf_size = kMaxBuildIDSize * 2 + 1;
  char buf[buf_size];

  auto elf = ElfLib::Create(std::make_unique<Accessor>(read_fn));

  if (!elf) {
    return std::string();
  }

  auto note = elf->GetNote("GNU", kNoteGnuBuildId);

  if (note && note->size() <= kMaxBuildIDSize) {
    size_t i = 0;
    for (const auto& c : *note)
      snprintf(&buf[i++ * 2], 3, "%02x", c);
    return std::string(buf);
  }

  return std::string();
}

std::string ExtractBuildID(FILE* file) {
  return ExtractBuildID([file](uint64_t offset, void* buffer, size_t length) {
    if (fseek(file, offset, SEEK_SET) != 0)
      return false;
    return fread(buffer, 1, length, file) == length;
  });
}

#if defined(__Fuchsia__)
std::string ExtractBuildID(const zx::process& process, uint64_t base) {
  return ExtractBuildID([&process, base](uint64_t offset, void* buffer,
                                         size_t length) {
    size_t num_read = 0;
    if (process.read_memory(base + offset, buffer, length, &num_read) != ZX_OK)
      return false;
    return num_read == length;
  });
}
#endif

}  // namespace debug_ipc
