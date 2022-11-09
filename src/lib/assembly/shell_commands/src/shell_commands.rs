// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use assembly_config_schema::product_config::ShellCommands;
use assembly_package_utils::PackageInternalPathBuf;
use camino::{Utf8Path, Utf8PathBuf};
use fuchsia_pkg::PackageBuilder;
use std::collections::BTreeSet;

type RefToPackage<'a> = (&'a String, &'a BTreeSet<PackageInternalPathBuf>);

const SHELL_COMMANDS_PACKAGE_NAME: &str = "shell-commands";
const SHELL_COMMANDS_MANIFEST_FILE_NAME: &str = "package_manifest.json";

type ShellCommandsManifestPath = Utf8PathBuf;

/// A builder for the shell commands manifest package.
// #[derive(Default)]
pub struct ShellCommandsBuilder {
    shell_commands: ShellCommands,
    repository: String,
}

impl ShellCommandsBuilder {
    pub fn new() -> Self {
        Self { shell_commands: ShellCommands::default(), repository: String::default() }
    }

    /// Setup function, used to establish configuration data for the package builder.
    /// Sets attributes on the ShellCommandsBuilder and the PackageBuilder at self.package_builder
    pub fn add_shell_commands(&mut self, shell_commands: ShellCommands, repository: String) {
        self.shell_commands = shell_commands;
        self.repository = repository;
    }

    /// Builds the package, after the add_shell_commands function has been called to configure
    /// the builder instance
    pub fn build(self, out_dir: impl AsRef<Utf8Path>) -> Result<ShellCommandsManifestPath> {
        let mut package_builder = PackageBuilder::new(SHELL_COMMANDS_PACKAGE_NAME);
        let packages_dir = out_dir.as_ref().join(SHELL_COMMANDS_PACKAGE_NAME);
        let manifest_path = packages_dir.join(SHELL_COMMANDS_MANIFEST_FILE_NAME);
        package_builder.repository(&self.repository);
        package_builder.manifest_path(&manifest_path);
        self.write_trampolines(&mut package_builder, &packages_dir)?;
        package_builder
            .build(&packages_dir, &packages_dir.join("meta.far"))
            .context("Building the package manifest")?;

        Ok(manifest_path)
    }

    /// Iterates through self.shell_commands, writing each (package, binary) pair to a file
    fn write_trampolines(
        &self,
        package_builder: &mut PackageBuilder,
        packages_dir: &Utf8PathBuf,
    ) -> Result<()> {
        for package in &self.shell_commands {
            self.write_trampoline(package, package_builder, packages_dir)?;
        }
        Ok(())
    }

