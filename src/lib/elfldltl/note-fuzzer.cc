// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/note.h>
#include <zircon/assert.h>

#include <string_view>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

namespace {

using namespace std::literals;

template <elfldltl::ElfData Data>
int Fuzz(elfldltl::ElfNote::Bytes data) {
  {
    // Adjust the data pointer to skip any unaligned prefix.
    using Nhdr = typename elfldltl::ElfNoteSegment<Data>::Nhdr;
    void* align_ptr = const_cast<void*>(static_cast<const void*>(data.data()));
    size_t align_space = data.size();
    for (size_t size = data.size();; --size) {
      if (size == 0) {
        data = {};
        break;
      }
      void* aligned = std::align(alignof(Nhdr), size, align_ptr, align_space);
      if (aligned) {
        data = {static_cast<const std::byte*>(aligned), size};
        break;
      }
    }
  }

  for (const auto& note : elfldltl::ElfNoteSegment<Data>(data)) {
    // This should ensure that the data actually gets copied out so it's valid.
    std::vector<char> name{note.name.begin(), note.name.end()};
    ZX_ASSERT(name.size() == note.name.size());
    ZX_ASSERT(!memcmp(name.data(), note.name.data(), name.size()));

    std::vector<std::byte> desc{note.desc.begin(), note.desc.end()};
    ZX_ASSERT(desc.size() == note.desc.size());
    ZX_ASSERT(!memcmp(desc.data(), note.desc.data(), desc.size()));

    ZX_ASSERT(note.HexSize() == 2 * note.desc.size());
    std::vector<char> hex;
    note.HexDump([&hex](char c) { hex.push_back(c); });
    ZX_ASSERT(hex.size() == note.HexSize());

    char buf[17];
    std::string_view hexs = note.HexString(buf);
    ZX_ASSERT(hexs.size() <= sizeof(buf));
    ZX_ASSERT(hexs.size() <= hex.size());
    ZX_ASSERT(!memcmp(hexs.data(), hex.data(), hexs.size()));

    bool is_gnu = note.name == "GNU\0"sv;
    ZX_ASSERT(note.Is("GNU") == is_gnu);

    bool is_buildid =
        is_gnu && note.type == static_cast<uint32_t>(elfldltl::ElfNoteType::kGnuBuildId);
    ZX_ASSERT(note.IsBuildId() == is_buildid);
  }

  return 0;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  bool little = provider.ConsumeBool();
  auto blob = provider.ConsumeRemainingBytes<std::byte>();
  return (little ? Fuzz<elfldltl::ElfData::k2Lsb>
                 : Fuzz<elfldltl::ElfData::k2Msb>)({blob.data(), blob.size()});
}
