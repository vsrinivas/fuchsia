// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <fs/lazy-dir.h>
#include <lib/fxl/strings/string_printf.h>
#include <perftest/perftest.h>
#include <zircon/syscalls.h>

#include "util.h"

namespace {

class TestLazyDir : public fs::LazyDir {
 public:
  void AddEntry(LazyEntry entry) { entries_.push_back(std::move(entry)); }

 protected:
  void GetContents(LazyEntryVector* out_vector) override {
    out_vector->reserve(entries_.size());
    for (const auto& entry : entries_) {
      out_vector->push_back(entry);
    }
  }

  zx_status_t GetFile(fbl::RefPtr<Vnode>* out_vnode, uint64_t id,
                      fbl::String name) override {
    // Do nothing to benchmark obtaining id and name.
    return ZX_OK;
  }

  fbl::Vector<LazyEntry> entries_;
};

// Measure the time taken to create an empty LazyDir.
bool TestCreate(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    ZX_ASSERT(fbl::AdoptRef<TestLazyDir>(new TestLazyDir()) != nullptr);
  }
  return true;
}

// Measure the time taken to look up an entry in a LazyDir of a given size.
bool TestLookup(perftest::RepeatState* state, size_t file_count) {
  auto file_names = util::MakeDeterministicNamesList(file_count);

  auto dir = fbl::AdoptRef(new TestLazyDir());

  uint64_t id = 1;
  for (const auto& name : file_names) {
    dir->AddEntry({id++, name, V_TYPE_FILE});
  }

  int i = 0;
  while (state->KeepRunning()) {
    fbl::RefPtr<fs::Vnode> out;
    ZX_ASSERT(dir->Lookup(&out, file_names[i]) == ZX_OK);
    i = (i + 1) % file_names.size();
  }
  return true;
}

// Measure the time taken to read directory entries from a LazyDir of
// a given size using a given sized buffer.
bool TestReaddir(perftest::RepeatState* state, size_t file_count,
                 size_t buffer_size) {
  auto file_names = util::MakeDeterministicNamesList(file_count);
  std::vector<char> buffer(buffer_size);

  auto dir = fbl::AdoptRef(new TestLazyDir());

  uint64_t id = 1;
  for (const auto& name : file_names) {
    dir->AddEntry({id++, name, V_TYPE_FILE});
  }

  while (state->KeepRunning()) {
    fs::vdircookie_t cookie;
    size_t real_len = 0;
    while (dir->Readdir(&cookie, buffer.data(), buffer_size, &real_len) !=
           ZX_OK) {
      ZX_ASSERT(real_len != 0);
    }
  }
  return true;
}

void RegisterTests() {
  static const size_t kSizes[] = {1, 8, 64, 512, 4 * 1024, 16 * 1024};
  static const size_t kBuffers[] = {1024, 4 * 1024, 16 * 1024, 64 * 1024};
  perftest::RegisterTest("LazyDir/Create", TestCreate);
  for (auto size : kSizes) {
    auto name = fbl::StringPrintf("LazyDir/Lookup/size:%zd", size);
    perftest::RegisterTest(name.c_str(), TestLookup, size);
  }

  for (auto size : kSizes) {
    for (auto buffer : kBuffers) {
      auto name =
          fbl::StringPrintf("LazyDir/Readdir/size:%zd/buf:%zd", size, buffer);
      perftest::RegisterTest(name.c_str(), TestReaddir, size, buffer);
    }
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
