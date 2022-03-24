// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::util;
use anyhow::{anyhow, ensure, Context, Result};
use assembly_config::{
    self as image_assembly_config,
    product_config::{AssemblyInputBundle, PackageConfigPatch, StructuredConfigPatches},
    FileEntry,
};
use assembly_config_data::ConfigDataBuilder;
use assembly_structured_config::Repackager;
use assembly_util::{DuplicateKeyError, InsertAllUniqueExt, InsertUniqueExt, MapEntry};
use fuchsia_pkg::PackageManifest;
use std::path::Path;
use std::{
    collections::{BTreeMap, BTreeSet},
    path::PathBuf,
};

type FileEntryMap = BTreeMap<String, PathBuf>;
type ConfigDataMap = BTreeMap<String, FileEntryMap>;
/// A set of packages with their manifests parsed into memory, keyed by package name.
type PackageSet = BTreeMap<String, PackageEntry>;

#[derive(Default)]
pub struct ImageAssemblyConfigBuilder {
    /// The base packages from the AssemblyInputBundles
    base: PackageSet,

    /// The cache packages from the AssemblyInputBundles
    cache: PackageSet,

    /// The system packages from the AssemblyInputBundles
    system: PackageSet,

    /// The boot_args from the AssemblyInputBundles
    boot_args: BTreeSet<String>,

    /// The bootfs_files from the AssemblyInputBundles
    bootfs_files: FileEntryMap,

    /// The config_data entries, by package and by destination path.
    config_data: ConfigDataMap,

    // Modifications that must be made to structured config within packages.
    structured_config: StructuredConfigPatches,

    kernel_path: Option<PathBuf>,
    kernel_args: BTreeSet<String>,
    kernel_clock_backstop: Option<u64>,
}

impl ImageAssemblyConfigBuilder {
    /// Add an Assembly Input Bundle to the builder, via the path to its
    /// manifest.
    ///
    /// If any of the items it's trying to add are duplicates (either of itself
    /// or others, this will return an error.)
    pub fn add_bundle(&mut self, bundle_path: impl AsRef<Path>) -> Result<()> {
        let bundle = util::read_config(bundle_path.as_ref())?;

        // Strip filename from bundle path.
        let bundle_path = bundle_path.as_ref().parent().map(PathBuf::from).unwrap_or("".into());

        // Now add the parsed bundle
        self.add_parsed_bundle(bundle_path, bundle)
    }

    /// Add an Assembly Input Bundle to the builder, using a parsed
    /// AssemblyInputBundle, and the path to the folder that contains it.
    ///
    /// If any of the items it's trying to add are duplicates (either of itself
    /// or others, this will return an error.)
    pub fn add_parsed_bundle(
        &mut self,
        bundle_path: impl AsRef<Path>,
        bundle: AssemblyInputBundle,
    ) -> Result<()> {
        let bundle_path = bundle_path.as_ref();
        let AssemblyInputBundle { image_assembly: bundle, config_data } = bundle;

        let add_all_packages =
            |to_add_to: &mut PackageSet, subpaths, set_name| -> anyhow::Result<()> {
                for p in Self::paths_from(bundle_path, subpaths) {
                    let entry = PackageEntry::parse_from(p)
                        .context("parsing package entry from parsed bundle")?;
                    to_add_to.try_insert_unique(MapEntry(entry.name().to_owned(), entry)).map_err(
                        |e| anyhow!("duplicate {} package entry found: {:?}", set_name, e),
                    )?;
                }
                Ok(())
            };
        add_all_packages(&mut self.base, bundle.base, "base")?;
        add_all_packages(&mut self.cache, bundle.cache, "cache")?;
        add_all_packages(&mut self.system, bundle.system, "system")?;

        self.boot_args
            .try_insert_all_unique(bundle.boot_args)
            .map_err(|arg| anyhow!("duplicate boot_arg found: {}", arg))?;

        for FileEntry { destination, source } in
            Self::file_entry_paths_from(bundle_path, bundle.bootfs_files)
        {
            self.bootfs_files
                .try_insert_unique(MapEntry(destination, source))
                .map_err(|e| anyhow!("Duplicate bootfs destination path found: {}", e.key()))?;
        }

        if let Some(kernel) = bundle.kernel {
            assembly_util::set_option_once_or(
                &mut self.kernel_path,
                kernel.path.map(|p| bundle_path.join(p)),
                anyhow!("Only one input bundle can specify a kernel path"),
            )?;

            self.kernel_args
                .try_insert_all_unique(kernel.args)
                .map_err(|arg| anyhow!("duplicate kernel arg found: {}", arg))?;

            assembly_util::set_option_once_or(
                &mut self.kernel_clock_backstop,
                kernel.clock_backstop,
                anyhow!("Only one input bundle can specify a kernel clock backstop"),
            )?;
        }

        for (package, entries) in config_data {
            for entry in Self::file_entry_paths_from(bundle_path, entries) {
                self.add_config_data_entry(&package, entry)?;
            }
        }
        Ok(())
    }

