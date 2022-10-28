// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{MetaContents, MetaContentsError, MetaPackage};
use anyhow::Result;
use fuchsia_merkle::Hash;
use fuchsia_url::PackageName;
use std::collections::{BTreeMap, HashMap};
use std::io::{Read, Seek};
use std::path::PathBuf;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Package {
    meta_contents: MetaContents,
    meta_package: MetaPackage,
    blobs: BTreeMap<String, BlobEntry>,
}

impl Package {
    /// Get the meta_contents.
    pub fn meta_contents(&self) -> MetaContents {
        self.meta_contents.clone()
    }

    /// Get the meta_package.
    pub fn meta_package(&self) -> MetaPackage {
        self.meta_package.clone()
    }

    /// Get the blobs.
    pub fn blobs(&self) -> BTreeMap<String, BlobEntry> {
        self.blobs.clone()
    }

    /// Create a new `PackageBuilder` from a package name.
    pub(crate) fn builder(name: PackageName) -> Builder {
        Builder::new(name)
    }

    /// Generate a Package from a meta.far file.
    pub fn from_meta_far<R: Read + Seek>(
        mut meta_far: R,
        blobs: BTreeMap<String, BlobEntry>,
    ) -> Result<Self> {
        let mut meta_far = fuchsia_archive::Utf8Reader::new(&mut meta_far)?;
        let meta_contents =
            MetaContents::deserialize(meta_far.read_file("meta/contents")?.as_slice())?;
        let meta_package =
            MetaPackage::deserialize(meta_far.read_file("meta/package")?.as_slice())?;
        Ok(Package { meta_contents, meta_package, blobs })
    }
}

pub(crate) struct Builder {
    contents: HashMap<String, Hash>,
    meta_package: MetaPackage,
    blobs: BTreeMap<String, BlobEntry>,
}

impl Builder {
    pub(crate) fn new(name: PackageName) -> Self {
        Self {
            contents: HashMap::new(),
            meta_package: MetaPackage::from_name(name),
            blobs: BTreeMap::new(),
        }
    }

    pub(crate) fn add_entry(
        &mut self,
        blob_path: String,
        hash: Hash,
        source_path: PathBuf,
        size: u64,
    ) {
        if blob_path != "meta/" {
            self.contents.insert(blob_path.clone(), hash);
        }
        self.blobs.insert(blob_path, BlobEntry { source_path, size, hash });
    }

    pub(crate) fn build(self) -> Result<Package, MetaContentsError> {
        Ok(Package {
            meta_contents: MetaContents::from_map(self.contents)?,
            meta_package: self.meta_package,
            blobs: self.blobs,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BlobEntry {
    source_path: PathBuf,
    hash: Hash,
    size: u64,
}

impl BlobEntry {
    pub fn source_path(&self) -> PathBuf {
        self.source_path.clone()
    }

    pub fn size(&self) -> u64 {
        self.size
    }

    pub fn hash(&self) -> Hash {
        self.hash
    }
}

#[cfg(test)]
mod test_package {
    use super::*;
    use crate::build::{build_with_file_system, FileSystem};
    use crate::CreationManifest;

    use fuchsia_merkle::Hash;
    use maplit::{btreemap, hashmap};
    use std::collections::HashMap;
    use std::fs::File;
    use std::io;
    use std::str::FromStr;
    use tempfile::tempdir;

    #[test]
    fn test_create_package() {
        let meta_package = MetaPackage::from_name("package-name".parse().unwrap());

        let map = hashmap! {
        "bin/my_prog".to_string() =>
            Hash::from_str(
             "0000000000000000000000000000000000000000000000000000000000000000")
            .unwrap(),
        "lib/mylib.so".to_string() =>
            Hash::from_str(
               "1111111111111111111111111111111111111111111111111111111111111111")
            .unwrap(),
            };
        let meta_contents = MetaContents::from_map(map).unwrap();
        let blob_entry = BlobEntry {
            source_path: PathBuf::from("src/bin/my_prog"),
            size: 1,
            hash: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
        };
        let blobs = btreemap! {
            "bin/my_prog".to_string() => blob_entry.clone(),
            "bin/my_prog2".to_string() => blob_entry,
        };
        let package = Package {
            meta_contents: meta_contents.clone(),
            meta_package: meta_package.clone(),
            blobs: blobs.clone(),
        };
        assert_eq!(meta_package, package.meta_package());
        assert_eq!(meta_contents, package.meta_contents());
        assert_eq!(blobs, package.blobs());
    }

    struct FakeFileSystem {
        content_map: HashMap<String, Vec<u8>>,
    }

    impl<'a> FileSystem<'a> for FakeFileSystem {
        type File = &'a [u8];
        fn open(&'a self, path: &str) -> Result<Self::File, io::Error> {
            Ok(self.content_map.get(path).unwrap().as_slice())
        }
        fn len(&self, path: &str) -> Result<u64, io::Error> {
            Ok(self.content_map.get(path).unwrap().len() as u64)
        }
        fn read(&self, path: &str) -> Result<Vec<u8>, io::Error> {
            Ok(self.content_map.get(path).unwrap().clone())
        }
    }

    #[test]
    fn test_from_meta_far_valid_meta_far() {
        let outdir = tempdir().unwrap();
        let meta_far_path = outdir.path().join("base.far");

        let creation_manifest = CreationManifest::from_external_and_far_contents(
            btreemap! {
                "lib/mylib.so".to_string() => "host/mylib.so".to_string()
            },
            btreemap! {
                "meta/my_component.cml".to_string() => "host/my_component.cml".to_string(),
                "meta/package".to_string() => "host/meta/package".to_string()
            },
        )
        .unwrap();
        let component_manifest_contents = "my_component.cml contents";
        let mut v = vec![];
        let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                "host/mylib.so".to_string() => "mylib.so contents".as_bytes().to_vec(),
                "host/my_component.cml".to_string() => component_manifest_contents.as_bytes().to_vec(),
                "host/meta/package".to_string() => v
            },
        };

        build_with_file_system(
            &creation_manifest,
            &meta_far_path,
            "my-package-name",
            None,
            &file_system,
        )
        .unwrap();

        let blob_entry = BlobEntry {
            source_path: PathBuf::from("src/bin/my_prog"),
            size: 1,
            hash: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
        };
        let blobs = btreemap! {
            "bin/my_prog".to_string() => blob_entry.clone(),
            "bin/my_prog2".to_string() => blob_entry,
        };
        let package =
            Package::from_meta_far(File::open(&meta_far_path).unwrap(), blobs.clone()).unwrap();
        assert_eq!(blobs, package.blobs());
        assert_eq!(
            &"my-package-name".parse::<PackageName>().unwrap(),
            package.meta_package().name()
        );
    }

    #[test]
    fn test_from_meta_far_empty_meta_far() {
        let dir = tempdir().unwrap();
        let file_path = dir.path().join("meta.far");
        File::create(&file_path).unwrap();
        let file = File::open(&file_path).unwrap();
        let blob_entry = BlobEntry {
            source_path: PathBuf::from("src/bin/my_prog"),
            size: 1,
            hash: Hash::from_str(
                "0000000000000000000000000000000000000000000000000000000000000000",
            )
            .unwrap(),
        };
        let blobs = btreemap! {
            "bin/my_prog".to_string() => blob_entry.clone(),
            "bin/my_prog2".to_string() => blob_entry,
        };
        let package = Package::from_meta_far(file, blobs);
        assert!(package.is_err());
    }
}
