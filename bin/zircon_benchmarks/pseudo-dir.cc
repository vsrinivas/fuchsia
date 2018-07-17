// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>

#include <benchmark/benchmark.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <fs/pseudo-dir.h>
#include <fs/pseudo-file.h>
#include <lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>

std::vector<std::string> MakeDeterministicNamesList(int length) {
  std::vector<std::string> ret;
  for (int i = 0; i < length; ++i) {
    ret.emplace_back(fxl::StringPrintf("%07d", i));
  }

  std::shuffle(ret.begin(), ret.end(), std::default_random_engine(0x2128847));

  return ret;
}

namespace {

class PseudoDir : public benchmark::Fixture {};

}  // namespace

// Benchmark baseline creation time.
BENCHMARK_DEFINE_F(PseudoDir, Create)(benchmark::State& state) {
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(fs::PseudoDir());
  }
}

BENCHMARK_REGISTER_F(PseudoDir, Create);

// Benchmark the time to remove an existing entry and add it back.
// Parameterized by the number of files.
BENCHMARK_DEFINE_F(PseudoDir, RemoveAdd)(benchmark::State& state) {
  auto file_names = MakeDeterministicNamesList(state.range(0));

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  int i = 0;

  while (state.KeepRunning()) {
    dir->RemoveEntry(file_names[i]);
    dir->AddEntry(file_names[i], file);
    i = (i + 1) % file_names.size();
  }
}

BENCHMARK_REGISTER_F(PseudoDir, RemoveAdd)->Range(1, 1 << 14);

// Benchmark the time to lookup an existing entry.
// Parameterized by the number of files.
BENCHMARK_DEFINE_F(PseudoDir, Lookup)(benchmark::State& state) {
  auto file_names = MakeDeterministicNamesList(state.range(0));

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  int i = 0;
  fbl::RefPtr<fs::Vnode> out;

  while (state.KeepRunning()) {
    dir->Lookup(&out, file_names[i]);
    i = (i + 1) % file_names.size();
  }
}

// Benchmark the time to read out a directory.
// Parameterized by the number of files and the size of the output buffer.
BENCHMARK_DEFINE_F(PseudoDir, Readdir)(benchmark::State& state) {
  auto file_names = MakeDeterministicNamesList(state.range(0));
  size_t len = state.range(1);
  void* buffer = calloc(0, len);

  auto dir = fbl::AdoptRef(new fs::PseudoDir());
  auto file = fbl::AdoptRef(new fs::UnbufferedPseudoFile());

  for (const auto& name : file_names) {
    dir->AddEntry(name, file);
  }

  while (state.KeepRunning()) {
    fs::vdircookie_t cookie;
    size_t real_len;
    while (dir->Readdir(&cookie, buffer, len, &real_len) != ZX_OK) {
    }
  }
}

BENCHMARK_REGISTER_F(PseudoDir, Readdir)
    ->Ranges({{1, 1 << 14}, {1 << 10, 1 << 16}});
