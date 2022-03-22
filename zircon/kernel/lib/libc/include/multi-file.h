// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_MULTI_FILE_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_MULTI_FILE_H_

#include <stdio.h>

#include <ktl/array.h>
#include <ktl/forward.h>
#include <ktl/string_view.h>
#include <ktl/type_traits.h>

// MultiFile broadcasts Write calls across multiple FILE* pointers.  Each
// pointer is ignored if it's null.  MultiFile::Write always returns success,
// ignoring any failures or short writes from the underlying FILE objects.
template <size_t N>
class MultiFile : public FILE {
 public:
  using FileArray = ktl::array<FILE*, N>;

  constexpr MultiFile() : FILE{this} {}

  constexpr explicit MultiFile(const FileArray& files) : FILE{this}, files_(files) {}

  constexpr MultiFile(const MultiFile& other) : MultiFile(other.files_) {}

  FileArray& files() { return files_; }
  const FileArray& files() const { return files_; }

  int Write(ktl::string_view str) {
    for (FILE* file : files_) {
      if (file) {
        file->Write(str);
      }
    }
    return static_cast<int>(str.size());
  }

 private:
  FileArray files_ = {};
};

template <size_t N>
MultiFile(const ktl::array<FILE*, N>&) -> MultiFile<N>;

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_MULTI_FILE_H_
