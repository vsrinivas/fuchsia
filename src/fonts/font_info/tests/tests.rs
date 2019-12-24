// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{format_err, Error},
    char_set::CharSet,
    font_info::{FontAssetSource, FontInfo, FontInfoLoaderImpl},
    std::path::Path,
};

/// Returns `"{output_dir}/test_data/font_info/sample_font.ttf"`.
#[cfg(not(target_os = "fuchsia"))]
fn font_path() -> Result<String, Error> {
    use std::env;

    assert!(!Path::new("/pkg/data").exists(), "/pkg/data should not exist on host");

    let relative_font_path = "test_data/font_info/sample_font.ttf";
    let mut path = env::current_exe().unwrap();

    // We don't know exactly where the binary is in the out directory (varies by target platform and
    // architecture), so search up the file tree for the sample font.
    loop {
        if path.join(relative_font_path).exists() {
            path.push(relative_font_path);
            break Ok(path.to_str().unwrap().to_string());
        }
        if !path.pop() {
            // Reached the root of the file system
            break Err(format_err!(
                "Couldn't find {:?} near {:?}",
                relative_font_path,
                env::current_exe().unwrap()
            ));
        }
    }
}

#[cfg(target_os = "fuchsia")]
fn font_path() -> Result<String, Error> {
    let font_path = "/pkg/data/sample_font.ttf";
    if Path::new(font_path).exists() {
        Ok(font_path.to_string())
    } else {
        Err(format_err!("{} missing on Fuchsia", font_path))
    }
}

#[test]
fn test_load_font_info_from_file() -> Result<(), Error> {
    let loader = FontInfoLoaderImpl::new()?;

    let source = FontAssetSource::FilePath(font_path()?);
    println!("Loading font from {:?}", &source);

    let actual = loader.load_font_info(source, 0)?;
    let expected = FontInfo {
        char_set: CharSet::new(vec![
            0x0, 0x0d, 0x20, 0x28, 0x29, 0x2c, 0x30, 0x31, 0x32, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
        ]),
    };

    assert_eq!(actual, expected);

    Ok(())
}
