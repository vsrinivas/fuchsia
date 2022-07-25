// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod c {
    #[link(name = "buildid", kind = "static")]
    extern "C" {
        pub fn get_build_id(out: *mut std::os::raw::c_uchar) -> std::os::raw::c_int;
    }
}

use std::ffi::CStr;
use thiserror::Error;

#[derive(Debug, Error)]
#[error("Failed to get build-id from current process: {result:?}")]
pub struct Error {
    result: std::os::raw::c_int,
}

pub fn get_build_id() -> Result<String, Error> {
    let mut out = [0u8; 33];

    let res = unsafe { c::get_build_id(out.as_mut_ptr()) };
    if res <= 0 {
        return Err(Error { result: res });
    }

    let str =
        CStr::from_bytes_with_nul(&out[..res as usize + 1]).map_err(|_| Error { result: res })?;
    Ok(str.to_string_lossy().to_string())
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn error_string() {
        assert_eq!(
            format!("{}", Error { result: 5 }),
            "Failed to get build-id from current process: 5"
        );
    }

    #[test]
    fn can_get_build_id() {
        let str = get_build_id().unwrap();

        #[cfg(target_os = "macos")]
        assert_eq!(str.len(), 32);

        #[cfg(target_os = "linux")]
        assert!(str.len() >= 16);

        for char in str.chars() {
            assert!(('0'..='9').contains(&char) || ('a'..='f').contains(&char))
        }
    }
}
