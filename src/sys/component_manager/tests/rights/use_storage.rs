// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    io_util::{self, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    std::path::PathBuf,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let data_proxy =
        io_util::open_directory_in_namespace("/data", OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)?;
    let file = io_util::open_file(
        &data_proxy,
        &PathBuf::from("test"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
    )?;
    if let Err(_) = io_util::write_file_bytes(&file, b"test_data").await {
        println!("Failed to write to file");
    } else {
        println!("All tests passed");
    }
    Ok(())
}
