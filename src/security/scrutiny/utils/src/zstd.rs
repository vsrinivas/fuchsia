// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    libc::{c_char, c_int, c_void, size_t},
    std::ffi::CStr,
};

/// "C" externs provided from the zstd third_party library.
#[link(name = "zstd")]
extern "C" {
    fn ZSTD_compress(
        dst: *mut c_void,
        dst_capacity: size_t,
        src: *const c_void,
        src_size: size_t,
        compression_level: c_int,
    ) -> size_t;
    fn ZSTD_decompress(
        dst: *mut c_void,
        dst_capacity: size_t,
        src: *const c_void,
        compressed_size: size_t,
    ) -> size_t;
    fn ZSTD_isError(code: size_t) -> u32;
    fn ZSTD_getErrorName(code: size_t) -> *const c_char;
}

/// Attempts to compress the `src` buffer returning a Vec<u8> with a capacity which
/// is at most `dst_capacity`.
pub fn compress(src: &[u8], dst_capacity: u32, level: i32) -> Result<Vec<u8>> {
    unsafe {
        let src_len = src.len() as size_t;
        let src_ptr = src.as_ptr() as *const c_void;
        let dst_len = dst_capacity as size_t;
        let mut dst = Vec::with_capacity(dst_capacity as usize);
        let dst_ptr = dst.as_mut_ptr() as *mut c_void;
        let compression_level = level as c_int;

        let code = ZSTD_compress(dst_ptr, dst_len, src_ptr, src_len, compression_level);

        if ZSTD_isError(code) != 0 {
            let error: *const c_char = ZSTD_getErrorName(code);
            let error_str = CStr::from_ptr(error).to_str().unwrap().to_owned();
            Err(anyhow!(error_str))
        } else {
            dst.set_len(code as usize);
            Ok(dst)
        }
    }
}

/// Attempts to decompress the `src` buffer returning a Vec<u8> with a capacity which
/// is at most `dst_capacity`. This is intended to be used when the dst_capacity
/// can be predicted (such as through the zbi.extra flag).
pub fn decompress(src: &[u8], dst_capacity: u32) -> Result<Vec<u8>> {
    unsafe {
        let src_len = src.len() as size_t;
        let src_ptr = src.as_ptr() as *const c_void;
        let dst_len = dst_capacity as size_t;
        let mut dst = Vec::with_capacity(dst_capacity as usize);
        let dst_ptr = dst.as_mut_ptr() as *mut c_void;

        let code = ZSTD_decompress(dst_ptr, dst_len, src_ptr, src_len);

        if ZSTD_isError(code) != 0 {
            let error: *const c_char = ZSTD_getErrorName(code);
            let error_str = CStr::from_ptr(error).to_str().unwrap().to_owned();
            Err(anyhow!(error_str))
        } else {
            dst.set_len(code as usize);
            Ok(dst)
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, hex};

    #[test]
    fn test_compress_decompress() {
        let dst_capacity = 100;
        let to_compress = hex::decode("aaaabbbbcccc").unwrap();
        let compressed = compress(&to_compress, dst_capacity, 3).unwrap();
        let decompressed = decompress(&compressed, dst_capacity).unwrap();
        assert_eq!(decompressed, to_compress);
    }
}
