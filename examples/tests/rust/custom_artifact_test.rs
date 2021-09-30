// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, std::fs, std::io::Write};

const FILE_NAME: &'static str = "/custom_artifacts/artifact.txt";
const FILE_CONTENTS: &'static str = "Hello, world!";

#[test]
fn use_artifact() -> Result<(), Error> {
    write_file()
}

fn write_file() -> Result<(), Error> {
    let mut file = fs::File::create(FILE_NAME)?;
    file.write_all(FILE_CONTENTS.as_bytes())?;
    file.sync_all()?;
    Ok(())
}
