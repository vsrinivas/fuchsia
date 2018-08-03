// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <fs/pseudo-dir.h>
#include <fs/pseudo-file.h>
#include <lib/fxl/strings/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/syscalls.h>

namespace {

std::vector<std::string> MakeDeterministicNamesList(int length) {
  std::vector<std::string> ret;
  for (int i = 0; i < length; ++i) {
    ret.emplace_back(fxl::StringPrintf("%07d", i));
  }

  std::shuffle(ret.begin(), ret.end(), std::default_random_engine(0x2128847));

  return ret;
}

// Benchmark baseline creation time.
bool PseudoDirCreateTest(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    perftest::DoNotOptimize(fs::PseudoDir());
  }
  return true;
}

// Benchmark the time to remove an existing entry and add it back.
// Parameterized by the number of files.
bool PseudoDirRemoveAddTest(perftest::RepeatState* state, int file_count) {
  auto file_names = MakeDeterministicNamesList(file_count);

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  int i = 0;

  while (state->KeepRunning()) {
    dir->RemoveEntry(file_names[i]);
    dir->AddEntry(file_names[i], file);
    i = (i + 1) % file_names.size();
  }
  return true;
}

// Benchmark the time to lookup an existing entry.
// Parameterized by the number of files.
bool PseudoDirLookupTest(perftest::RepeatState* state, int file_count) {
  auto file_names = MakeDeterministicNamesList(file_count);

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  int i = 0;
  fbl::RefPtr<fs::Vnode> out;

  while (state->KeepRunning()) {
    dir->Lookup(&out, file_names[i]);
    i = (i + 1) % file_names.size();
  }
  return true;
}

// Benchmark the time to read out a directory.
// Parameterized by the number of files and the size of the output buffer.
bool PseudoDirReaddirTest(perftest::RepeatState* state, int file_count,
                          int buffer_size) {
  auto file_names = MakeDeterministicNamesList(file_count);
  std::vector<char> buffer(buffer_size);

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  while (state->KeepRunning()) {
    fs::vdircookie_t cookie;
    size_t real_len;
    while (dir->Readdir(&cookie, buffer.data(), buffer.size(), &real_len) !=
           ZX_OK) {
    }
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("PseudoDir/Create", PseudoDirCreateTest);

  for (int file_count = 1; file_count <= 1 << 14; file_count *= 8) {
    auto name = fbl::StringPrintf("PseudoDir/RemoveAdd/%d", file_count);
    perftest::RegisterTest(name.c_str(), PseudoDirRemoveAddTest, file_count);
  }

  for (int file_count = 1; file_count <= 1 << 14; file_count *= 8) {
    auto name = fbl::StringPrintf("PseudoDir/Lookup/%d", file_count);
    perftest::RegisterTest(name.c_str(), PseudoDirLookupTest, file_count);
  }

  for (int buffer_size = 1; buffer_size <= 64; buffer_size *= 8) {
    for (int file_count = 1; file_count <= 1 << 14; file_count *= 8) {
      auto name = fbl::StringPrintf("PseudoDir/Readdir/%d/%dk", file_count,
                                    buffer_size);
      perftest::RegisterTest(name.c_str(), PseudoDirReaddirTest, file_count,
                             buffer_size * 1024);
    }
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
