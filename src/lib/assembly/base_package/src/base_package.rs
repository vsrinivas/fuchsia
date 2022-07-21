// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    assembly_package_list::{PackageList, PackageUrlList, WritablePackageList},
    fuchsia_pkg::{PackageBuilder, PackageManifest},
    std::path::PathBuf,
    std::{collections::BTreeMap, path::Path},
};

/// The path to the static package index file in the `base` package.
const STATIC_PACKAGE_INDEX: &str = "data/static_packages";

/// The path to the cache package index file in the `base` package.
const CACHE_PACKAGE_INDEX: &str = "data/cache_packages.json";

/// A builder that constructs base packages.
#[derive(Default)]
pub struct BasePackageBuilder {
    // Maps the blob destination -> source.
    contents: BTreeMap<String, String>,
    base_packages: PackageList,
    cache_packages: PackageUrlList,
}

impl BasePackageBuilder {
    /// Add all the blobs from `package` into the base package being built.
    pub fn add_files_from_package(&mut self, package: PackageManifest) {
        package.into_blobs().into_iter().filter(|b| b.path != "meta/").for_each(|b| {
            self.contents.insert(b.path, b.source_path);
        });
    }

    /// Add the `package` to the list of base packages, which is then added to
    /// base package as file `data/static_packages`.
    pub fn add_base_package(&mut self, package: PackageManifest) -> Result<()> {
        self.base_packages.add_package(package)
    }

    /// Add the `package` to the list of cache packages, which is then added to
    /// base package as file `data/cache_packages`.
    pub fn add_cache_package(&mut self, package: PackageManifest) -> Result<()> {
        self.cache_packages.add_package(package)
    }

    /// Build the base package and write the bytes to `out`.
    ///
    /// Intermediate files will be written to the directory specified by
    /// `gendir`.
    pub fn build(
        self,
        outdir: impl AsRef<Path>,
        gendir: impl AsRef<Path>,
        name: impl AsRef<str>,
        out: impl AsRef<Path>,
    ) -> Result<BasePackageBuildResults> {
        // Write all generated files in a subdir with the name of the package.
        let gendir = gendir.as_ref().join(name.as_ref());

        let Self { contents, base_packages, cache_packages } = self;

        // Capture the generated files
        let mut generated_files = BTreeMap::new();

        // Generate the base and cache package lists.
        let (dest, path) = base_packages.write_index_file(&gendir, "base", STATIC_PACKAGE_INDEX)?;
        generated_files.insert(dest, path);

        let (dest, path) =
            cache_packages.write_index_file(&gendir, "cache", CACHE_PACKAGE_INDEX)?;
        generated_files.insert(dest, path);

        // Construct the list of blobs in the base package that lives outside of the meta.far.
        let mut external_contents = contents;
        for (destination, source) in &generated_files {
            external_contents.insert(destination.clone(), source.clone());
        }

        // All base packages are named 'system-image' internally, for
        // consistency on the platform.
        let mut builder = PackageBuilder::new("system_image");
        // However, they can have different published names.  And the name here
        // is the name to publish it under (and to include in the generated
        // package manifest).
        builder.published_name(name);

        for (destination, source) in &external_contents {
            builder.add_file_as_blob(destination, source)?;
        }
        let manifest_path = outdir.as_ref().join("package_manifest.json");
        builder.manifest_path(manifest_path.clone());
        builder.build(gendir, out)?;

        Ok(BasePackageBuildResults {
            contents: external_contents,
            base_packages,
            cache_packages,
            generated_files,
            manifest_path: manifest_path.clone(),
        })
    }
}

/// The results of building the `base` package.
///
/// These are based on the information that the builder is configured with, and
/// then augmented with the operations that the `BasePackageBuilder::build()`
/// fn performs, including an extra additions or removals.
///
/// This provides an audit trail of "what was created".
pub struct BasePackageBuildResults {
    // Maps the blob destination -> source.
    pub contents: BTreeMap<String, String>,
    pub base_packages: PackageList,
    pub cache_packages: PackageUrlList,

    /// The paths to the files generated by the builder.
    pub generated_files: BTreeMap<String, String>,

    pub manifest_path: PathBuf,
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_archive::Utf8Reader;
    use fuchsia_hash::Hash;
    use fuchsia_pkg::{BlobInfo, MetaPackage, PackageManifest, PackageManifestBuilder};
    use fuchsia_url::PinnedAbsolutePackageUrl;
    use std::fs::File;
    use std::path::Path;
    use tempfile::{NamedTempFile, TempDir};