    fn paths_from<'a, I>(base: &'a Path, paths: I) -> impl IntoIterator<Item = PathBuf> + 'a
    where
        I: IntoIterator<Item = PathBuf> + 'a,
    {
        paths.into_iter().map(move |p| base.join(p))
    }

    fn file_entry_paths_from(
        base: &Path,
        entries: impl IntoIterator<Item = FileEntry>,
    ) -> Vec<FileEntry> {
        entries
            .into_iter()
            .map(|entry| FileEntry {
                destination: entry.destination,
                source: base.join(entry.source),
            })
            .collect()
    }

    /// Add an entry to `config_data` for the given package.  If the entry
    /// duplicates an existing entry, return an error.
    fn add_config_data_entry(&mut self, package: impl AsRef<str>, entry: FileEntry) -> Result<()> {
        let package_entries = self.config_data.entry(package.as_ref().into()).or_default();
        let FileEntry { destination, source } = entry;
        package_entries.try_insert_unique(MapEntry(destination, source)).map_err(|e|
            anyhow!(
                "Duplicate config_data destination path found: {}, previous path was: {}, new path is: {}",
                e.key(),
                e.previous_value().display(),
                e.new_value().display()
            ))
    }

    /// Set the structured configuration updates for a package. Can only be called once per
    /// package.
    pub fn set_structured_config(
        &mut self,
        package: impl AsRef<str>,
        config: PackageConfigPatch,
    ) -> Result<()> {
        if self.structured_config.insert(package.as_ref().to_owned(), config).is_none() {
            Ok(())
        } else {
            Err(anyhow::format_err!("duplicate config patch"))
        }
    }

    /// Construct an ImageAssembly ImageAssemblyConfig from the collected items in the
    /// builder.
    ///
    /// If there are config_data entries, the config_data package will be
    /// created in the outdir, and it will be added to the returned
    /// ImageAssemblyConfig.
    ///
    /// If this cannot create a completed ImageAssemblyConfig, it will return an error
    /// instead.
    pub fn build(
        self,
        outdir: impl AsRef<Path>,
    ) -> Result<image_assembly_config::ImageAssemblyConfig> {
        let outdir = outdir.as_ref();
        // Decompose the fields in self, so that they can be recomposed into the generated
        // image assembly configuration.
        let Self {
            structured_config,
            mut base,
            mut cache,
            mut system,
            boot_args,
            bootfs_files,
            config_data,
            kernel_path,
            kernel_args,
            kernel_clock_backstop,
        } = self;

        // repackage any matching packages, ignoring whether we actually succeed. if a patch has
        // been provided that doesn't match a package, we silently skip it and let product
        // validation catch any issues
        for (package, config) in structured_config {
            // get the manifest for this package name, returning the set from which it was removed
            if let Some((manifest, source_package_set)) =
                remove_package_from_sets(&package, [&mut base, &mut cache, &mut system])
                    .with_context(|| format!("removing {} for repackaging", package))?
            {
                let outdir = outdir.join("repackaged").join(&package);
                let mut repackager = Repackager::new(manifest, &outdir)
                    .with_context(|| format!("reading existing manifest for {}", package))?;
                for (component, values) in &config.components {
                    repackager
                        .set_component_config(component, values.clone())
                        .with_context(|| format!("setting new config for {}", component))?;
                }
                let new_path = repackager
                    .build()
                    .with_context(|| format!("building repackaged {}", package))?;
                let new_entry = PackageEntry::parse_from(new_path)
                    .with_context(|| format!("parsing repackaged {}", package))?;
                source_package_set.insert(new_entry.name().to_owned(), new_entry);
            }
        }

        if !config_data.is_empty() {
            // Build the config_data package
            let mut config_data_builder = ConfigDataBuilder::default();
            for (package_name, entries) in config_data {
                for (destination, source) in entries {
                    config_data_builder.add_entry(&package_name, destination.into(), source)?;
                }
            }
            let manifest_path = config_data_builder
                .build(&outdir)
                .context("Writing the 'config_data' package metafar.")?;
            let entry = PackageEntry::parse_from(manifest_path)
                .context("parsing generated config-data package")?;
            base.try_insert_unique(MapEntry(entry.name().to_owned(), entry)).map_err(|p| {
                anyhow!(
                    "found a duplicate config_data package when adding generated package at: {}",
                    p.new_value().path.display()
                )
            })?;
        }

        // Construct a single "partial" config from the combined fields, and
        // then pass this to the ImageAssemblyConfig::try_from_partials() to get the
        // final validation that it's complete.
        let partial = image_assembly_config::PartialImageAssemblyConfig {
            system: system.into_iter().map(|(_, e)| e.path).collect(),
            base: base.into_iter().map(|(_, e)| e.path).collect(),
            cache: cache.into_iter().map(|(_, e)| e.path).collect(),
            kernel: Some(image_assembly_config::PartialKernelConfig {
                path: kernel_path,
                args: kernel_args.into_iter().collect(),
                clock_backstop: kernel_clock_backstop,
            }),
            boot_args: boot_args.into_iter().collect(),
            bootfs_files: bootfs_files
                .into_iter()
                .map(|(destination, source)| FileEntry { destination, source })
                .collect(),
        };

        let image_assembly_config = image_assembly_config::ImageAssemblyConfig::try_from_partials(
            std::iter::once(partial),
        )?;

        Ok(image_assembly_config)
    }
}