    /// Iterates through the list of binaries included with a package, creating a shell
    /// script for each (package, binary) pair
    fn write_trampoline(
        &self,
        package: RefToPackage<'_>,
        package_builder: &mut PackageBuilder,
        packages_dir: &Utf8PathBuf,
    ) -> Result<()> {
        let (package_name, binaries) = package;
        for binary_path in binaries.iter() {
            // Take just the file name from the full path,
            let shebang = format!(
                "#!resolve fuchsia-pkg://{repo}/{package_name}#{bin_path}\n",
                repo = &self.repository,
                package_name = package_name,
                bin_path = binary_path
            );

            let file_name = &binary_path
                .file_name()
                .ok_or(anyhow::anyhow!("Unable to acquire filename {}", &binary_path))?;
            let target_path = format!("bin/{}", &file_name.to_string());
            package_builder.add_contents_as_blob(
                &target_path,
                &shebang,
                &packages_dir.join(package_name),
            )?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::Result;
    use std::fs;
    use tempfile::TempDir;

    const BINARY1_PATH: &str = "bin/binary1";
    const BINARY2_PATH: &str = "very/long/path/binary2";

    pub fn make_test_shell_commands() -> ShellCommands {
        ShellCommands::from([(
            "package1".to_string(),
            BTreeSet::from([
                PackageInternalPathBuf::from(BINARY1_PATH),
                PackageInternalPathBuf::from(BINARY2_PATH),
            ]),
        )])
    }

    fn test_folder_structure(outdir: &Utf8PathBuf) -> Result<()> {
        let mut count = 0;
        for entry in fs::read_dir(&outdir.join(SHELL_COMMANDS_PACKAGE_NAME))? {
            count += 1;
            let file = entry.unwrap();
            let file_type = file.file_type().unwrap();
            let file_name = file.file_name().into_string().unwrap();
            match file_type.is_dir() {
                // Should contain 2 directories, one named meta, and one named package1
                true => {
                    match file_name.as_str() {
                        "package1" => test_bash_files(outdir, &file_name)?,
                        "pkgctl" => test_mismatch_case(&outdir)?,
                        _ => assert!(file_name.contains("meta")),
                    };
                }
                // Should contain 2 files, one named meta.far, one named package_manifest.json
                false => {
                    assert!(file_name.contains(".far") || file_name.contains(".json"));
                    if file_name.contains(".json") {
                        test_unmarshalled_package_manifest()?
                    }
                }
            };
        }
        assert_eq!(count, 4);
        Ok(())
    }

    fn test_mismatch_case(outdir: &Utf8PathBuf) -> Result<()> {
        let contents = fs::read_to_string(
            outdir.join(SHELL_COMMANDS_PACKAGE_NAME).join("pkgctl").join("bin"),
        )?;
        assert!(contents.contains("bin/multi"));
        Ok(())
    }

    fn test_bash_files(outdir: &Utf8PathBuf, package: &String) -> Result<()> {
        for entry in
            fs::read_dir(outdir.join(SHELL_COMMANDS_PACKAGE_NAME).join(package).join("bin"))?
        {
            let file_path = &entry.unwrap().path();
            let file_name = file_path.file_name();
            let contents = fs::read_to_string(&file_path)?;
            let base_string =
                |val| format!("#!resolve fuchsia-pkg://fuchsia.com/{}#{}", &package, val);

            match file_name.unwrap().to_str() {
                Some("binary1") => {
                    assert!(contents.contains(&base_string(BINARY1_PATH)))
                }
                Some("binary2") => {
                    assert!(contents.contains(&base_string(BINARY2_PATH)))
                }
                _ => panic!("This case shouldn't have happened"),
            }
        }
        Ok(())
    }

    fn test_unmarshalled_package_manifest() -> Result<()> {
        Ok(())
    }

    #[test]
    fn test_build() -> Result<()> {
        let mut builder = ShellCommandsBuilder::new();
        let outdir = TempDir::new().unwrap().into_path();
        let outdir_path = Utf8PathBuf::from_path_buf(outdir).unwrap();
        builder.add_shell_commands(make_test_shell_commands(), "fuchsia.com".to_string());
        builder.build(&outdir_path).unwrap();
        test_folder_structure(&outdir_path)
            .map_err(|_| anyhow::anyhow!("The folder structure is not as expected"))?;
        Ok(())
    }

    #[test]
    fn test_mismatch_target_path_source_path() -> Result<()> {
        let mut builder = ShellCommandsBuilder::new();
        let outdir = TempDir::new().unwrap().into_path();
        builder.add_shell_commands(
            ShellCommands::from([(
                "pkgctl".to_string(),
                BTreeSet::from([PackageInternalPathBuf::from("bin/multi_universal_tool")]),
            )]),
            "fuchsia.com".to_string(),
        );

        builder.build(Utf8PathBuf::from_path_buf(outdir).unwrap()).unwrap();
        Ok(())
    }

    #[test]
    fn test_add_shell_commands() -> Result<()> {
        let mut builder = ShellCommandsBuilder::new();
        assert_eq!(builder.shell_commands.len(), 0);
        builder.add_shell_commands(make_test_shell_commands(), "fuchsia.com".to_string());

        // Shell Commands successfully added
        assert_eq!(builder.shell_commands.len(), 1);
        // Shell Commands Builder assigned a repository
        assert_eq!(builder.repository, "fuchsia.com");

        Ok(())
    }
}