    #[test]
    fn build_with_unsupported_packages() {
        let mut builder = BasePackageBuilder::default();
        assert!(builder.add_base_package(generate_test_manifest("system_image", None)).is_err());
        assert!(builder.add_base_package(generate_test_manifest("update", None)).is_err());
    }

    #[test]
    fn build() {
        let outdir = TempDir::new().unwrap();
        let far_path = outdir.path().join("base.far");

        // Build the base package with an extra file, a base package, and a cache package.
        let mut builder = BasePackageBuilder::default();
        let test_file = NamedTempFile::new().unwrap();
        builder.add_files_from_package(generate_test_manifest("package", Some(test_file.path())));
        builder.add_base_package(generate_test_manifest("base_package", None)).unwrap();
        builder.add_cache_package(generate_test_manifest("cache_package", None)).unwrap();

        let gendir = TempDir::new().unwrap();
        let build_results =
            builder.build(&outdir.path(), &gendir.path(), "system_image", &far_path).unwrap();

        // The following asserts lead up to the final one, catching earlier failure points where it
        // can be more obvious as to why the test is failing, as the hashes themselves are opaque.

        // Verify the package list intermediate structures.
        assert_eq!(
            vec![("base_package/0".to_string(), Hash::from([0u8; 32]))],
            build_results.base_packages
        );
        let url: PinnedAbsolutePackageUrl = "fuchsia-pkg://testrepository.com/cache_package/0\
             ?hash=0000000000000000000000000000000000000000000000000000000000000000"
            .parse()
            .unwrap();
        assert_eq!(&vec![url], build_results.cache_packages.get_packages());

        // Inspect the generated files to verify their contents.
        let gen_static_index = build_results.generated_files.get("data/static_packages").unwrap();
        assert_eq!(
            "base_package/0=0000000000000000000000000000000000000000000000000000000000000000\n",
            std::fs::read_to_string(gen_static_index).unwrap()
        );

        let gen_cache_index =
            build_results.generated_files.get("data/cache_packages.json").unwrap();
        let cache_packages_json = r#"{"content":["fuchsia-pkg://testrepository.com/cache_package/0?hash=0000000000000000000000000000000000000000000000000000000000000000"],"version":"1"}"#;

        assert_eq!(cache_packages_json, std::fs::read_to_string(gen_cache_index).unwrap());

        // Validate that the generated files are in the contents.
        for (generated_file, _) in &build_results.generated_files {
            assert!(
                build_results.contents.contains_key(generated_file),
                "Unable to find generated file in base package contents: {}",
                generated_file
            );
        }

        // Read the output and ensure it contains the right files (and their hashes)
        let mut far_reader = Utf8Reader::new(File::open(&far_path).unwrap()).unwrap();
        let package = far_reader.read_file("meta/package").unwrap();
        assert_eq!(br#"{"name":"system_image","version":"0"}"#, &*package);
        let contents = far_reader.read_file("meta/contents").unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        let expected_contents = "\
            data/cache_packages.json=49d59d7e9567de7ce2d5fc8632ea544965402426a8fa66456fbd68dccca36b4c\n\
            data/file.txt=15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b\n\
            data/static_packages=2d86ccb37d003499bdc7bdd933428f4b83d9ed224d1b64ad90dc257d22cff460\n\
        "
        .to_string();
        assert_eq!(expected_contents, contents);
    }

    // Generates a package manifest to be used for testing. The `name` is used in the blob file
    // names to make each manifest somewhat unique. If supplied, `file_path` will be used as the
    // non-meta-far blob source path, which allows the tests to use a real file.
    fn generate_test_manifest(name: &str, file_path: Option<&Path>) -> PackageManifest {
        let meta_source = format!("path/to/{}/meta.far", name);
        let file_source = match file_path {
            Some(path) => path.to_string_lossy().into_owned(),
            _ => format!("path/to/{}/file.txt", name),
        };
        let builder = PackageManifestBuilder::new(MetaPackage::from_name(name.parse().unwrap()));
        let builder = builder.repository("testrepository.com");
        let builder = builder.add_blob(BlobInfo {
            source_path: meta_source,
            path: "meta/".into(),
            merkle: "0000000000000000000000000000000000000000000000000000000000000000"
                .parse()
                .unwrap(),
            size: 1,
        });
        let builder = builder.add_blob(BlobInfo {
            source_path: file_source,
            path: "data/file.txt".into(),
            merkle: "1111111111111111111111111111111111111111111111111111111111111111"
                .parse()
                .unwrap(),
            size: 1,
        });
        builder.build()
    }
}
