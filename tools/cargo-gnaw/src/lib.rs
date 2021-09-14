// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    crate::{build::BuildScript, graph::GnBuildGraph, target::GnTarget, types::*},
    anyhow::{Context, Error},
    argh::FromArgs,
    cargo_metadata::DependencyKind,
    serde_derive::{Deserialize, Serialize},
    std::collections::{BTreeMap, HashMap, HashSet},
    std::{
        fs::File,
        io::{self, Read, Write},
        path::{Path, PathBuf},
        process::Command,
    },
};

mod build;
mod cfg;
mod gn;
mod graph;
mod target;
mod types;

#[derive(FromArgs, Debug)]
/// Generate a GN manifest for your vendored Cargo dependencies.
struct Opt {
    /// cargo manifest path
    #[argh(option)]
    manifest_path: PathBuf,

    /// root of GN project
    #[argh(option)]
    project_root: PathBuf,

    /// cargo binary to use (for vendored toolchains)
    #[argh(option)]
    cargo: Option<PathBuf>,

    /// already generated configs from cargo build scripts
    #[argh(option, short = 'p')]
    #[allow(unused)]
    cargo_configs: Option<PathBuf>,

    /// location of GN file
    #[argh(option, short = 'o')]
    output: PathBuf,

    /// location of JSON file with crate metadata
    #[argh(option)]
    emit_metadata: Option<PathBuf>,

    /// location of GN binary to use for formating.
    /// If no path is provided, no format will be run.
    #[argh(option)]
    gn_bin: Option<PathBuf>,

    /// don't generate a target for the root crate
    #[argh(switch)]
    skip_root: bool,
}

type PackageName = String;
type TargetName = String;
type Version = String;

/// Per-target metadata in the Cargo.toml for Rust crates that
/// require extra information to in the BUILD.gn
#[derive(Default, Clone, Serialize, Deserialize, Debug)]
pub struct TargetCfg {
    /// Config flags for rustc. Ex: --cfg=std
    rustflags: Option<Vec<String>>,
    /// Environment variables. These are usually from Cargo or the
    /// build.rs file in the crate.
    env_vars: Option<Vec<String>>,
    /// GN Targets that this crate should depend on. Generally for
    /// crates that build C libraries and link against.
    deps: Option<Vec<String>>,
    /// GN Configs that this crate should depend on.  Used to add
    /// crate-specific configs.
    configs: Option<Vec<String>>,
}

/// Configuration for a single GN executable target to generate from a Cargo binary target.
#[derive(Clone, Serialize, Deserialize, Debug)]
pub struct BinaryCfg {
    /// Name to use as both the top-level GN group target and the executable's output name.
    output_name: String,
    /// Binary target configuration for all platforms.
    #[serde(default, flatten)]
    default_cfg: TargetCfg,
    /// Per-platform binary target configuration.
    #[serde(default)]
    #[serde(rename = "platform")]
    platform_cfg: HashMap<Platform, TargetCfg>,
}

// Configuration for a Cargo package. Contains configuration for its (single) library target at the
// top level and optionally zero or more binaries to generate.
#[derive(Default, Clone, Serialize, Deserialize, Debug)]
#[serde(default)]
pub struct PackageCfg {
    /// Library target configuration for all platforms.
    #[serde(flatten)]
    default_cfg: TargetCfg,
    /// Per-platform library target configuration.
    #[serde(rename = "platform")]
    platform_cfg: HashMap<Platform, TargetCfg>,
    /// Configuration for GN binary targets to generate from one of the package's binary targets.
    /// The map key identifies the cargo target name within this cargo package.
    binary: HashMap<TargetName, BinaryCfg>,
}

/// Configs added to all GN targets in the BUILD.gn
#[derive(Serialize, Deserialize, Debug)]
pub struct GlobalTargetCfgs {
    remove_cfgs: Vec<String>,
    add_cfgs: Vec<String>,
}

/// Extra metadata in the Cargo.toml file that feeds into the
/// BUILD.gn file.
#[derive(Serialize, Deserialize, Debug)]
struct GnBuildMetadata {
    /// global configs
    config: Option<GlobalTargetCfgs>,
    /// map of per-Cargo package configuration
    package: HashMap<PackageName, HashMap<Version, PackageCfg>>,
}

