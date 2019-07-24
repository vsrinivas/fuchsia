// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::DeviceProfile;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for DeviceProfile {}

pub fn merge_device_profile<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<DeviceProfile, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_device_profile,
        meta = "device/foobar.json",
        data = r#"
        {
            "name": "foobar",
            "type": "device_profile",
            "description": "I am Foobar",
            "images_url": "gs://images/foobar",
            "packages_url": "gs://packages/foobar"
        }
        "#,
        files = [],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_device_profile,
        meta = "device/foobar.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "device_profile",
            "description": "I am Foobar",
            "images_url": "gs://images/foobar",
            "packages_url": "gs://packages/foobar"
        }
        "#,
        base_files = [],
        // This metadata has a different description.
        complement_data = r#"
        {
            "name": "foobar",
            "type": "device_profile",
            "description": "I am not Foobar",
            "images_url": "gs://images/foobar",
            "packages_url": "gs://packages/foobar"
        }
        "#,
        complement_files = [],
    }
}
