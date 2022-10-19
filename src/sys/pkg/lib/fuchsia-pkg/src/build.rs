// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::BuildError;
use crate::{CreationManifest, MetaContents, MetaPackageError, Package, PackageManifest};
use fuchsia_merkle::{Hash, MerkleTree};
use std::collections::{btree_map, BTreeMap};
use std::io::{Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::{fs, io};
use tempfile::NamedTempFile;

pub(crate) fn build(
    creation_manifest: &CreationManifest,
    meta_far_path: impl AsRef<Path>,
    published_name: impl AsRef<str>,
    repository: Option<String>,
) -> Result<PackageManifest, BuildError> {
    build_with_file_system(
        creation_manifest,
        meta_far_path,
        published_name,
        repository,
        &ActualFileSystem {},
    )
}

// Used to mock out native filesystem for testing
pub(crate) trait FileSystem<'a> {
    type File: io::Read;
    fn open(&'a self, path: &str) -> Result<Self::File, io::Error>;
    fn len(&self, path: &str) -> Result<u64, io::Error>;
    fn read(&self, path: &str) -> Result<Vec<u8>, io::Error>;
}

struct ActualFileSystem;

impl FileSystem<'_> for ActualFileSystem {
    type File = std::fs::File;
    fn open(&self, path: &str) -> Result<Self::File, io::Error> {
        Ok(fs::File::open(path)?)
    }
    fn len(&self, path: &str) -> Result<u64, io::Error> {
        Ok(fs::metadata(path)?.len())
    }
    fn read(&self, path: &str) -> Result<Vec<u8>, io::Error> {
        fs::read(path)
    }
}

pub(crate) fn build_with_file_system<'a>(
    creation_manifest: &CreationManifest,
    meta_far_path: impl AsRef<Path>,
    published_name: impl AsRef<str>,
    repository: Option<String>,
    file_system: &'a impl FileSystem<'a>,
) -> Result<PackageManifest, BuildError> {
    if creation_manifest.far_contents().get("meta/package").is_none() {
        return Err(BuildError::MetaPackage(MetaPackageError::MetaPackageMissing));
    };

    let mut package_builder =
        Package::builder(published_name.as_ref().parse().map_err(BuildError::PackageName)?);

    let external_content_infos =
        get_external_content_infos(creation_manifest.external_contents(), file_system)?;

    for (path, info) in external_content_infos.iter() {
        package_builder.add_entry(
            path.to_string(),
            info.hash,
            PathBuf::from(info.source_path),
            info.size,
        );
    }

    let meta_contents = MetaContents::from_map(
        external_content_infos.iter().map(|(path, info)| (path.clone(), info.hash)).collect(),
    )?;

    let mut meta_contents_bytes = Vec::new();
    meta_contents.serialize(&mut meta_contents_bytes)?;

    let mut far_contents: BTreeMap<&str, Vec<u8>> = BTreeMap::new();
    for (resource_path, source_path) in creation_manifest.far_contents() {
        far_contents.insert(
            resource_path,
            file_system.read(source_path).map_err(|e| (e, source_path.to_string()))?,
        );
    }

    let insert_generated_file =
        |resource_path: &'static str, content, far_contents: &mut BTreeMap<_, _>| match far_contents
            .entry(resource_path)
        {
            btree_map::Entry::Vacant(entry) => {
                entry.insert(content);
                Ok(())
            }
            btree_map::Entry::Occupied(_) => Err(BuildError::ConflictingResource {
                conflicting_resource_path: resource_path.to_string(),
            }),
        };
    insert_generated_file("meta/contents", meta_contents_bytes, &mut far_contents)?;
    let mut meta_entries: BTreeMap<&str, (u64, Box<dyn io::Read>)> = BTreeMap::new();
    for (resource_path, content) in &far_contents {
        meta_entries.insert(resource_path, (content.len() as u64, Box::new(content.as_slice())));
    }

    // Write the meta-far to a temporary file.
    let mut meta_far_file = if let Some(parent) = meta_far_path.as_ref().parent() {
        NamedTempFile::new_in(parent)?
    } else {
        NamedTempFile::new()?
    };
    fuchsia_archive::write(&meta_far_file, meta_entries)?;

    // Calculate the merkle of the meta-far.
    meta_far_file.seek(SeekFrom::Start(0))?;
    let meta_far_merkle = MerkleTree::from_reader(&meta_far_file)?.root();

    // Calculate the size of the meta-far.
    let meta_far_size = meta_far_file.as_file().metadata()?.len();

    // Replace the existing meta-far with the new file.
    meta_far_file.persist(&meta_far_path).map_err(|err| err.error)?;

    // Add the meta-far as an entry to the package.
    package_builder.add_entry(
        "meta/".to_string(),
        meta_far_merkle,
        meta_far_path.as_ref().to_path_buf(),
        meta_far_size,
    );

    let package = package_builder.build()?;
    let package_manifest = PackageManifest::from_package(package, repository)?;
    Ok(package_manifest)
}

