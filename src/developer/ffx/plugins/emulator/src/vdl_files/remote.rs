// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::VDLFiles;
use crate::types::{FuchsiaPaths, InTreePaths};
use anyhow::Result;
use errors::ffx_bail;
use ffx_emulator_args::RemoteCommand;
use home::home_dir;
use std::fs::create_dir_all;
use std::process::{Command, Output, Stdio};
use std::str;

impl VDLFiles {
    pub fn remote_ssh_command(
        &self,
        base_args: &[&str],
        command: &str,
        stream_io: bool,
    ) -> Result<Output, std::io::Error> {
        let mut child = Command::new("ssh");
        child.args(base_args).arg(command);
        if stream_io {
            child.stdin(Stdio::inherit()).stdout(Stdio::inherit()).stderr(Stdio::inherit());
        }
        child.output()
    }

    pub fn run_remote_files_locally(
        &self,
        command: &RemoteCommand,
        build_dir: &str,
        emu_targets: &str,
    ) -> Result<Output, std::io::Error> {
        // Start by downloading the images from the remote host
        let out_dir = "out/fetched";
        create_dir_all(out_dir).expect("Couldn't create output directory");
        let result = Command::new("rsync")
            .arg(&format!("{}:{}/{{{}}}", &command.host, &build_dir, emu_targets.replace(" ", ",")))
            .arg(out_dir)
            .stdout(Stdio::inherit())
            .output()?;
        if !result.status.success() {
            let error_string = format!(
                "Couldn't retrieve files: {}",
                String::from_utf8(result.stderr).expect("Couldn't parse stderr")
            );
            return Err(std::io::Error::new(std::io::ErrorKind::Other, error_string));
        }
        println!("Build images fetched into {}", out_dir);

        // TODO(doughertyda): See if we can replace this with an exec call instead
        // Start up the emulator using a child 'ffx vdl' command, redirecting all I/O from this
        // process to the new one.
        let mut paths = InTreePaths { root_dir: None, build_dir: None };
        let build_dir = paths.find_fuchsia_root().expect("").join(out_dir);
        return Command::new("ffx")
            .arg("vdl")
            .arg("start")
            .env("IMAGE_FVM_RAW", build_dir.join("fvm.blk"))
            .env("IMAGE_QEMU_KERNEL_RAW", build_dir.join("multiboot.bin"))
            .env("IMAGE_ZIRCONA_ZBI", build_dir.join("fuchsia.zbi"))
            .args(&command.args)
            .stdin(Stdio::inherit())
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit())
            .output();
    }

    pub fn remote_emulator(&self, remote_command: &RemoteCommand) -> Result<()> {
        // Our ssh commands will all go to the same host, and we'll reuse the connection so we don't
        // have to hit the gnubby over and over again
        let ssh_file = home_dir().unwrap_or_default().join(".ssh/control-fuchsia-fx-remote");
        let ssh_base_args = [
            &remote_command.host,
            "-S",
            ssh_file.to_str().expect("Home directory cannot be parsed as valid UTF-8"),
            "-o",
            "ControlMaster=auto",
        ];

        // Right now, the command still uses `fx` commands for building, which isn't supported
        if self.is_sdk {
            ffx_bail!("Remote emulation isn't currently supported by the SDK. Bailing out.");
        }

        // If there is already a control master then exit it. We can't be sure its to the right host
        // and it also could be stale. We don't really care about the return value.
        let _ = self.remote_ssh_command(&ssh_base_args, "-O exit > /dev/null 2&>1", false);

        let dir = &remote_command.dir;
        let mut fx_path = dir.clone();
        fx_path.push_str("/.jiri_root/bin/fx");

        // Verifies the remote directory exists, whether passed in via --dir or using the default
        let mut result = self.remote_ssh_command(
            &ssh_base_args,
            &format!("ls {} > /dev/null", &fx_path),
            false,
        )?;
        if !result.status.success() {
            ffx_bail!("failed to find {} on $host, please specify --dir", &fx_path);
        }

        let emu_targets = "multiboot.bin fuchsia.zbi obj/build/images/fvm.blk";

        // The ffx tool currently does not support compilation or determining the build directory
        // for a local source tree. We have to use fx subcommands for these tasks.
        if !remote_command.no_build {
            // Trigger a remote build
            result = self.remote_ssh_command(
                &ssh_base_args,
                &format!("cd {} && ./.jiri_root/bin/fx build {}", &dir, emu_targets),
                true,
            )?;
            if !result.status.success() {
                ffx_bail!("Remote build failed: {}", String::from_utf8(result.stderr)?);
            }
        }

        // Get some details about the remote build results
        result = self.remote_ssh_command(
            &ssh_base_args,
            &format!("cd {} && ./.jiri_root/bin/fx get-build-dir", &dir),
            false,
        )?;
        if !result.status.success() {
            ffx_bail!("Couldn't retrieve remote build dir: {}", String::from_utf8(result.stderr)?);
        }
        let build_dir = String::from_utf8(result.stdout)?.trim().to_string();
        println!("Build dir is: {}", build_dir);

        // TODO(doughertyda): save_remote_info "$host" "$dir"

        if !remote_command.stream {
            // Fetch artifacts and run local emulator when streaming is disabled.
            result = self.run_remote_files_locally(&remote_command, &build_dir, &emu_targets)?;
            if !result.status.success() {
                ffx_bail!("Couldn't start emu: {}", String::from_utf8(result.stderr)?);
            }
        }

        // TODO(doughertyda): Add the streamed version here

        Ok(())
    }
}
