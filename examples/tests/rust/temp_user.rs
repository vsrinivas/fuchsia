// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    std::fs,
    std::io::{Read, Write},
};

const TEMP_FILE_NAME: &'static str = "/tmp/example_file";
const TEMP_FILE_CONTENTS: &'static str = "Hello, world!";

#[test]
fn use_temp() -> Result<(), Error> {
    write_file()?;
    let read_contents = read_file()?;
    assert_eq!(read_contents, TEMP_FILE_CONTENTS);
    Ok(())
}

fn write_file() -> Result<(), Error> {
    let mut file = fs::File::create(TEMP_FILE_NAME)?;
    file.write_all(TEMP_FILE_CONTENTS.as_bytes())?;
    file.sync_all()?;
    Ok(())
}

fn read_file() -> Result<String, Error> {
    let mut file = fs::File::open(TEMP_FILE_NAME)?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)?;
    Ok(contents)
}
