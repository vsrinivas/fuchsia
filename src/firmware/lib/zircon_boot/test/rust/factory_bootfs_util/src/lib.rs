// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use fuchsia_bootfs::BootfsParser;
use fuchsia_zbi::{ZbiParser, ZbiType::StorageBootfsFactory};
use fuchsia_zircon as zx;
use std::{convert::TryInto, slice};

pub type size_t = ::std::os::raw::c_ulong;

/// Get a factory bootfs file from a zbi image.
///
/// # Safety
///
/// Caller should guarantee all pointers passed are valid.
#[no_mangle]
pub unsafe extern "C" fn get_bootfs_file_payload(
    zbi: *const ::std::os::raw::c_void,
    size: size_t,
    file_name: *const ::std::os::raw::c_char,
    payload: *mut ::std::os::raw::c_void,
    out_size: *mut size_t,
) -> ::std::os::raw::c_int {
    // Write the buffer into a vmo
    let slice = slice::from_raw_parts(zbi as *const u8, size.try_into().unwrap());
    let vmo = zx::Vmo::create(size).unwrap();
    vmo.write(slice, 0).unwrap();

    // Create a zbi parser from the vmo
    let zbi_parser = ZbiParser::new(vmo).parse().unwrap();

    // Try to find the StorageBootfsFactory item
    match zbi_parser.try_get_item(StorageBootfsFactory) {
        Ok(result) => {
            // Create a rust string from c char pointer
            let file_name_c_str: &std::ffi::CStr = std::ffi::CStr::from_ptr(file_name);
            let file_name_str = file_name_c_str.to_str().unwrap();

            // Loop through all matching StorageBootfsFactory items
            for item in result {
                // Write the StorageBootfsFactory item bytes into a vmo
                let bootfs_vmo = zx::Vmo::create(item.bytes.len().try_into().unwrap()).unwrap();
                bootfs_vmo.write(&item.bytes, 0).unwrap();

                // Create a bootfs parser from the vmo
                let bootfs_parser = BootfsParser::create_from_vmo(bootfs_vmo).unwrap();

                // Find the file
                match bootfs_parser.iter().find(|bootfs_item_result| {
                    bootfs_item_result.as_ref().unwrap().name == file_name_str
                }) {
                    Some(bootfs_file) => {
                        // Retrieve the payload and see if output buffer size can fit.
                        let file_payload = bootfs_file.unwrap().payload.unwrap();
                        let read_len: &mut size_t = &mut *out_size;
                        if *read_len < file_payload.len().try_into().unwrap() {
                            return 1;
                        }
                        *read_len = file_payload.len().try_into().unwrap();

                        // Cast the output buffer pointer into a rust slice
                        let out_slice = slice::from_raw_parts_mut(
                            payload as *mut u8,
                            (*read_len).try_into().unwrap(),
                        );

                        // Copy the file content to the output buffer.
                        (*out_slice).copy_from_slice(&file_payload);
                        return 0;
                    }
                    _ => {}
                };
            }
            return 1;
        }
        _ => {
            return 1;
        }
    };
}
