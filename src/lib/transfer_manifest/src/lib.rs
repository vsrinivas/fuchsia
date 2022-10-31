// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Construct and parse a transfer manifest that indicates how to move artifacts between local and
//! remote locations.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

/// A verioned manifest describing what to upload or download.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
#[serde(tag = "version")]
pub enum TransferManifest {
    /// Version 1 of the transfer manifest.
    #[serde(rename = "1")]
    V1(TransferManifestV1),
}

/// Version 1 of the transfer manifest that contains a list of entries to upload or download.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct TransferManifestV1 {
    /// List of entries to transfer.
    pub entries: Vec<TransferEntry>,
}

/// A single entry to upload or download.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct TransferEntry {
    /// The type of artifacts to transfer.
    #[serde(rename = "type")]
    pub artifact_type: ArtifactType,
    /// The local directory to find or download the artifact into that is relative to the transfer
    /// manifest itself.
    pub local: PathBuf,
    /// The remote directory to store or download the artifact from.
    pub remote: PathBuf,
    /// Which files inside either `local` or `remote` that should be transferred.
    pub entries: Vec<ArtifactEntry>,
}

/// The type of artifacts to transfer, which can indicate to the uploader and downloader where to
/// place the artifacts.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub enum ArtifactType {
    /// Fuchsia package blobs.
    #[serde(rename = "blobs")]
    Blobs,
    /// A collection of files.
    #[serde(rename = "files")]
    Files,
}

/// A single artifact inside a local or remote location.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct ArtifactEntry {
    /// The path to the file in the local or remote location.
    pub name: PathBuf,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_deserialization() {
        let expected = TransferManifest::V1(TransferManifestV1 {
            entries: vec![
                TransferEntry {
                    artifact_type: ArtifactType::Blobs,
                    local: PathBuf::from("path/to/local/a"),
                    remote: "gs://bucket/a".into(),
                    entries: vec![
                        ArtifactEntry { name: PathBuf::from("one") },
                        ArtifactEntry { name: PathBuf::from("two") },
                    ],
                },
                TransferEntry {
                    artifact_type: ArtifactType::Files,
                    local: PathBuf::from("path/to/local/b"),
                    remote: "gs://bucket/b".into(),
                    entries: vec![
                        ArtifactEntry { name: PathBuf::from("three") },
                        ArtifactEntry { name: PathBuf::from("four") },
                    ],
                },
            ],
        });
        let value = serde_json::json!({
            "version": "1",
            "entries": [
                {
                    "type": "blobs",
                    "local": "path/to/local/a",
                    "remote": "gs://bucket/a",
                    "entries": [
                        { "name": "one" },
                        { "name": "two" },
                    ],
                },
                {
                    "type": "files",
                    "local": "path/to/local/b",
                    "remote": "gs://bucket/b",
                    "entries": [
                        { "name": "three" },
                        { "name": "four" },
                    ],
                },
            ],
        });
        let manifest: TransferManifest = serde_json::from_value(value).unwrap();
        assert_eq!(expected, manifest);
    }
}
