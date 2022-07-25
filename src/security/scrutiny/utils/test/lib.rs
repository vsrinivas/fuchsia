// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_merkle::MerkleTree,
    once_cell::sync::Lazy,
    scrutiny_utils::artifact::{ArtifactReader, BlobFsArtifactReader, CompoundArtifactReader},
    std::path::PathBuf,
};

const TESTDATA_DIR: &str = env!("TESTDATA_DIR");
const ALPHA_BETA_BLOBFS_FILE: &str = concat!("alpha_beta_blobfs.blk");
const BETA_GAMMA_BLOBFS_FILE: &str = concat!("beta_gamma_blobfs.blk");
const ALPHA_BYTES: &[u8] = include_bytes!("../testdata/alpha");
const BETA_BYTES: &[u8] = include_bytes!("../testdata/beta");
const GAMMA_BYTES: &[u8] = include_bytes!("../testdata/gamma");
const DELTA_BYTES: &[u8] = include_bytes!("../testdata/delta");

const BLOBFS_PATHS: Lazy<BlobfsPaths> = Lazy::new(|| BlobfsPaths {
    alpha: merkle_as_path_buf(ALPHA_BYTES),
    beta: merkle_as_path_buf(BETA_BYTES),
    gamma: merkle_as_path_buf(GAMMA_BYTES),
    delta: merkle_as_path_buf(DELTA_BYTES),
});

// Merkle strings treated as paths by blobfs.
struct BlobfsPaths {
    alpha: PathBuf,
    beta: PathBuf,
    gamma: PathBuf,
    delta: PathBuf,
}

fn merkle_as_path_buf(data: &[u8]) -> PathBuf {
    MerkleTree::from_reader(data)
        .expect("compute merkle for blobfs file name")
        .root()
        .to_string()
        .into()
}

#[test]
fn test_blobfs() {
    let testdata_dir: PathBuf = TESTDATA_DIR.into();
    let mut blobfs_artifact_reader = BlobFsArtifactReader::try_new(
        testdata_dir.as_path(),
        None::<PathBuf>,
        testdata_dir.join(ALPHA_BETA_BLOBFS_FILE).as_path(),
    )
    .unwrap();
    let paths = BLOBFS_PATHS;
    assert!(blobfs_artifact_reader.read_bytes(&paths.alpha).is_ok());
    assert!(blobfs_artifact_reader.read_bytes(&paths.beta).is_ok());
    assert!(blobfs_artifact_reader.read_bytes(&paths.gamma).is_err());
    assert!(blobfs_artifact_reader.read_bytes(&paths.delta).is_err());
}

#[test]
fn test_compound_blobfs() {
    let testdata_dir: PathBuf = TESTDATA_DIR.into();
    let alpha_beta_artifact_reader = BlobFsArtifactReader::try_new(
        testdata_dir.as_path(),
        None::<PathBuf>,
        testdata_dir.join(ALPHA_BETA_BLOBFS_FILE).as_path(),
    )
    .unwrap();
    let beta_gamma_artifact_reader = BlobFsArtifactReader::try_new(
        testdata_dir.as_path(),
        None::<PathBuf>,
        testdata_dir.join(BETA_GAMMA_BLOBFS_FILE).as_path(),
    )
    .unwrap();
    let mut compound_artifact_reader = CompoundArtifactReader::new(vec![
        Box::new(alpha_beta_artifact_reader),
        Box::new(beta_gamma_artifact_reader),
    ]);
    let paths = BLOBFS_PATHS;
    assert!(compound_artifact_reader.read_bytes(&paths.alpha).is_ok());
    assert!(compound_artifact_reader.read_bytes(&paths.beta).is_ok());
    assert!(compound_artifact_reader.read_bytes(&paths.gamma).is_ok());
    assert!(compound_artifact_reader.read_bytes(&paths.delta).is_err());
}