struct ExternalContentInfo<'a> {
    source_path: &'a str,
    size: u64,
    hash: Hash,
}

fn get_external_content_infos<'a, 'b>(
    external_contents: &'a BTreeMap<String, String>,
    file_system: &'b impl FileSystem<'b>,
) -> Result<BTreeMap<String, ExternalContentInfo<'a>>, BuildError> {
    external_contents
        .iter()
        .map(|(resource_path, source_path)| -> Result<(String, ExternalContentInfo<'_>), BuildError> {
            let file = file_system.open(source_path)
                .map_err(|e| (e, source_path.to_string()))?;
            Ok((
                resource_path.clone(),
                ExternalContentInfo {
                    source_path,
                    size: file_system.len(source_path)?,
                    hash: MerkleTree::from_reader(file)?.root(),
                },
            ))
        })
        .collect()
}

#[cfg(test)]
mod test_build_with_file_system {
    use super::*;
    use crate::{test::*, MetaPackage};
    use assert_matches::assert_matches;
    use maplit::{btreemap, hashmap};
    use proptest::prelude::*;
    use rand::{Rng as _, SeedableRng as _};
    use std::collections::{HashMap, HashSet};
    use std::fs::File;
    use std::io;
    use std::iter::FromIterator;
    use tempfile::TempDir;

    const GENERATED_FAR_CONTENTS: [&str; 2] = ["meta/contents", "meta/package"];

    struct FakeFileSystem {
        content_map: HashMap<String, Vec<u8>>,
    }

