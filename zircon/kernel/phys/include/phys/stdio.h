// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_STDIO_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_STDIO_H_

#include <multi-file.h>
#include <stddef.h>
#include <stdio.h>
#include <zircon/compiler.h>

class PhysConsole {
 public:
  static PhysConsole& Get();

  FILE* null() { return &null_; }
  FILE* graphics() { return &mux_files_[kGraphics]; }
  FILE* serial() { return &mux_files_[kSerial]; }

  void set_graphics(const FILE& f) { SetMux(kGraphics, f); }
  void set_serial(const FILE& f) { SetMux(kSerial, f); }

 private:
  enum MuxType : size_t { kGraphics, kSerial, kMuxTypes };

  friend void InitStdout();

  PhysConsole();

  void SetMux(size_t idx, const FILE& f);

  MultiFile<kMuxTypes> mux_;
  FILE null_, mux_files_[kMuxTypes];
};

void InitStdout();

// A printf that respects the `kernel.phys.verbose` boot option: if the option
// is false, nothing will be printed.
void debugf(const char* fmt, ...) __PRINTFLIKE(1, 2);

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_STDIO_H_
