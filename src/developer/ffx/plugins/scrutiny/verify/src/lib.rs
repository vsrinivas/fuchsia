// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_verify_args::{Command, SubCommand},
    scrutiny_utils::path::relativize_path,
    std::{fs, io::Write, path::PathBuf},
};

mod bootfs;
mod component_resolvers;
mod kernel_cmdline;
mod route_sources;
mod routes;
mod static_pkgs;
mod structured_config;

#[ffx_plugin()]
pub async fn scrutiny_verify(cmd: Command) -> Result<()> {
    if cmd.depfile.is_some() && cmd.stamp.is_none() {
        bail!("Cannot specify --depfile without --stamp");
    }

    let tmp_dir = cmd.tmp_dir.as_ref();
    if let Some(tmp_dir) = tmp_dir {
        fs::create_dir_all(tmp_dir).map_err(|err| {
            anyhow!(
                "Failed to create temporary directory {:?} for `ffx scrutiny verify`: {}",
                tmp_dir,
                err
            )
        })?;
    }

    let deps_set = match &cmd.subcommand {
        SubCommand::Bootfs(subcommand) => bootfs::verify(subcommand, tmp_dir).await,
        SubCommand::ComponentResolvers(subcommand) => {
            component_resolvers::verify(subcommand, tmp_dir).await
        }
        SubCommand::KernelCmdline(subcommand) => kernel_cmdline::verify(subcommand, tmp_dir).await,
        SubCommand::RouteSources(subcommand) => route_sources::verify(subcommand, tmp_dir).await,
        SubCommand::Routes(subcommand) => routes::verify(subcommand, tmp_dir).await,
        SubCommand::StaticPkgs(subcommand) => static_pkgs::verify(subcommand).await,
        SubCommand::StructuredConfig(subcommand) => structured_config::verify(subcommand).await,
    }?;

    if let Some(depfile_path) = cmd.depfile.as_ref() {
        let stamp_path = cmd
            .stamp
            .as_ref()
            .ok_or_else(|| anyhow!("Cannot specify depfile without specifying stamp"))?;
        let stamp_path = stamp_path.to_str().ok_or_else(|| {
            anyhow!(
                "Stamp path {:?} cannot be converted to string for writing to depfile",
                stamp_path
            )
        })?;
        let mut depfile = fs::File::create(depfile_path).context("failed to create depfile")?;

        // Convert any absolute paths into paths relative to `cwd` to satisfy depfile format
        // requirements.
        let default_build_path = PathBuf::from(String::from("."));
        let relative_dep_paths: Vec<PathBuf> = deps_set
            .into_iter()
            .map(|dep_path| relativize_path(default_build_path.clone(), dep_path))
            .collect();

        let deps = relative_dep_paths
            .iter()
            .map(|dep_path| {
                dep_path.to_str().ok_or_else(|| {
                    anyhow!("Failed to convert path for depfile to string: {:?}", dep_path)
                })
            })
            .collect::<Result<Vec<&str>>>()?;
        write!(depfile, "{}: {}", stamp_path, deps.join(" "))
            .context("failed to write to depfile")?;
    }
    if let Some(stamp_path) = cmd.stamp.as_ref() {
        fs::write(stamp_path, "Verified\n").context("failed to write stamp file")?;
    }

    Ok(())
}