/// Remove a package with a matching name from the provided package sets, returning its parsed
/// manifest and a mutable reference to the set from which it was removed.
fn remove_package_from_sets<'a, 'b: 'a, const N: usize>(
    package_name: &str,
    package_sets: [&'a mut PackageSet; N],
) -> anyhow::Result<Option<(PackageManifest, &'a mut PackageSet)>> {
    let mut matches_name = None;

    for package_set in package_sets {
        if let Some(entry) = package_set.remove(package_name) {
            ensure!(
                matches_name.is_none(),
                "only one package with a given name is allowed per product"
            );
            matches_name = Some((entry.manifest, package_set));
        }
    }

    Ok(matches_name)
}

#[derive(Debug)]
struct PackageEntry {
    path: PathBuf,
    manifest: PackageManifest,
}

impl PackageEntry {
    fn parse_from(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_owned();
        let manifest = PackageManifest::try_load_from(&path)
            .context(format!("parsing {} as a package manifest", path.display()))?;
        Ok(Self { path, manifest })
    }

    fn name(&self) -> &str {
        self.manifest.name().as_ref()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_pkg::{PackageBuilder, PackageManifest};
    use std::fs::File;
    use tempfile::TempDir;

    fn make_test_assembly_bundle(bundle_path: &Path) -> AssemblyInputBundle {
        let write_empty_pkg = |path: &str| {
            let mut builder = PackageBuilder::new("test_pkg");
            builder.manifest_path(bundle_path.join(path));
            builder.build(&bundle_path, bundle_path.join(format!("{}_meta.far", path))).unwrap();
            PathBuf::from(path)
        };
        AssemblyInputBundle {
            image_assembly: image_assembly_config::PartialImageAssemblyConfig {
                base: vec![write_empty_pkg("base_package0")],
                system: vec![write_empty_pkg("sys_package0")],
                cache: vec![write_empty_pkg("cache_package0")],
                kernel: Some(image_assembly_config::PartialKernelConfig {
                    path: Some("kernel/path".into()),
                    args: vec!["kernel_arg0".into()],
                    clock_backstop: Some(56244),
                }),
                boot_args: vec!["boot_arg0".into()],
                bootfs_files: vec![FileEntry {
                    source: "source/path/to/file".into(),
                    destination: "dest/file/path".into(),
                }],
            },
            config_data: BTreeMap::default(),
        }
    }

    #[test]
    fn test_builder() {
        let outdir = TempDir::new().unwrap();
        let mut builder = ImageAssemblyConfigBuilder::default();
        builder.add_parsed_bundle(outdir.path(), make_test_assembly_bundle(outdir.path())).unwrap();
        let result: image_assembly_config::ImageAssemblyConfig = builder.build(&outdir).unwrap();

        assert_eq!(result.base, vec![outdir.path().join("base_package0")]);
        assert_eq!(result.cache, vec![outdir.path().join("cache_package0")]);
        assert_eq!(result.system, vec![outdir.path().join("sys_package0")]);

        assert_eq!(result.boot_args, vec!("boot_arg0".to_string()));
        assert_eq!(
            result.bootfs_files,
            vec!(FileEntry {
                source: outdir.path().join("source/path/to/file"),
                destination: "dest/file/path".into()
            })
        );

        assert_eq!(result.kernel.path, outdir.path().join("kernel/path"));
        assert_eq!(result.kernel.args, vec!("kernel_arg0".to_string()));
        assert_eq!(result.kernel.clock_backstop, 56244);
    }

    #[test]
    fn test_builder_with_config_data() {
        let outdir = TempDir::new().unwrap();
        let mut builder = ImageAssemblyConfigBuilder::default();

        // Write a file to the temp dir for use with config_data.
        let bundle_path = outdir.path().join("bundle");
        let config_data_target_package_name = "base_package0";
        let config_data_target_package_dir =
            bundle_path.join("config_data").join(config_data_target_package_name);
        let config_data_file_path = config_data_target_package_dir.join("config_data_source_file");
        std::fs::create_dir_all(&config_data_target_package_dir).unwrap();
        std::fs::write(&config_data_file_path, "configuration data").unwrap();

        // Create an assembly bundle and add a config_data entry to it.
        let mut bundle = make_test_assembly_bundle(&bundle_path);
        bundle.config_data.insert(
            config_data_target_package_name.to_string(),
            vec![FileEntry {
                source: config_data_file_path,
                destination: "dest/file/path".to_owned(),
            }],
        );

        builder.add_parsed_bundle(&bundle_path, bundle).unwrap();
        let result: image_assembly_config::ImageAssemblyConfig = builder.build(&outdir).unwrap();

        // config_data's manifest is in outdir
        let expected_config_data_manifest_path =
            outdir.path().join("config_data").join("package_manifest.json");

        // Validate that the base package set contains config_data.
        assert_eq!(result.base.len(), 2);
        assert!(result.base.contains(&bundle_path.join("base_package0")));
        assert!(result.base.contains(&expected_config_data_manifest_path));

        // Validate the contents of config_data is what is, expected by:
        // 1.  Reading in the package manifest to get the metafar path
        // 2.  Opening the metafar
        // 3.  Reading the config_data entry's file
        // 4.  Validate the contents of the file

        // 1. Read the config_data package manifest
        let config_data_manifest =
            PackageManifest::try_load_from(expected_config_data_manifest_path).unwrap();
        assert_eq!(config_data_manifest.name().as_ref(), "config-data");

        // and get the metafar path.
        let blobs = config_data_manifest.into_blobs();
        let metafar_blobinfo = blobs.get(0).unwrap();
        assert_eq!(metafar_blobinfo.path, "meta/");

        // 2. Read the metafar.
        let mut config_data_metafar = File::open(&metafar_blobinfo.source_path).unwrap();
        let mut far_reader = fuchsia_archive::Reader::new(&mut config_data_metafar).unwrap();

        // 3.  Read the configuration file.
        let config_file_data = far_reader
            .read_file(&format!("meta/data/{}/dest/file/path", config_data_target_package_name))
            .unwrap();

        // 4.  Validate its contents.
        assert_eq!(config_file_data, "configuration data".as_bytes());
    }

    /// Helper to duplicate the first item in an Vec<T: Clone> and make it also
    /// the last item. This intentionally panics if the Vec is empty.
    fn duplicate_first<T: Clone>(vec: &mut Vec<T>) {
        vec.push(vec.first().unwrap().clone());
    }

    #[test]
    fn test_builder_catches_dupe_base_pkgs_in_aib() {
        let temp = TempDir::new().unwrap();
        let mut aib = make_test_assembly_bundle(temp.path());
        duplicate_first(&mut aib.image_assembly.base);

        let mut builder = ImageAssemblyConfigBuilder::default();
        assert!(builder.add_parsed_bundle(temp.path(), aib).is_err());
    }

    #[test]
    fn test_builder_catches_dupe_cache_pkgs_in_aib() {
        let temp = TempDir::new().unwrap();
        let mut aib = make_test_assembly_bundle(temp.path());
        duplicate_first(&mut aib.image_assembly.cache);

        let mut builder = ImageAssemblyConfigBuilder::default();
        assert!(builder.add_parsed_bundle(temp.path(), aib).is_err());
    }

    #[test]
    fn test_builder_catches_dupe_system_pkgs_in_aib() {
        let temp = TempDir::new().unwrap();
        let mut aib = make_test_assembly_bundle(temp.path());
        duplicate_first(&mut aib.image_assembly.system);

        let mut builder = ImageAssemblyConfigBuilder::default();
        assert!(builder.add_parsed_bundle(temp.path(), aib).is_err());
    }

    fn test_duplicates_across_aibs_impl<
        T: Clone,
        F: Fn(&mut AssemblyInputBundle) -> &mut Vec<T>,
    >(
        accessor: F,
    ) {
        let outdir = TempDir::new().unwrap();
        let mut aib = make_test_assembly_bundle(outdir.path());
        let mut second_aib = AssemblyInputBundle::default();

        let first_list = (accessor)(&mut aib);
        let second_list = (accessor)(&mut second_aib);

        // Clone the first item in the first AIB into the same list in the
        // second AIB to create a duplicate item across the two AIBs.
        let value = first_list.get(0).unwrap();
        second_list.push(value.clone());

        let mut builder = ImageAssemblyConfigBuilder::default();
        builder.add_parsed_bundle(outdir.path(), aib).unwrap();
        assert!(builder.add_parsed_bundle(outdir.path().join("second"), second_aib).is_err());
    }

    #[test]
    #[ignore] // As packages from different bundles have different paths,
              // this isn't currently working
    fn test_builder_catches_dupe_base_pkgs_across_aibs() {
        test_duplicates_across_aibs_impl(|a| &mut a.image_assembly.base);
    }

    #[test]
    #[ignore] // As packages from different bundles have different paths,
              // this isn't currently working
    fn test_builder_catches_dupe_cache_pkgs_across_aibs() {
        test_duplicates_across_aibs_impl(|a| &mut a.image_assembly.cache);
    }

    #[test]
    #[ignore] // As packages from different bundles have different paths,
              // this isn't currently working
    fn test_builder_catches_dupe_system_pkgs_across_aibs() {
        test_duplicates_across_aibs_impl(|a| &mut a.image_assembly.system);
    }

    #[test]
    fn test_builder_catches_dupe_bootfs_files_across_aibs() {
        test_duplicates_across_aibs_impl(|a| &mut a.image_assembly.bootfs_files);
    }

    #[test]
    fn test_builder_catches_dupe_config_data_across_aibs() {
        let temp = TempDir::new().unwrap();
        let mut first_aib = make_test_assembly_bundle(temp.path());
        let mut second_aib = AssemblyInputBundle::default();

        let config_data_file_entry = FileEntry {
            source: "source/path/to/file".into(),
            destination: "dest/file/path".into(),
        };

        first_aib.config_data.insert("base_package0".into(), vec![config_data_file_entry.clone()]);
        second_aib.config_data.insert("base_package0".into(), vec![config_data_file_entry]);

        let mut builder = ImageAssemblyConfigBuilder::default();
        builder.add_parsed_bundle(temp.path(), first_aib).unwrap();
        assert!(builder.add_parsed_bundle(temp.path().join("second"), second_aib).is_err());
    }
}
