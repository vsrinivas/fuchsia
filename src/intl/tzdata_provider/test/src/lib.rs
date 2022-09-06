// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {icu_data::Loader, rust_icu_ucal as ucal};

#[fuchsia::test]
async fn test_tzdata_icu_44_le() {
    const TZDATA_DIR: &str = "/tzdata-icu-44-le";
    const REVISION_FILE_PATH: &str = "/tzdata-icu-44-le/revision.txt";

    let _loader = Loader::new_with_tz_resources_and_validation(TZDATA_DIR, REVISION_FILE_PATH)
        .expect("Failed to create Loader");

    let _version = ucal::get_tz_data_version().expect("Failed to get tzdata version");
}
