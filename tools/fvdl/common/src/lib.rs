// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
pub mod portpicker;
pub mod vdl_files;

mod cipd;
mod device;
mod graphic_utils;
mod images;
mod tools;
mod types;
mod vdl_proto_parser;

pub fn test_lib() -> Result<(), anyhow::Error> {
    println!("Hello library world");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_lib_exists() -> Result<(), anyhow::Error> {
        assert!(test_lib().is_ok());
        Ok(())
    }
}
