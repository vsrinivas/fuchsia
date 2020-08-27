// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    crate::{build::BuildScript, graph::GnBuildGraph, target::GnTarget, types::*},
    anyhow::{anyhow, Context, Error},
    argh::FromArgs,
    cargo_metadata::DependencyKind,
    serde_derive::{Deserialize, Serialize},
    std::collections::HashMap,
    std::{
        fs::File,
        io::{self, Read, Write},
        path::PathBuf,
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
    cargo_configs: Option<PathBuf>,
    /// location of GN file
    #[argh(option, short = 'o')]
    output: Option<PathBuf>,
    /// location of GN binary to use for formating.
    /// If no path is provided, no format will be run.
    #[argh(option)]
    gn_bin: Option<PathBuf>,
    /// don't generate a target for the root crate
    #[argh(switch)]
    skip_root: bool,
}

type CrateName = String;
type Version = String;

/// Per-target metadata in the Cargo.toml for Rust crates that
/// require extra information to in the BUILD.gn
#[derive(Clone, Serialize, Deserialize, Debug)]
pub struct TargetCfg {
    /// Platform this configuration is for. None is all platforms.
    platform: Option<Platform>,
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
    /// array of crates with target specific configuration
    #[serde(rename = "crate")]
    crate_: HashMap<CrateName, HashMap<Version, Vec<TargetCfg>>>,
}

#[derive(Serialize, Deserialize, Debug)]
struct BuildMetadata {
    gn: Option<GnBuildMetadata>,
}

pub fn generate_from_manifest<W: io::Write>(
    mut output: &mut W,
    manifest_path: PathBuf,
    project_root: PathBuf,
    cargo: Option<PathBuf>,
    skip_root: bool,
) -> Result<(), Error> {
    // generate cargo metadata
    let mut cmd = cargo_metadata::MetadataCommand::new();
    let parent_dir = manifest_path
        .parent()
        .with_context(|| format!("while parsing parent path: {:?}", &manifest_path))?;
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
    let mut file = File::open(&manifest_path)?;
    let mut contents = String::new();
    file.read_to_string(&mut contents)
        .with_context(|| format!("while reading manifest: {:?}", &manifest_path))?;
    let metadata_configs: BuildMetadata = toml::from_str(&contents)?;

    gn::write_header(&mut output, &manifest_path)?;

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
                    build_graph.add_cargo_package(dep.pkg.clone())?;
                    for kinds in dep.dep_kinds.iter() {
                        if kinds.kind == DependencyKind::Normal {
                            let platform = kinds.target.as_ref().map(|t| format!("{}", t));
                            gn::write_top_level_rule(&mut output, platform, &metadata[&dep.pkg])
                                .with_context(|| {
                                    format!(
                                        "while writing top level rule for package: {}",
                                        &dep.pkg
                                    )
                                })?;
                        }
                    }
                }
            } else {
                build_graph
                    .add_cargo_package(top_level_id.clone())
                    .with_context(|| "could not add cargo package")?;
                gn::write_top_level_rule(&mut output, None, &metadata[&top_level_id])?;
            }
        }
        None => return Err(anyhow!("Failed to resolve a build graph for the package tree")),
    }
    let gn_config = match metadata_configs.gn {
        Some(ref gn_configs) => gn_configs.config.as_ref(),
        None => None,
    };

    // Write out a GN rule for each target in the build graph
    // needs to be sorted for stable output to minimize diff churn
    let mut graph_targets: Vec<&GnTarget<'_>> = build_graph.targets().collect();
    graph_targets.sort();

    // Clone the GN configs so we can consume them while writing the GN files
    let mut gn_crates = metadata_configs.gn.as_ref().map(|i| i.crate_.clone());

    for target in graph_targets {
        let cfg: Option<Vec<TargetCfg>> = match gn_crates {
            Some(ref mut gn_crates) => match gn_crates.get_mut(&target.gn_name()) {
                Some(crate_) => {
                    let resp = crate_.remove(&target.version());
                    if crate_.len() == 0 {
                        gn_crates
                            .remove(&target.gn_name())
                            .ok_or(anyhow!("removed non-existant crate from custom configs"))?;
                    }
                    resp
                }
                None => None,
            },
            None => None,
        };

        if target.uses_build_script() && cfg.is_none() {
            let build_output = BuildScript::compile(target).and_then(|s| s.execute());
            match build_output {
                Ok(rules) => {
                    return Err(anyhow!(
                        "Add this to your Cargo.toml located at {}\n \
                        [[gn.crate.{}.\"{}\"]] \n \
                        rustflags = [{}]\n",
                        manifest_path.display(),
                        target.gn_name(),
                        target.version(),
                        rules.cfgs.join(", ")
                    ));
                }
                Err(err) => {
                    return Err(anyhow!(
                        "{} {} uses a build script but no section defined in the GN section \
                    nor can we automatically generate the appropriate rules:\n{}",
                        target.gn_name(),
                        target.version(),
                        err,
                    ))
                }
            }
        }
        let _ = gn::write_rule(&mut output, &target, &project_root, gn_config, cfg)?;
    }

    // Collect any unused crates and show the user
    if let Some(gn_crates) = gn_crates {
        if gn_crates.len() != 0 {
            let mut accum = String::new();
            for (crate_, cfg) in gn_crates.iter() {
                for (version, _) in cfg.iter() {
                    accum.push_str(format!("crate: {} {}\n", crate_, version).as_str());
                }
            }
            return Err(anyhow!("The following configs are unused:\n\n{}", accum));
        }
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
    {
        let mut output: Box<dyn io::Write> = if opt.output.is_some() {
            Box::new(&mut gn_output_buffer)
        } else {
            Box::new(io::stdout())
        };

        generate_from_manifest(
            &mut output,
            opt.manifest_path,
            opt.project_root,
            opt.cargo,
            opt.skip_root,
        )?;
    }

    // Write the file buffer to an actual file
    if let Some(ref path) = opt.output {
        let mut fout = File::create(path)?;
        fout.write_all(&gn_output_buffer)?;
    }

    // Format the GN file
    if opt.output.is_none() && opt.gn_bin.is_some() {
        return Err(anyhow!("Cannot format GN output to stdout"));
    }
    if let Some(gn_bin) = opt.gn_bin {
        let output = opt.output.expect("output");
        eprintln!("Formatting output file: {}", &output.to_string_lossy());
        Command::new(&gn_bin)
            .arg("format")
            .arg(output)
            .output()
            .with_context(|| format!("failed to run GN format command: {:?}", &gn_bin))?;
    }

    Ok(())
}