#[derive(Serialize, Deserialize, Debug)]
struct BuildMetadata {
    gn: Option<GnBuildMetadata>,
}

/// Used for identifying 3p owners via reverse dependencies. Ties together several pieces of
/// metadata needed to associate a GN target with an OWNERS file and vice versa.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, PartialOrd, Ord, Serialize)]
pub struct CrateOutputMetadata {
    /// Name of the crate as specified in Cargo.toml.
    pub name: String,

    /// Version of the crate, used for disambiguating between duplicated transitive deps.
    pub version: String,

    /// Full GN target for depending on the crate.
    ///
    /// For example, Rust targets all have a canonical target like
    /// `//third_party/rust_crates:foo-v1_0_0`.
    pub canonical_target: String,

    /// Shorthand GN target for depending on the crate.
    ///
    /// For example, Rust targets listed in `third_party/rust_crates/Cargo.toml` have a
    /// shortcut target like `//third_party/rust_crates:foo`.
    pub shortcut_target: Option<String>,

    /// Filesystem path to the directory containing `Cargo.toml`.
    pub path: PathBuf,
}

// Use BTreeMap so that iteration over platforms is stable.
type CombinedTargetCfg<'a> = BTreeMap<Option<&'a Platform>, &'a TargetCfg>;

macro_rules! define_combined_cfg {
    ($t:ty) => {
        impl $t {
            fn combined_target_cfg(&self) -> CombinedTargetCfg<'_> {
                let mut combined: CombinedTargetCfg<'_> =
                    self.platform_cfg.iter().map(|(k, v)| (Some(k), v)).collect();
                assert!(
                    combined.insert(None, &self.default_cfg).is_none(),
                    "Default platform (None) already present in combined cfg"
                );
                combined
            }
        }
    };
}
define_combined_cfg!(PackageCfg);
define_combined_cfg!(BinaryCfg);

