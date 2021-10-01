// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Error},
    fuchsia_zircon as zx,
    matches::assert_matches,
    rust_icu_ucal as ucal,
};
// [START imports]
use icu_data::Loader;
// [END imports]

#[fuchsia::test]
async fn no_tzdata_res_files() -> Result<(), Error> {
    // [START loader_example]
    // Load the ICU data
    let _loader = Loader::new().expect("loader constructed successfully");
    // [END loader_example]

    // Get TimeZone data version
    let actual_revision_id = ucal::get_tz_data_version()?;
    println!("Squawk! TZ version is: {}", actual_revision_id);

    Ok(())
}

#[fuchsia::test]
async fn with_tzdata_res_files() -> Result<(), Error> {
    // [START loader_config_example]
    const DEFAULT_TZDATA_DIR: &str = "/config/data/tzdata/icu/44/le";
    const DEFAULT_TZREVISION_FILE_PATH: &str = "/config/data/tzdata/revision.txt";

    // Load the ICU data
    let _loader = Loader::new_with_tz_resources_and_validation(
        DEFAULT_TZDATA_DIR,
        DEFAULT_TZREVISION_FILE_PATH,
    )
    .expect("loader constructed successfully");
    // [END loader_config_example]

    // Get TimeZone data version
    let actual_revision_id = ucal::get_tz_data_version()?;
    println!("Squawk! TZ version is: {}", actual_revision_id);

    Ok(())
}

#[fuchsia::test]
async fn with_tzdata_res_files_wrong_revision() -> Result<(), Error> {
    const DEFAULT_TZDATA_DIR: &str = "/config/data/tzdata/icu/44/le";
    const LOCAL_TZREVISION_FILE_PATH: &str = "/pkg/data/newer_revision.txt";

    // Load the ICU data
    let _loader = Loader::new_with_tz_resources_and_validation(
        DEFAULT_TZDATA_DIR,
        LOCAL_TZREVISION_FILE_PATH,
    );

    let err = _loader.unwrap_err();
    assert_matches!(err, icu_data::Error::Status(zx::Status::IO_DATA_INTEGRITY, Some(_)));

    // Get TimeZone data version
    let actual_revision_id = ucal::get_tz_data_version()?;
    println!("Squawk! TZ version is: {}", actual_revision_id);

    Ok(())
}