    impl FakeFileSystem {
        fn from_creation_manifest_with_random_contents(
            creation_manifest: &CreationManifest,
            rng: &mut impl rand::Rng,
        ) -> FakeFileSystem {
            let mut content_map = HashMap::new();
            for (resource_path, host_path) in
                creation_manifest.far_contents().iter().chain(creation_manifest.external_contents())
            {
                if resource_path.to_string() == "meta/package".to_string() {
                    let mut v = vec![];
                    let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
                    meta_package.serialize(&mut v).unwrap();
                    content_map.insert(host_path.to_string(), v);
                } else {
                    let file_size = rng.gen_range(0..6000);
                    content_map.insert(
                        host_path.to_string(),
                        rng.sample_iter(&rand::distributions::Standard).take(file_size).collect(),
                    );
                }
            }
            Self { content_map }
        }
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
    fn test_verify_far_contents_with_fixed_inputs() {
        let outdir = TempDir::new().unwrap();
        let meta_far_path = outdir.path().join("meta.far");

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
                "host/meta/package".to_string() => v.clone()
            },
        };
        build_with_file_system(
            &creation_manifest,
            &meta_far_path,
            "published-name",
            None,
            &file_system,
        )
        .unwrap();
        let mut reader =
            fuchsia_archive::Utf8Reader::new(File::open(&meta_far_path).unwrap()).unwrap();
        let actual_meta_package_bytes = reader.read_file("meta/package").unwrap();
        let expected_meta_package_bytes = v.as_slice();
        assert_eq!(actual_meta_package_bytes.as_slice(), &expected_meta_package_bytes[..]);
        let actual_meta_contents_bytes = reader.read_file("meta/contents").unwrap();
        let expected_meta_contents_bytes =
            b"lib/mylib.so=4a886105646222c10428e5793868b13f536752d4b87e6497cdf9caed37e67410\n";
        assert_eq!(actual_meta_contents_bytes.as_slice(), &expected_meta_contents_bytes[..]);
        let actual_meta_component_bytes = reader.read_file("meta/my_component.cml").unwrap();
        assert_eq!(actual_meta_component_bytes.as_slice(), component_manifest_contents.as_bytes());
    }

    #[test]
    fn test_reject_conflict_with_generated_file() {
        let outdir = TempDir::new().unwrap();
        let meta_far_path = outdir.path().join("meta.far");

        let creation_manifest = CreationManifest::from_external_and_far_contents(
            BTreeMap::new(),
            btreemap! {
                "meta/contents".to_string() => "some-host-path".to_string(),
                "meta/package".to_string() => "host/meta/package".to_string()
            },
        )
        .unwrap();
        let mut v = vec![];
        let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
        meta_package.serialize(&mut v).unwrap();
        let file_system = FakeFileSystem {
            content_map: hashmap! {
                "some-host-path".to_string() => Vec::new(),
                "host/meta/package".to_string() => v
            },
        };
        let result = build_with_file_system(
            &creation_manifest,
            &meta_far_path,
            "published-name",
            None,
            &file_system,
        );
        assert_matches!(
            result,
            Err(BuildError::ConflictingResource {
                conflicting_resource_path: path
            }) if path == "meta/contents".to_string()
        );
    }
    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn test_meta_far_directory_names_are_exactly_generated_files_and_creation_manifest_far_contents(
            creation_manifest in random_creation_manifest(),
            seed: u64)
        {
            let outdir = TempDir::new().unwrap();
            let meta_far_path = outdir.path().join("meta.far");

            let mut private_key_bytes = [0u8; 32];
            let mut prng = rand::rngs::StdRng::seed_from_u64(seed);
            prng.fill(&mut private_key_bytes);
            let file_system = FakeFileSystem::from_creation_manifest_with_random_contents(
                &creation_manifest, &mut prng);
            build_with_file_system(
                &creation_manifest,
                &meta_far_path,
                "published-name",
                None,
                &file_system,
            )
                .unwrap();
            let reader =
                fuchsia_archive::Utf8Reader::new(File::open(&meta_far_path).unwrap()).unwrap();
            let expected_far_directory_names = {
                let mut map: HashSet<&str> = HashSet::new();
                for path in GENERATED_FAR_CONTENTS.iter() {
                    map.insert(*path);
                }
                for (path, _) in creation_manifest.far_contents().iter() {
                    map.insert(path);
                }
                map
            };
            let actual_far_directory_names = reader.list().map(|e| e.path()).collect();
            prop_assert_eq!(expected_far_directory_names, actual_far_directory_names);
        }

        #[test]
        fn test_meta_far_contains_creation_manifest_far_contents(
            creation_manifest in random_creation_manifest(),
            seed: u64)
        {
            let outdir = TempDir::new().unwrap();
            let meta_far_path = outdir.path().join("meta.far");

            let mut private_key_bytes = [0u8; 32];
            let mut prng = rand::rngs::StdRng::seed_from_u64(seed);
            prng.fill(&mut private_key_bytes);
            let file_system = FakeFileSystem::from_creation_manifest_with_random_contents(
                &creation_manifest, &mut prng);
            build_with_file_system(
                &creation_manifest,
                &meta_far_path,
                "published-name",
                None,
                &file_system,
            )
                .unwrap();
            let mut reader =
                fuchsia_archive::Utf8Reader::new(File::open(&meta_far_path).unwrap()).unwrap();
            for (resource_path, host_path) in creation_manifest.far_contents().iter() {
                let expected_contents = file_system.content_map.get(host_path).unwrap();
                let actual_contents = reader.read_file(resource_path).unwrap();
                prop_assert_eq!(expected_contents, &actual_contents);
            }
        }

        #[test]
        fn test_meta_far_meta_contents_lists_creation_manifest_external_contents(
            creation_manifest in random_creation_manifest(),
            seed: u64)
        {
            let outdir = TempDir::new().unwrap();
            let meta_far_path = outdir.path().join("meta.far");

            let mut private_key_bytes = [0u8; 32];
            let mut prng = rand::rngs::StdRng::seed_from_u64(seed);
            prng.fill(&mut private_key_bytes);
            let file_system = FakeFileSystem::from_creation_manifest_with_random_contents(
                &creation_manifest, &mut prng);
            build_with_file_system(
                &creation_manifest,
                &meta_far_path,
                "published-name",
                None,
                &file_system,
            )
                .unwrap();
            let mut reader =
                fuchsia_archive::Utf8Reader::new(File::open(&meta_far_path).unwrap()).unwrap();
            let meta_contents =
                MetaContents::deserialize(
                    reader.read_file("meta/contents").unwrap().as_slice())
                .unwrap();
            let actual_external_contents: HashSet<&str> = meta_contents
                .contents()
                .keys()
                .map(|s| s.as_str())
                .collect();
            let expected_external_contents: HashSet<&str> =
                HashSet::from_iter(
                    creation_manifest
                        .external_contents()
                        .keys()
                        .map(|s| s.as_str()));
            prop_assert_eq!(expected_external_contents, actual_external_contents);
        }
    }
}

