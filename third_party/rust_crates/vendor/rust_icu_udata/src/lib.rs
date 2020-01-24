// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use {
    rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::versioned_function,
    rust_icu_sys::*, std::convert::TryFrom, std::os::raw,
};

/// Implements `UDataMemory`.
///
/// Represents data memory backed by a borrowed memory buffer used for loading ICU data.
/// UDataMemory is very much not thread safe, as it affects the global state of the ICU library.
/// This suggests that the best way to use this data is to load it up in a main thread, or access
/// it through a synchronized wrapper.
#[derive(Debug)]
pub struct UDataMemory {
    // The buffer backing this data memory.  We're holding it here though unused
    // so that we're sure the underlying buffer won't go away while the data is
    // in use.
    #[allow(dead_code)]
    buf: Vec<u8>,
}

impl Drop for UDataMemory {
    // Implements `u_cleanup`.
    fn drop(&mut self) {
        unsafe { versioned_function!(u_cleanup)() };
    }
}

impl TryFrom<Vec<u8>> for crate::UDataMemory {
    type Error = common::Error;
    /// Makes a UDataMemory out of a buffer.
    ///
    /// Implements `udata_setCommonData`.
    fn try_from(buf: Vec<u8>) -> Result<Self, Self::Error> {
        let mut status = sys::UErrorCode::U_ZERO_ERROR;
        // Expects that buf is a valid pointer and that it contains valid
        // ICU data.  If data is invalid, an error status will be set.
        // No guarantees for invalid pointers.
        unsafe {
            versioned_function!(udata_setCommonData)(
                buf.as_ptr() as *const raw::c_void,
                &mut status,
            );
        };
        common::Error::ok_or_warning(status)?;
        Ok(UDataMemory { buf })
    }
}
