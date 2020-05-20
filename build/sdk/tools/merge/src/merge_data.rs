// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::Data;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::immutable::merge_immutable_element;
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for Data {
    fn get_common_files(&self) -> Vec<String> {
        self.data.clone()
    }
}

pub fn merge_data<F: TarballContent>(
    meta_path: &str,
    base: &impl InputTarball<F>,
    complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    merge_immutable_element::<Data, _, _, _, _>(meta_path, base, complement, output)
}

#[cfg(test)]
mod tests {
    use super::*;

    test_merge_immutable_success! {
        name = test_merge,
        merge = merge_data,
        meta = "data/config/foobar/meta.json",
        data = r#"
        {
            "name": "foobar",
            "type": "config",
            "data": [
                "data/config/foobar/config.json"
            ]
        }
        "#,
        files = [
            "data/config/foobar/config.json"
        ],
    }

    test_merge_immutable_failure! {
        name = test_merge_failed,
        merge = merge_data,
        meta = "data/config/foobar/meta.json",
        base_data = r#"
        {
            "name": "foobar",
            "type": "config",
            "docs": [
                "data/config/foobar/config.json"
            ]
        }
        "#,
        base_files = [
            "data/foobar/config.json",
        ],
        // This metadata has one extra config.
        complement_data = r#"
        {
            "name": "foobar",
            "type": "config",
            "docs": [
                "data/config/foobar/config.json"
                "data/config/foobar/config.too.json"
            ]
        }
        "#,
        complement_files = [
            "data/config/foobar/config.json",
            "data/config/foobar/config.too.json",
        ],
    }
}
