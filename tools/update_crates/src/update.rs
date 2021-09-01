// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Context};
use argh::FromArgs;
use serde::Deserialize;
use std::{
    collections::BTreeSet,
    env, iter,
    path::{Path, PathBuf},
    str::FromStr,
};
use toml_edit::{decorated, Document, Item, Value};

/// update outdated crates in the provided manifest
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "update")]
pub struct UpdateOptions {
    /// path to the cargo config file to suppress when running cargo-outdated
    #[argh(option)]
    config_path: Option<PathBuf>,

    /// path to cargo binary
    #[argh(option)]
    cargo: PathBuf,

    /// path to the directory containing the cargo-outdated binary
    #[argh(option)]
    outdated_dir: PathBuf,

    /// tells cargo-outdated not to use the network, only resolving against local files
    #[argh(switch)]
    offline: bool,
}

pub fn update_crates(
    overrides: PathBuf,
    manifest_path: PathBuf,
    UpdateOptions { outdated_dir, cargo, config_path, offline }: UpdateOptions,
) -> anyhow::Result<()> {
    let mut cargo_toml = toml_edit::Document::from_str(
        &std::fs::read_to_string(&manifest_path).context("reading cargo.toml")?,
    )
    .context("parsing cargo.toml")?;

    let overrides =
        toml::from_str(&std::fs::read_to_string(&overrides).context("reading overrides config")?)
            .context("parsing overrides config")?;

    let updates =
        crates_to_update(&cargo, &outdated_dir, config_path, &manifest_path, overrides, offline)
            .context("getting list of crates to update")?;

    for to_update in updates {
        eprintln!("Updating {:?}", to_update);
        set_dep_version_to(to_update.get_dep_spec(&mut cargo_toml), &to_update.latest);
    }

    let updated_contents = cargo_toml.to_string_in_original_order();
    std::fs::write(&manifest_path, updated_contents).context("writing updated contents")?;

    Ok(())
}

fn crates_to_update(
    cargo: &Path,
    outdated_dir: &Path,
    config_path: Option<PathBuf>,
    manifest_path: &Path,
    OverrideConfig { skip_updating }: OverrideConfig,
    offline: bool,
) -> anyhow::Result<Vec<OutdatedCrate>> {
    let outdated_output = run_cargo_outdated_raw(
        cargo,
        outdated_dir,
        config_path,
        manifest_path,
        &skip_updating,
        offline,
    )
    .context("running cargo-outdated command for its stdout")?;

    let OutdatedOutput { dependencies: mut crates_to_update } =
        serde_json::from_slice(&outdated_output).context("parsing cargo-outdated output")?;
    crates_to_update.iter_mut().for_each(OutdatedCrate::fixup_name);
    crates_to_update
        .retain(|krate| krate.latest != "Removed" && !skip_updating.contains(&krate.name));

    Ok(crates_to_update)
}

fn run_cargo_outdated_raw(
    cargo: &Path,
    outdated_dir: &Path,
    config_path: Option<PathBuf>,
    manifest_path: &Path,
    skip_updating: &BTreeSet<String>,
    offline: bool,
) -> anyhow::Result<Vec<u8>> {
    // move the cargo config to a new spot, move it back when done
    if let Some(config_path) = &config_path {
        std::fs::rename(config_path, config_path.with_extension("ignore"))
            .context("renaming cargo config")?;
    }
    scopeguard::defer! {
        if let Some(config_path) = &config_path {
            std::fs::rename(config_path.with_extension("ignore"), config_path)
                .expect("couldn't restore cargo config, repo may be inconsistent");
        }
    };

    let mut excluded = String::new();
    for (i, krate) in skip_updating.iter().enumerate() {
        if i > 0 {
            excluded.push(',');
        }
        excluded.push_str(&krate);
    }

    let path_with_outdated = env::join_paths(
        iter::once(outdated_dir.to_path_buf()).chain(env::split_paths(&env::var("PATH").unwrap())),
    )
    .unwrap();
    let mut cmd = std::process::Command::new(cargo);
    cmd.arg("outdated")
        // TODO(fxbug.dev/80080) remove this flag when we start updating transitive deps
        .arg("--root-deps-only") // only return things in Cargo.toml explicitly
        .arg("--verbose")
        .arg("--manifest-path")
        .arg(manifest_path)
        .arg("--format")
        .arg("json")
        .env("PATH", path_with_outdated)
        .current_dir(manifest_path.parent().unwrap());

    if !excluded.is_empty() {
        cmd.arg("--exclude").arg(excluded);
    }

    if offline {
        cmd.arg("--offline");
    }

    let output = cmd.output().context("executing cargo outdated")?;

    if !output.status.success() {
        bail!(
            "cargo-outdated failed ({:?}): \n{}\n{}",
            output.status,
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr),
        );
    }

    Ok(output.stdout)
}

