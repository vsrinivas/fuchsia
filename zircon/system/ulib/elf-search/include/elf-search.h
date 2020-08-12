// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELF_SEARCH_H_
#define ELF_SEARCH_H_

#include <elf.h>
#include <lib/zx/process.h>
#include <stdint.h>

#include <fbl/array.h>
#include <fbl/function.h>
#include <fbl/string_piece.h>

namespace elf_search {

#if defined(__aarch64__)
constexpr Elf64_Half kNativeElfMachine = EM_AARCH64;
#elif defined(__x86_64__)
constexpr Elf64_Half kNativeElfMachine = EM_X86_64;
#endif

// TODO(jakehehrlich): Replace ArrayRef here with something more like std::span
// in fbl.
template <class T>
class ArrayRef {
 public:
  ArrayRef() = default;
  template <size_t N>
  ArrayRef(const T (&arr)[N]) : ptr_(arr), sz_(N) {}
  ArrayRef(const fbl::Array<T>& arr) : ptr_(arr.data()), sz_(arr.size()) {}
  ArrayRef(const T* ptr, size_t sz) : ptr_(ptr), sz_(sz) {}

  bool empty() const { return sz_ == 0; }

  const T* get() const { return ptr_; }

  const T* begin() const { return ptr_; }

  const T* end() const { return ptr_ + sz_; }

  size_t size() const { return sz_; }

  const T& operator[](size_t idx) const { return ptr_[idx]; }

 private:
  const T* ptr_ = nullptr;
  size_t sz_ = 0;
};

template <class T>
bool operator==(ArrayRef<T> a, ArrayRef<T> b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

template <typename T>
ArrayRef<T> MakeArrayRef(const T* p, size_t sz) {
  return {p, sz};
}

template <typename T, size_t N>
ArrayRef<T> MakeArrayRef(const T (&arr)[N]) {
  return {arr};
}

struct ModuleInfo {
  fbl::StringPiece name;
  uintptr_t vaddr;
  ArrayRef<uint8_t> build_id;
  const Elf64_Ehdr& ehdr;
  ArrayRef<Elf64_Phdr> phdrs;
};

using ModuleAction = fbl::Function<void(const ModuleInfo&)>;
extern zx_status_t ForEachModule(const zx::process&, ModuleAction);

}  // namespace elf_search

#endif  // ELF_SEARCH_H_
