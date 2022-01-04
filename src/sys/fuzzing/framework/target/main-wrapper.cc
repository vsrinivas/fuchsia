// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is used to work around Rust linker and convert Rust binaries back into staticlibs that
// can be linked against the compiler runtimes provided by the clang toolchain suite.
//
// To make use of it, conditionally define the `fuzz_main` symbol using FFI and use it to call the
// actual main function, i.e. for Rust, add something like:
//
//   #[cfg(fuzz)]
//   #[no_mangle]
//   pub extern "C" fn fuzz_main() {
//     main().unwrap();
//   }

extern "C" void fuzz_main();

int main(int argc, char const *argv[]) {
  fuzz_main();
  return 0;
}
