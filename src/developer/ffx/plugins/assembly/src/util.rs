// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fuchsia_pkg::PackageManifest;
use std::fs::File;
use std::io::BufReader;
use std::path::Path;

pub fn pkg_manifest_from_path(path: impl AsRef<Path>) -> Result<PackageManifest> {
    let manifest_file = File::open(path)?;
    let pkg_manifest_reader = BufReader::new(manifest_file);
    serde_json::from_reader(pkg_manifest_reader).map_err(Into::into)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_hash::Hash;
    use serde_json::json;
    use std::str::FromStr;
    use tempfile::NamedTempFile;

    #[test]
    fn invalid_path() {
        assert!(pkg_manifest_from_path("invalid.json").is_err());
    }

    #[test]
    fn valid_path() {
        let value = json!(
            {
                "version": "1",
                "package": {
                    "name": "test",
                    "version": "0.0.0",
                },
                "blobs": [
                    {
                        "source_path": "path/to/file.txt",
                        "path": "meta/",
                        "merkle":
                            "0000000000000000000000000000000000000000000000000000000000000000",
                        "size": 1
                    },
                ]
            }
        );
        let file = NamedTempFile::new().unwrap();
        serde_json::ser::to_writer(&file, &value).unwrap();

        let manifest = pkg_manifest_from_path(file.path()).unwrap();
        assert_eq!(manifest.name().as_ref(), "test");
        let blobs = manifest.into_blobs();
        assert_eq!(blobs.len(), 1);
        assert_eq!(blobs[0].source_path, "path/to/file.txt");
        assert_eq!(blobs[0].path, "meta/");
        assert_eq!(
            blobs[0].merkle,
            Hash::from_str("0000000000000000000000000000000000000000000000000000000000000000")
                .unwrap()
        );
        assert_eq!(blobs[0].size, 1);
    }
}
