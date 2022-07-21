// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{artifact::ArtifactReader, key_value::parse_key_value},
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Utf8Reader,
    fuchsia_merkle::MerkleTree,
    std::{fs::File, io::Cursor, path::Path, str::from_utf8},
};

/// Path within a Fuchsia package that contains the package contents manifest.
pub static META_CONTENTS_PATH: &str = "meta/contents";

pub fn open_update_package<P: AsRef<Path>>(
    update_package_path: P,
    artifact_reader: &mut Box<dyn ArtifactReader>,
) -> Result<Utf8Reader<Cursor<Vec<u8>>>> {
    let update_package_path = update_package_path.as_ref();
    let mut update_package_file = File::open(update_package_path).with_context(|| {
        format!("Failed to open update package meta.far at {:?}", update_package_path)
    })?;
    let update_package_merkle = MerkleTree::from_reader(&mut update_package_file)
        .with_context(|| {
            format!(
                "Failed to compute merkle root of update package meta.far at {:?}",
                update_package_path
            )
        })?
        .root()
        .to_string();
    let far_contents =
        artifact_reader.read_bytes(&Path::new(&update_package_merkle)).with_context(|| {
            format!(
                "Failed to load update package meta.far at {:?} from artifact archives",
                update_package_path
            )
        })?;
    Utf8Reader::new(Cursor::new(far_contents)).with_context(|| {
        format!(
            "Failed to initialize far reader for update package at {:?} with merkle root {}",
            update_package_path, update_package_merkle
        )
    })
}

pub fn read_content_blob(
    far_reader: &mut Utf8Reader<Cursor<Vec<u8>>>,
    artifact_reader: &mut Box<dyn ArtifactReader>,
    path: &str,
) -> Result<Vec<u8>> {
    let meta_contents = far_reader
        .read_file(META_CONTENTS_PATH)
        .context("Failed to read meta/contents from package")?;
    let meta_contents = from_utf8(meta_contents.as_slice())
        .context("Failed to convert package meta/contents from bytes to string")?;
    let paths_to_merkles = parse_key_value(meta_contents)
        .context("Failed to parse path=merkle pairs in package meta/contents file")?;
    let merkle_root = paths_to_merkles
        .get(path)
        .ok_or_else(|| anyhow!("Package does not contain file: {}", path))?;
    artifact_reader
        .read_bytes(&Path::new(merkle_root))
        .with_context(|| format!("Failed to load file from package: {}", path))
}
