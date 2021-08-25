// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

#[cxx::bridge(namespace = rust::zx::ffi)]
pub mod ffi {
    /// A valueless result type, similar to C++ zx::status<> but with an added string message to
    /// hold the string-ified Rust error. On the C++ side this can be converted to a
    /// rust::zx::Result which has a similar API to zx::status (see //src/lib/fuchsia-cxx/result.h).
    ///
    /// This type can be reused in your own bridges directly or used to create result types with a
    /// success value. For example:
    ///
    ///   #[cxx::bridge(namespace = foo::ffi)]
    ///   mod ffi {
    ///       #[namespace = "rust::zx::ffi"]
    ///       extern "C++" {
    ///           include!("src/lib/fuchsia-cxx/result.h");
    ///           type Result = fuchsia_cxx::Result;
    ///       }
    ///
    ///       struct FooResult {
    ///           result: Result,
    ///           foo: Box<Foo>,
    ///       }
    ///
    ///       extern "Rust" {
    ///           type Foo;
    ///           fn new_foo() -> FooResult;
    ///           fn use_foo(self: &Foo) -> Result;
    ///       }
    ///   }
    ///
    /// TODO(fxbug.dev/71142): Once CXX has support for Results with a generic error type and
    /// non-throw/panic behavior, this type can become just the error part of the result.
    pub struct Result {
        // On success, equal to ZX_OK (0). On error, a non-zero ZX_ERR_* Zircon status value.
        status: i32,
        // On success, this is empty. On error, the stringified (with Display) Rust error.
        message: String,
    }
}

impl ffi::Result {
    pub fn ok() -> ffi::Result {
        ffi::Result { status: zx::Status::OK.into_raw(), message: String::new() }
    }

    // This can't be a From impl because then it conflicts with the std::result::Result impl below,
    // even though std::result::Result could never actually impl Into<zx::Status>.
    pub fn from_error<T>(error: T) -> ffi::Result
    where
        T: Into<zx::Status> + std::fmt::Display,
    {
        let message = error.to_string();
        ffi::Result { status: error.into().into_raw(), message }
    }
}

// This allows for easy conversion from the std Result type to ffi::Result as long as your error
// type implements Into<zx::Status>.
impl<T> From<std::result::Result<(), T>> for ffi::Result
where
    T: Into<zx::Status> + std::fmt::Display,
{
    fn from(result: std::result::Result<(), T>) -> Self {
        match result {
            Ok(()) => ffi::Result::ok(),
            Err(err) => ffi::Result::from_error(err),
        }
    }
}
