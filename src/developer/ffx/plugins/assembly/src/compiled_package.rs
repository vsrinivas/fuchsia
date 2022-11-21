// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context, Result};
use assembly_components::ComponentBuilder;
use assembly_config_schema::product_config::{CompiledPackageDefinition, MainPackageDefinition};
use assembly_tool::Tool;
use camino::{Utf8Path, Utf8PathBuf};
use fuchsia_pkg::PackageBuilder;
use std::collections::BTreeMap;

#[derive(Debug, Default, PartialEq)]
pub struct CompiledPackageBuilder {
    name: String,
    component_shards: BTreeMap<String, Vec<Utf8PathBuf>>,
    main_definition: Option<MainPackageDefinition>,
}

/// Builds `CompiledPackageDefinition`s which are specified for Assembly
/// to create into packages.
impl CompiledPackageBuilder {
    pub fn new(name: impl Into<String>) -> CompiledPackageBuilder {
        CompiledPackageBuilder { name: name.into(), ..Default::default() }
    }

    pub fn add_package_def(&mut self, entry: &CompiledPackageDefinition) -> Result<&mut Self> {
        let name = entry.name();

        if name != self.name {
            bail!(
                "PackageEntry name '{name}' does not match CompiledPackageDefinition name '{}'",
                &self.name
            );
        }

        match entry {
            CompiledPackageDefinition::MainDefinition(def) => {
                if self.main_definition.is_some() {
                    bail!("Duplicate main definition for package {name}");
                }
                self.main_definition = Some(def.clone());
            }
            CompiledPackageDefinition::Additional(def) => {
                for (component_name, mut component_shards) in def.component_shards.clone() {
                    self.component_shards
                        .entry(component_name.clone())
                        .or_default()
                        .append(&mut component_shards);
                }
            }
        }

        Ok(self)
    }

    fn validate(&self) -> Result<()> {
        let main_definition = self
            .main_definition
            .as_ref()
            .with_context(|| format!("main definition for package '{}' not found", &self.name))?
            .clone();

        for name in self.component_shards.keys() {
            if !main_definition.components.contains_key(name) {
                bail!(
                    "component '{}' for package '{}' does not have a MainDefinition in any AIB",
                    name,
                    &self.name
                );
            }
        }

        Ok(())
    }