#[cfg(test)]
mod test_build {
    use super::*;
    use crate::{test::*, MetaPackage};
    use proptest::prelude::*;
    use rand::{Rng as _, SeedableRng as _};
    use std::fs;
    use std::io::Write;
    use std::path::PathBuf;
    use tempfile::TempDir;

    // Creates a temporary directory, then for each host path in the `CreationManifest`'s
    // external contents and far contents maps creates a file in the temporary directory
    // with path "${TEMP_DIR}/${HOST_PATH}" and random size and contents.
    // Returns a new `CreationManifest` with updated host paths and the `TempDir`.
    fn populate_filesystem_from_creation_manifest(
        creation_manifest: CreationManifest,
        rng: &mut impl rand::Rng,
    ) -> (CreationManifest, TempDir) {
        let temp_dir = TempDir::new().unwrap();
        let temp_dir_path = temp_dir.path();
        fn populate_filesystem_and_make_new_map(
            path_prefix: &std::path::Path,
            resource_to_host_path: &BTreeMap<String, String>,
            rng: &mut impl rand::Rng,
        ) -> BTreeMap<String, String> {
            let mut new_map = BTreeMap::new();
            for (resource_path, host_path) in resource_to_host_path {
                let new_host_path = PathBuf::from(path_prefix.join(host_path).to_str().unwrap());
                fs::create_dir_all(new_host_path.parent().unwrap()).unwrap();
                let mut f = fs::File::create(&new_host_path).unwrap();
                if resource_path.to_string() == "meta/package".to_string() {
                    let meta_package = MetaPackage::from_name("my-package-name".parse().unwrap());
                    meta_package.serialize(f).unwrap();
                } else {
                    let file_size = rng.gen_range(0..6000);
                    f.write_all(
                        rng.sample_iter(&rand::distributions::Standard)
                            .take(file_size)
                            .collect::<Vec<u8>>()
                            .as_slice(),
                    )
                    .unwrap();
                }
                new_map.insert(
                    resource_path.to_string(),
                    new_host_path.into_os_string().into_string().unwrap(),
                );
            }
            new_map
        }
        let new_far_contents = populate_filesystem_and_make_new_map(
            temp_dir_path,
            creation_manifest.far_contents(),
            rng,
        );
        let new_external_contents = populate_filesystem_and_make_new_map(
            temp_dir_path,
            creation_manifest.external_contents(),
            rng,
        );
        let new_creation_manifest = CreationManifest::from_external_and_far_contents(
            new_external_contents,
            new_far_contents,
        )
        .unwrap();
        (new_creation_manifest, temp_dir)
    }

    proptest! {
        #![proptest_config(ProptestConfig{
            failure_persistence: None,
            ..Default::default()
        })]

        #[test]
        fn test_meta_far_contains_creation_manifest_far_contents(
            creation_manifest in random_creation_manifest(),
            seed: u64)
        {
            let outdir = TempDir::new().unwrap();
            let meta_far_path = outdir.path().join("meta.far");

            let mut prng = rand::rngs::StdRng::seed_from_u64(seed);
            let (creation_manifest, _temp_dir) = populate_filesystem_from_creation_manifest(creation_manifest, &mut prng);
            let mut private_key_bytes = [0u8; 32];
            prng.fill(&mut private_key_bytes);
            build(
                &creation_manifest,
                &meta_far_path,
                "published-name",
                None,
            )
                .unwrap();
            let mut reader =
                fuchsia_archive::Utf8Reader::new(fs::File::open(&meta_far_path).unwrap()).unwrap();
            for (resource_path, host_path) in creation_manifest.far_contents().iter() {
                let expected_contents = std::fs::read(host_path).unwrap();
                let actual_contents = reader.read_file(resource_path).unwrap();
                prop_assert_eq!(expected_contents, actual_contents);
            }
        }
    }
}
