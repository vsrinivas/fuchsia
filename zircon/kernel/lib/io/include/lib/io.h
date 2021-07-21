// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_
#define ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_

#include <stdio.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>
#include <ktl/array.h>
#include <ktl/string_view.h>

enum class SkipPersistedDebuglog { No = 0, Yes };

class Linebuffer {
 public:
  // Buffer the contents of |str|, sending lines at a time to be
  // output via kStdout.
  //
  // Lines break either at '\n' characters in |str|, or when the
  // internal buffer gets full.
  void Write(ktl::string_view str, SkipPersistedDebuglog skip_pdlog);

 private:
  size_t pos_ = 0;
  ktl::array<char, 128> buffer_;
};

class PrintCallback : public fbl::DoublyLinkedListable<PrintCallback*> {
 public:
  PrintCallback(const PrintCallback&) = delete;
  PrintCallback(PrintCallback&&) = delete;
  PrintCallback& operator=(const PrintCallback&) = delete;
  PrintCallback& operator=(PrintCallback&&) = delete;

  using Callback = void(PrintCallback* cb, ktl::string_view str);

  constexpr explicit PrintCallback(Callback* callback) : callback_(callback) {}

  void Print(ktl::string_view str) {
    if (callback_)
      callback_(this, str);
  }

 private:
  Callback* callback_;
};

// Register a callback to receive debug prints.
void register_print_callback(PrintCallback* cb);
void unregister_print_callback(PrintCallback* cb);

// Back doors to directly write to the kernel serial and console, respectively.
void serial_write(ktl::string_view str);
void console_write(ktl::string_view str);

extern FILE gSerialFile;
extern FILE gConsoleFile;
extern FILE gStdoutNoPersist;

#endif  // ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_
