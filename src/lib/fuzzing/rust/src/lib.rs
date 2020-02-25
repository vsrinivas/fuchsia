// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Rust/LibFuzzer integration for Fuchsia

/// Declare a fuzzer.
///
/// `declare_fuzzers!` takes function expressions, `$fuzz_target`, as arguments. Each
/// `$fuzz_target` must be a function which accepts a `&[u8]` and has no return value -
/// precisely, it must implement `FnOnce(&[u8])`.
///
/// `declare_fuzzers!` defines a well-known symbol which is available for linking
/// by other programs. As such, it must only be called at most once in any given
/// program.
#[macro_export]
macro_rules! declare_fuzzers {
    ( $($fuzz_target:expr),* ) => {
        // This anonymous constant prevents Rust code from calling this symbol natively. It can
        // only be called by linking against the produced object file, e.g. with libFuzzer.
        const _: () = {
            #[no_mangle]
            pub extern "C" fn LLVMFuzzerTestOneInput(data: *const u8, size: usize) -> i32 {
                if size == 0 {
                    return 0;
                }
                // Data must not be modified; make an immutable slice.
                let data = unsafe { std::slice::from_raw_parts(data, size) };
                // Define this function to provide a more helpful error message when the provided
                // function doesn't have the right signature.
                fn call<F: FnOnce(&[u8])>(f: F, data: &[u8]) {
                    f(data);
                }
                // Use the first byte to pick a fuzz target .
                let mut index = data[0];
                $(
                    if index == 0 {
                        call($fuzz_target, &data[1..]);
                        return 0;
                    }
                    index = index.wrapping_sub(1);
                )*
                0 // Always return zero per  https://llvm.org/docs/LibFuzzer.html#fuzz-target.
            }
        };
    };
}