pub fn generate_from_manifest<W: io::Write>(
    mut output: &mut W,
    output_path: &Path,
    metadata_path: Option<PathBuf>,
    manifest_path: PathBuf,
    project_root: PathBuf,
    cargo: Option<PathBuf>,
    skip_root: bool,
) -> Result<(), Error> {
    let path_from_root_to_generated = output_path
        .parent()
        .unwrap()
        .strip_prefix(&project_root)
        .expect("--project-root must be a parent of --output");
    let mut emitted_metadata: Vec<CrateOutputMetadata> = Vec::new();
    let mut top_level_metadata: HashSet<String> = HashSet::new();

    // generate cargo metadata
    let mut cmd = cargo_metadata::MetadataCommand::new();
    let parent_dir = manifest_path
        .parent()
        .with_context(|| format!("while parsing parent path: {}", manifest_path.display()))?;
    cmd.current_dir(parent_dir);
    cmd.manifest_path(&manifest_path);
    if let Some(ref cargo_path) = cargo {
        cmd.cargo_path(&cargo_path);
    }
    cmd.other_options([String::from("--frozen")]);
    let metadata = cmd.exec().with_context(|| {
        format!("while running cargo metadata: supplied cargo binary: {:?}", &cargo)
    })?;

    // read out custom gn commands from the toml file
    let mut file = File::open(&manifest_path)
        .with_context(|| format!("opening {}", manifest_path.display()))?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)
        .with_context(|| format!("while reading manifest: {}", manifest_path.display()))?;
    let metadata_configs: BuildMetadata =
        toml::from_str(&contents).context("parsing manifest toml")?;

    gn::write_header(&mut output, &manifest_path).context("writing header")?;

    // Construct a build graph of all the targets for GN
    let mut build_graph = GnBuildGraph::new(&metadata);
    match metadata.resolve.as_ref() {
        Some(resolve) => {
            let top_level_id = resolve.root.as_ref().unwrap();
            if skip_root {
                let top_level_node = resolve
                    .nodes
                    .iter()
                    .find(|node| node.id == *top_level_id)
                    .expect("top level node not in node graph");
                for dep in &top_level_node.deps {
                    build_graph
                        .add_cargo_package(dep.pkg.clone())
                        .context("adding cargo package")?;
                    for kinds in dep.dep_kinds.iter() {
                        if kinds.kind == DependencyKind::Normal {
                            let platform = kinds.target.as_ref().map(|t| format!("{}", t));
                            let package = &metadata[&dep.pkg];
                            top_level_metadata.insert(package.name.to_owned());
                            gn::write_top_level_rule(&mut output, platform, package)
                                .with_context(|| {
                                    format!(
                                        "while writing top level rule for package: {}",
                                        &dep.pkg
                                    )
                                })
                                .context("writing top level rule")?;
                        }
                    }
                }
            } else {
                build_graph
                    .add_cargo_package(top_level_id.clone())
                    .with_context(|| "could not add cargo package")?;
                let package = &metadata[&top_level_id];
                top_level_metadata.insert(package.name.to_owned());
                gn::write_top_level_rule(&mut output, None, package)
                    .with_context(|| "writing top level rule")?;
            }
        }
        None => anyhow::bail!("Failed to resolve a build graph for the package tree"),
    }

    // Sort targets for stable output to minimize diff churn
    let mut graph_targets: Vec<&GnTarget<'_>> = build_graph.targets().collect();
    graph_targets.sort();

    let global_config = match metadata_configs.gn {
        Some(ref gn_configs) => gn_configs.config.as_ref(),
        None => None,
    };

    // Grab the per-package configs.
    let gn_pkg_cfgs = metadata_configs.gn.as_ref().map(|i| &i.package);

    // Iterate through the target configs, verifying that the build graph contains the configured
    // targets, then save off a mapping of GnTarget to the target config.
    let mut target_cfgs = HashMap::<&GnTarget<'_>, CombinedTargetCfg<'_>>::new();
    let mut binary_names = HashMap::<&GnTarget<'_>, &str>::new();
    let mut unused_configs = String::new();
    if let Some(gn_pkg_cfgs) = gn_pkg_cfgs {
        for (pkg_name, versions) in gn_pkg_cfgs {
            for (pkg_version, pkg_cfg) in versions {
                // Search the build graph for the library target.
                if let Some(target) = build_graph.find_library_target(pkg_name, pkg_version) {
                    assert!(
                        target_cfgs.insert(target, pkg_cfg.combined_target_cfg()).is_none(),
                        "Duplicate library config somehow specified"
                    );
                } else {
                    unused_configs.push_str(&format!(
                        "library crate, package {} version {}\n",
                        pkg_name, pkg_version
                    ));
                }

                // Handle binaries that should be built for this package, similarly searching the
                // build graph for the binary targets.
                for (bin_cargo_target, bin_cfg) in &pkg_cfg.binary {
                    if let Some(target) =
                        build_graph.find_binary_target(pkg_name, pkg_version, bin_cargo_target)
                    {
                        if let Some(old_name) = binary_names.insert(target, &bin_cfg.output_name) {
                            anyhow::bail!(
                                "A given binary target ({} in package {} version {}) can only be \
                                used for a single GN target, but multiple exist, including {} \
                                and {}",
                                bin_cargo_target,
                                pkg_name,
                                pkg_version,
                                &bin_cfg.output_name,
                                old_name
                            );
                        }
                        assert!(
                            target_cfgs.insert(target, bin_cfg.combined_target_cfg()).is_none(),
                            "Should have bailed above"
                        );
                    } else {
                        unused_configs.push_str(&format!(
                            "binary crate {}, package {} version {}\n",
                            bin_cargo_target, pkg_name, pkg_version
                        ));
                    }
                }
            }
        }
    }
    if unused_configs.len() > 0 {
        anyhow::bail!(
            "GNaw config exists for crates that were not found in the Cargo build graph:\n\n{}",
            unused_configs
        );
    }

    // Write the top-level GN rules for binaries. Verify that the names are unique, otherwise a
    // build failure will result.
    {
        let mut names = HashSet::new();
        for (target, bin_name) in &binary_names {
            if !names.insert(bin_name) {
                anyhow::bail!(
                    "Multiple targets are configured to generate executables named \"{}\"",
                    bin_name
                );
            }

            emitted_metadata.push(CrateOutputMetadata {
                name: bin_name.to_string(),
                version: target.version(),
                canonical_target: format!(
                    "//{}:{}",
                    path_from_root_to_generated.display(),
                    target.gn_target_name()
                ),
                shortcut_target: Some(format!(
                    "//{}:{}",
                    path_from_root_to_generated.display(),
                    bin_name
                )),
                path: target.package_root(&project_root),
            });
            gn::write_binary_top_level_rule(&mut output, None, bin_name, target)
                .context("writing binary top level rule")?;
        }
    }

    // Write out a GN rule for each target in the build graph
    for target in graph_targets {
        // Check whether we should generate a target if this is a binary.
        let binary_name = if let GnRustType::Binary = target.target_type {
            let name = binary_names.get(target).map(|s| *s);
            if name.is_none() {
                continue;
            }
            name
        } else {
            None
        };

        let target_cfg = target_cfgs.get(target);
        if target.uses_build_script() && target_cfg.is_none() {
            let build_output = BuildScript::compile(target).and_then(|s| s.execute());
            match build_output {
                Ok(rules) => {
                    anyhow::bail!(
                        "Add this to your Cargo.toml located at {}:\n\
                        [gn.package.{}.\"{}\"]\n\
                        rustflags = [{}]",
                        manifest_path.display(),
                        target.name(),
                        target.version(),
                        rules.cfgs.join(", ")
                    );
                }
                Err(err) => anyhow::bail!(
                    "{} {} uses a build script but no section defined in the GN section \
                    nor can we automatically generate the appropriate rules:\n{}",
                    target.name(),
                    target.version(),
                    err,
                ),
            }
        }

        let package_root = target.package_root(&project_root);
        let shortcut_target = if top_level_metadata.contains(target.pkg_name) {
            Some(format!("//{}:{}", path_from_root_to_generated.display(), target.pkg_name))
        } else {
            None
        };

        emitted_metadata.push(CrateOutputMetadata {
            name: target.name(),
            version: target.version(),
            canonical_target: format!(
                "//{}:{}",
                path_from_root_to_generated.display(),
                target.gn_target_name()
            ),
            shortcut_target,
            path: package_root.to_owned(),
        });

        let _ = gn::write_rule(
            &mut output,
            &target,
            &project_root,
            global_config,
            target_cfg,
            binary_name,
        )
        .context("writing rule")?;
    }

    if let Some(metadata_path) = metadata_path {
        eprintln!("Emitting external crate metadata: {}", metadata_path.display());
        emitted_metadata.sort();
        let metadata_json = serde_json::to_string_pretty(&emitted_metadata)
            .context("serializing metadata to json")?;
        std::fs::write(metadata_path, &metadata_json).context("writing metadata file")?;
    }

    Ok(())
}

