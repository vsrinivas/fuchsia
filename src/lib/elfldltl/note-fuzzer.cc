// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/fuzzer.h>
#include <lib/elfldltl/note.h>
#include <zircon/assert.h>

#include <string_view>
#include <vector>

namespace {

using namespace std::literals;

template <elfldltl::ElfData Data>
struct NoteFuzzer {
  int operator()(FuzzedDataProvider& provider) const {
    elfldltl::FuzzerInput<sizeof(uint32_t), std::byte> input(provider);
    auto [bytes] = input.as_bytes();
    elfldltl::ElfNote::Bytes data{bytes.data(), bytes.size()};

    for (const auto& note : elfldltl::ElfNoteSegment<Data>(data)) {
      // This should ensure the data actually gets copied out so it's checked.
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
};

using Fuzzer = elfldltl::ElfDataFuzzer<NoteFuzzer>;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  return Fuzzer{}(provider);
}
