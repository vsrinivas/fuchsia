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
    rust_icu_sys::*, std::cmp::Eq, std::convert::TryFrom, std::os::raw,
};

#[derive(Debug)]
pub struct Text {
    // rep is allocated by the underlying library and has to be dropped using `utext_close`.
    rep: *mut UText,
}

impl PartialEq for Text {
    fn eq(&self, other: &Self) -> bool {
        let ret = unsafe { versioned_function!(utext_equals)(self.rep, other.rep) };
        match ret {
            0 => false,
            1 => true,
            // Could be a bug in the underlying library.
            _ => panic!("value is not convertible to bool in Text::eq: {}", ret),
        }
    }
}
impl Eq for Text {}

impl TryFrom<String> for Text {
    type Error = common::Error;

    /// Produces a unicode Text from a rust String.
    ///
    /// The conversion may fail if the string is not well formed, and may result in an error.
    ///
    /// Implements `utext_open` from ICU4C.
    fn try_from(s: String) -> Result<Self, Self::Error> {
        let len: i64 = s.len() as i64;
        let bytes = s.as_ptr() as *const raw::c_char;
        // bytes and len must be compatible.  Ensured by the two lines just above.
        unsafe { Self::from_raw_bytes(bytes, len) }
    }
}

impl TryFrom<&str> for Text {
    type Error = common::Error;
    /// Implements `utext_open`
    fn try_from(s: &str) -> Result<Self, Self::Error> {
        let len = s.len() as i64;
        let bytes = s.as_ptr() as *const raw::c_char;
        // bytes and len must be compatible.  Ensured by the two lines just above.
        unsafe { Self::from_raw_bytes(bytes, len) }
    }
}

impl Text {
    /// Constructs the Text from raw byte contents.
    ///
    /// The expectation is that the buffer and length are valid and compatible.  That is,
    /// that buffer is a valid pointer, that it points to an allocated buffer and that the length
    /// of the allocated buffer is exactly `len`.
    unsafe fn from_raw_bytes(buffer: *const raw::c_char, len: i64) -> Result<Self, common::Error> {
        let mut status = common::Error::OK_CODE;
        // Requires that 'bytes' is a valid pointer and len is the correct length of 'bytes'.
        let rep = versioned_function!(utext_openUTF8)(0 as *mut UText, buffer, len, &mut status);
        common::Error::ok_or_warning(status)?;
        Ok(Text { rep })
    }

    /// Tries to produce a clone of this Text.
    ///
    /// If `deep` is set, a deep clone is made.   This is not a Clone trait since
    /// this clone is parameterized, and may fail.
    ///
    /// Implements `utext_clone` from ICU4C.
    pub fn try_clone(&self, deep: bool, readonly: bool) -> Result<Self, common::Error> {
        let mut status = common::Error::OK_CODE;
        // Requires that 'src' be a valid pointer.
        let rep = unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(utext_clone)(
                0 as *mut UText,
                self.rep,
                deep as sys::UBool,
                readonly as sys::UBool,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        Ok(Text { rep })
    }
}

impl Drop for Text {
    /// Implements `utext_close` from ICU4C.
    fn drop(&mut self) {
        // Requires that self.rep is a valid pointer.
        unsafe {
            versioned_function!(utext_close)(self.rep);
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn partial_eq() {
        let foo = Text::try_from("foo".to_string()).expect("conversion from string succeeds.");
        let bar = Text::try_from("foo").expect("conversion from literal succeeds");

        let baz = Text::try_from("baz").expect("conversion from literal succeeds");

        // Should all be equal to themselves.
        assert_eq!(1i8, unsafe {
            versioned_function!(utext_equals)(foo.rep, foo.rep)
        });
        assert_eq!(1i8, unsafe {
            versioned_function!(utext_equals)(bar.rep, bar.rep)
        });

        // Should not be equal since not the same text and position.
        assert_ne!(1i8, unsafe {
            versioned_function!(utext_equals)(foo.rep, bar.rep)
        });

        assert_ne!(foo, bar);
        assert_ne!(foo, baz);
        assert_ne!(bar, baz);

        assert_eq!(
            foo,
            foo.try_clone(true, true).expect("clone is a success"),
            "a clone should be the same as its source"
        );
    }
}
