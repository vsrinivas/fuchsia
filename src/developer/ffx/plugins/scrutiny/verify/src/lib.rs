// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    ffx_core::ffx_plugin,
    ffx_scrutiny_verify_args::{Command, SubCommand},
    std::{fs, io::Write},
};

mod bootfs;
mod component_resolvers;
mod kernel_cmdline;
mod route_sources;
mod routes;
mod static_pkgs;

#[ffx_plugin()]
pub async fn scrutiny_verify(cmd: Command) -> Result<()> {
    if cmd.depfile.is_some() && cmd.stamp.is_none() {
        bail!("Cannot specify --depfile without --stamp");
    }

    let deps_set = match cmd.subcommand {
        SubCommand::Bootfs(cmd) => bootfs::verify(cmd).await,
        SubCommand::ComponentResolvers(cmd) => component_resolvers::verify(cmd).await,
        SubCommand::KernelCmdline(cmd) => kernel_cmdline::verify(cmd).await,
        SubCommand::RouteSources(cmd) => route_sources::verify(cmd).await,
        SubCommand::Routes(cmd) => routes::verify(cmd).await,
        SubCommand::StaticPkgs(cmd) => static_pkgs::verify(cmd).await,
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

        let deps = deps_set
            .iter()
            .map(|path_buf| {
                path_buf.to_str().ok_or_else(|| {
                    anyhow!("Failed to convert path for depfile to string: {:?}", path_buf)
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