pub fn run(args: &[impl AsRef<str>]) -> Result<(), Error> {
    // Check if running through cargo or stand-alone before arg parsing
    let mut strs: Vec<&str> = args.iter().map(|s| s.as_ref()).collect();
    if strs.get(1) == Some(&"gnaw") {
        // If the second command is "gnaw" this likely invoked by `cargo gnaw`
        // shift all args by one.
        strs = strs[1..].to_vec()
    }
    let opt = Opt::from_args(&[strs[0]], &strs[1..])
        .map_err(|early_exit| anyhow::anyhow!("early exit: {:?}", &early_exit))
        .with_context(|| "while parsing command line arguments")?;

    eprintln!("Generating GN file from {}", opt.manifest_path.to_string_lossy());

    // redirect to stdout if no GN output file specified
    // Stores data in a buffer in-case to prevent creating bad BUILD.gn
    let mut gn_output_buffer = vec![];
    generate_from_manifest(
        &mut gn_output_buffer,
        &opt.output,
        opt.emit_metadata,
        opt.manifest_path,
        opt.project_root,
        opt.cargo,
        opt.skip_root,
    )
    .context("generating manifest")?;

    // Write the file buffer to an actual file
    File::create(&opt.output)
        .context("creating output file")?
        .write_all(&gn_output_buffer)
        .context("writing output contents")?;

    if let Some(gn_bin) = opt.gn_bin {
        eprintln!("Formatting output file: {}", opt.output.display());
        Command::new(&gn_bin)
            .arg("format")
            .arg(opt.output)
            .output()
            .with_context(|| format!("failed to run GN format command: {}", gn_bin.display()))?;
    }

    Ok(())
}