    /// Build the components for the package and the package itself.
    ///
    /// Returns a path to the package manifest
    pub fn build(self, cmc_tool: &dyn Tool, outdir: impl AsRef<Utf8Path>) -> Result<Utf8PathBuf> {
        self.validate()?;
        let outdir = outdir.as_ref().join(&self.name);

        let main_definition = self.main_definition.as_ref().context("no main definition")?;

        let mut package_builder = PackageBuilder::new(&self.name);

        for (component_name, cml) in &main_definition.components {
            let mut component_builder = ComponentBuilder::new(component_name);
            component_builder.add_shard(cml).with_context(|| {
                format!("Adding cml for component: '{component_name}' to package: '{}'", &self.name)
            })?;

            let default = Vec::new();
            let cml_shards = self.component_shards.get(component_name).unwrap_or(&default);

            for cml_shard in cml_shards {
                component_builder.add_shard(cml_shard.as_path()).with_context(|| {
                    format!("Adding shard for: '{component_name}' to package '{}'", &self.name)
                })?;
            }

            let component_manifest_path = component_builder
                .build(&outdir, cmc_tool)
                .with_context(|| format!("building component {}", component_name))?;
            let component_manifest_file_name =
                component_manifest_path.file_name().context("component file name")?;

            package_builder.add_file_to_far(
                format!("meta/{component_manifest_file_name}"),
                component_manifest_path,
            )?;
        }

        for entry in &main_definition.contents {
            package_builder.add_file_as_blob(&entry.destination, &entry.source)?;
        }

        let package_manifest_path = outdir.join("package_manifest.json");
        package_builder.manifest_path(&package_manifest_path);
        let metafar_path = outdir.join(format!("{}.far", &self.name));
        package_builder.build(&outdir, metafar_path).context("building package")?;

        Ok(package_manifest_path)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assembly_config_schema::{product_config::AdditionalPackageContents, FileEntry};
    use assembly_tool::testing::FakeToolProvider;
    use assembly_tool::ToolProvider;
    use fuchsia_archive::Utf8Reader;
    use fuchsia_pkg::PackageManifest;
    use std::fs::File;
    use tempfile::TempDir;

    #[test]
    fn add_package_def_appends_entries_to_builder() {
        let mut compiled_package_builder = CompiledPackageBuilder::new("foo");
        let outdir_tmp = TempDir::new().unwrap();
        let outdir = Utf8Path::from_path(outdir_tmp.path()).unwrap();
        make_test_package_and_components(outdir);

        compiled_package_builder
            .add_package_def(&CompiledPackageDefinition::MainDefinition(MainPackageDefinition {
                name: "foo".into(),
                components: BTreeMap::from([
                    ("component1".into(), "cml1".into()),
                    ("component2".into(), "cml2".into()),
                ]),
                contents: vec![FileEntry {
                    source: outdir.join("file1"),
                    destination: "file1".into(),
                }],
            }))
            .unwrap()
            .add_package_def(&CompiledPackageDefinition::Additional(AdditionalPackageContents {
                name: "foo".into(),
                component_shards: BTreeMap::from([("component2".into(), vec!["shard1".into()])]),
            }))
            .unwrap();

        assert!(compiled_package_builder.main_definition.is_some());
        assert_eq!(
            compiled_package_builder,
            CompiledPackageBuilder {
                name: "foo".into(),
                component_shards: BTreeMap::from([("component2".into(), vec!["shard1".into()])]),
                main_definition: Some(MainPackageDefinition {
                    name: "foo".into(),
                    components: BTreeMap::from([
                        ("component1".into(), "cml1".into()),
                        ("component2".into(), "cml2".into()),
                    ]),
                    contents: vec![FileEntry {
                        source: outdir.join("file1"),
                        destination: "file1".into()
                    }],
                })
            }
        );
    }

    #[test]
    fn build_builds_package() {
        let mut compiled_package_builder = CompiledPackageBuilder::new("foo");
        let tools = FakeToolProvider::default();
        let outdir_tmp = TempDir::new().unwrap();
        let outdir = Utf8Path::from_path(outdir_tmp.path()).unwrap();
        make_test_package_and_components(outdir);

        compiled_package_builder
            .add_package_def(&CompiledPackageDefinition::MainDefinition(MainPackageDefinition {
                name: "foo".into(),
                components: BTreeMap::from([
                    ("component1".into(), "cml1".into()),
                    ("component2".into(), "cml2".into()),
                ]),
                contents: vec![FileEntry {
                    source: outdir.join("file1"),
                    destination: "file1".into(),
                }],
            }))
            .unwrap()
            .add_package_def(&CompiledPackageDefinition::Additional(AdditionalPackageContents {
                name: "foo".into(),
                component_shards: BTreeMap::from([("component2".into(), vec!["shard1".into()])]),
            }))
            .unwrap();

        compiled_package_builder.build(tools.get_tool("cmc").unwrap().as_ref(), outdir).unwrap();

        let compiled_package_file = File::open(outdir.join("foo/foo.far")).unwrap();
        let mut far_reader = Utf8Reader::new(&compiled_package_file).unwrap();
        let manifest_path = outdir.join("foo/package_manifest.json");
        assert_far_contents_eq(
            &mut far_reader,
            "meta/contents",
            "file1=b5209759e76a8343c45b8c7abad13a1f0609512865ee7f7f5533212d8ab334dc\n",
        );
        assert_far_contents_eq(&mut far_reader, "meta/component1.cm", "component fake contents");
        let package_manifest = PackageManifest::try_load_from(manifest_path).unwrap();
        assert_eq!(package_manifest.name().as_ref(), "foo");
    }

    #[test]
    fn add_package_def_with_wrong_name_returns_err() {
        let mut compiled_package_builder = CompiledPackageBuilder::new("bar");

        let result = compiled_package_builder.add_package_def(
            &CompiledPackageDefinition::MainDefinition(MainPackageDefinition {
                name: "foo".into(),
                components: BTreeMap::new(),
                contents: Vec::new(),
            }),
        );

        assert!(result.is_err());
    }

    #[test]
    fn validate_without_main_definition_returns_err() {
        let mut compiled_package_builder = CompiledPackageBuilder::new("foo");
        compiled_package_builder
            .add_package_def(&CompiledPackageDefinition::Additional(AdditionalPackageContents {
                name: "foo".into(),
                component_shards: BTreeMap::from([("component2".into(), vec!["shard1".into()])]),
            }))
            .unwrap();

        let result = compiled_package_builder.validate();

        assert!(result.is_err());
    }

    #[test]
    fn add_package_def_with_duplicate_main_definition_returns_err() {
        let mut compiled_package_builder = CompiledPackageBuilder::new("foo");
        compiled_package_builder
            .add_package_def(&CompiledPackageDefinition::MainDefinition(MainPackageDefinition {
                name: "foo".into(),
                ..Default::default()
            }))
            .unwrap();

        let result =
            compiled_package_builder.add_package_def(&CompiledPackageDefinition::MainDefinition(
                MainPackageDefinition { name: "foo".into(), ..Default::default() },
            ));

        assert!(result.is_err());
    }

    fn assert_far_contents_eq(
        far_reader: &mut Utf8Reader<&File>,
        path: &str,
        expected_contents: &str,
    ) {
        let contents = far_reader.read_file(path).unwrap();
        let contents = std::str::from_utf8(&contents).unwrap();
        assert_eq!(contents, expected_contents);
    }

    fn make_test_package_and_components(outdir: &Utf8Path) {
        // We're mocking the component compiler but we are
        // using the real packaging library.
        // Write the contents of the package.
        let file1 = outdir.join("file1");
        std::fs::write(file1, "file1 contents").unwrap();
        // Write the expected output component files since the component
        // compiler is mocked.
        let component1_dir = outdir.join("foo/component1");
        let component2_dir = outdir.join("foo/component2");
        std::fs::create_dir_all(&component1_dir).unwrap();
        std::fs::create_dir_all(&component2_dir).unwrap();
        std::fs::write(component1_dir.join("component1.cm"), "component fake contents").unwrap();
        std::fs::write(component2_dir.join("component2.cm"), "component fake contents").unwrap();
    }
}