/// serde type for override configuration
#[derive(Debug, Deserialize)]
struct OverrideConfig {
    /// list of crates to skip
    skip_updating: BTreeSet<String>,
}

/// outer serde type for extracting what we want from cargo-outdated
#[derive(Debug, Deserialize)]
struct OutdatedOutput {
    dependencies: Vec<OutdatedCrate>,
}

#[derive(Debug, Deserialize)]
struct OutdatedCrate {
    /// name of the crate
    name: String,
    /// the version we want to update to
    latest: String,
    /// platform cfg's, used for matching toml fields
    platform: Option<String>,
    // project: String,
    // compat: String,
    // kind: String,
}

impl OutdatedCrate {
    /// cargo-outdated gives us names in a format like `parent_crate->middle->actual_dep` where we
    /// want `actual_dep` to be the name we use for looking everything up in all the metadata.
    fn fixup_name(&mut self) {
        self.name = self.name.split("->").last().unwrap().to_owned();
    }

    /// Finds the version string for `self` within `cargo_toml`.
    fn get_dep_spec<'d>(&self, cargo_toml: &'d mut Document) -> &'d mut Value {
        let dependencies = if let Some(platform) = &self.platform {
            // cargo-outdated thinks this dep is in a table like `[target.'cfg(...)'.dependencies]`
            if let Some(plat) = cargo_toml["target"][platform].as_table_mut() {
                &mut plat["dependencies"]
            } else {
                panic!("cargo-outdated gave us a dep to update that's not in Cargo.toml");
            }
        } else {
            // cargo-outdated thinks this is in an unqualified block of `[dependencies]`
            &mut cargo_toml["dependencies"]
        }
        .as_table_mut()
        .expect("all deps from cargo-outdated must be present in Cargo.toml");

        match &mut dependencies[&self.name] {
            Item::Value(v) => v, // the version specifier is the only value for this key, return it
            Item::Table(t) => {
                // self is a table like `[dependencies.foo]` and has its own version key
                t["version"].as_value_mut().expect("valid dep tables must have version keys")
            }
            Item::None => panic!("all deps from cargo-outdated must be present in Cargo.toml"),
            Item::ArrayOfTables(_) => {
                unreachable!("not valid to have array of tables for dep specs")
            }
        }
    }
}

fn set_dep_version_to(spec: &mut Value, to: &str) {
    if let Some(table) = spec.as_inline_table_mut() {
        let mut version =
            table.get_mut("version").expect("dep spec tables must have a version key");
        // recursing here makes us more permissive than cargo because we'd be ok with
        // foo = { { { version: "..." }, features: [...] }}
        set_dep_version_to(&mut version, to);
    } else {
        assert!(spec.is_str(), "Dependency specs must be strings or tables.");
        // just a plain swap is fine here, there are no features to preserve
        *spec = decorated(Value::from(to.to_owned()), spec.decor().prefix(), spec.decor().suffix());
    }
}
