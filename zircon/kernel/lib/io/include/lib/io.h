// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_
#define ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_

#include <sys/types.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>

/* LK specific calls to register to get input/output of the main console */

class PrintCallback : public fbl::DoublyLinkedListable<PrintCallback*> {
 public:
  PrintCallback(const PrintCallback&) = delete;
  PrintCallback(PrintCallback&&) = delete;
  PrintCallback& operator=(const PrintCallback&) = delete;
  PrintCallback& operator=(PrintCallback&&) = delete;

  using Callback = void(PrintCallback* cb, const char* str, size_t len);

  constexpr explicit PrintCallback(Callback* callback) : callback_(callback) {}

  void Print(const char* str, size_t len) {
    if (callback_)
      callback_(this, str, len);
  }

 private:
  Callback* callback_;
};

/* register callback to receive debug prints */
void register_print_callback(PrintCallback* cb);
void unregister_print_callback(PrintCallback* cb);

/* back doors to directly write to the kernel serial and console */
void __kernel_serial_write(const char* str, size_t len);
void __kernel_console_write(const char* str, size_t len);

#endif  // ZIRCON_KERNEL_LIB_IO_INCLUDE_LIB_IO_H_
