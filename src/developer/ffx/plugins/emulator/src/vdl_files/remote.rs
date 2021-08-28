// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::VDLFiles;
use crate::types::{FuchsiaPaths, InTreePaths};
use anyhow::{format_err, Result};
use errors::ffx_bail;
use ffx_emulator_args::RemoteCommand;
use fuchsia_async::{Duration, LocalExecutor};
use fuchsia_hyper::new_https_client;
use home::home_dir;
use std::env;
use std::fs::create_dir_all;
use std::net::TcpListener;
use std::process::{Command, Output, Stdio};
use std::str;
use std::thread;

impl VDLFiles {
    // Launches an ssh command to the remote host, passing the arguments on the command line and
    // returning the process's output to the caller. If stream_io is true, the input and output will
    // be redirected to the stdin/stdout/stderr of the calling process, and only the child's return
    // value will be returned to the caller. Note that the base_args and additional_args are passed
    // to the ssh process as separate strings, while "command" is kept together as if in quotes on
    // the command line.
    fn remote_ssh_command(
        base_args: &[&str],
        additional_args: &[&str],
        command: &str,
        stream_io: bool,
    ) -> Result<Output, std::io::Error> {
        let mut child = Command::new("ssh");
        child.args(base_args).args(additional_args).arg(command);
        if stream_io {
            child.stdin(Stdio::inherit()).stdout(Stdio::inherit()).stderr(Stdio::inherit());
        }
        child.output()
    }

    // Launches a local instance of the emulator, using run files from the remote host. The files
    // are downloaded to the local host using rsync (if needed), and I/O is redirected to the child
    // for the duration of its execution.
    fn run_remote_files_locally(
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

        // TODO(doughertyda): See if we can replace this with an exec call, or call it directly
        // Start up the emulator using a child 'ffx emu' command, redirecting all I/O from this
        // process to the new one.
        let mut paths = InTreePaths { root_dir: None, build_dir: None };
        let root_dir = paths.find_fuchsia_root().expect("");
        let build_dir = root_dir.join(out_dir);
        return Command::new(root_dir.join(".jiri_root/bin/ffx").to_str().expect(""))
            .arg("emu")
            .arg("start")
            .args(["-A", "x64"])
            .args(["-f", build_dir.join("fvm.blk").to_str().expect("")])
            .args(["-k", build_dir.join("multiboot.bin").to_str().expect("")])
            .args(["-z", build_dir.join("fuchsia.zbi").to_str().expect("")])
            .args(&command.args)
            .stdin(Stdio::inherit())
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit())
            .output();
    }

    // Looks for a listener on the indicated port, then launches an instance of the FEMU web app in
    // a new Chrome process. If the port isn't listening it will retry once per second for 30
    // seconds, to give the emulator a chance to start up.
    fn launch_chrome(port: u16) -> Result<()> {
        let mut command = match env::consts::OS {
            "macos" => {
                let mut c = Command::new("open");
                c.arg("-a").arg("/Applications/Google Chrome.app");
                c
            }
            "linux" => Command::new("google-chrome"),
            _ => {
                ffx_bail!("Running on unsupported OS {}", env::consts::OS);
            }
        };
        let mut executor = LocalExecutor::new().unwrap();
        executor.run_singlethreaded(async move {
            let timeout = 10;
            for waited in 0..timeout {
                let client = new_https_client();
                let url = format!("http://localhost:{}", port).parse().unwrap();
                let res = client.get(url).await;
                match res {
                    Ok(_) => {
                        let result = command
                            .arg(format!("https://web-femu.appspot.com/?port={}", port))
                            .output()?;
                        if !result.status.success() {
                            ffx_bail!(
                                "Couldn't launch Chrome: {}",
                                String::from_utf8(result.stderr).expect("Couldn't parse stderr")
                            );
                        }
                        break;
                    }
                    Err(_) => {
                        if timeout > waited {
                            println!("Waiting for emulator ({} seconds left)...", timeout - waited);
                            thread::sleep(Duration::from_secs(1));
                        } else {
                            ffx_bail!("No emulator after waiting {} seconds, giving up.", timeout);
                        }
                    }
                };
            }
            Ok(())
        })
    }

    // Root of the "remote" vdl command. Communicates with a remote host over ssh to (optionally)
    // compile and execute an instance of FEMU. If --stream is selected, the FEMU instance runs on
    // the remote host, and a Chrome web app provides a window to view the screen. Otherwise, the
    // emulator is run locally using the remote's compiled Fuchsia binaries. This still depends on
    // fx to perform some in-tree functions, so SDK users are currently rejected.
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
        let _ = VDLFiles::remote_ssh_command(&ssh_base_args, &["-O", "exit"], "", false);

        let dir = &remote_command.dir;
        let mut fx_path = dir.clone();
        fx_path.push_str("/.jiri_root/bin/fx");

        // Verifies the remote directory exists, whether passed in via --dir or using the default
        let mut result =
            VDLFiles::remote_ssh_command(&ssh_base_args, &[], &format!("ls {}", &fx_path), false)?;
        if !result.status.success() {
            ffx_bail!(
                "failed to find {} on {}, please specify --dir",
                &fx_path,
                &remote_command.host
            );
        }

        let emu_targets = "multiboot.bin fuchsia.zbi obj/build/images/fuchsia/fuchsia/fvm.blk";

        // The ffx tool currently does not support compilation or determining the build directory
        // for a local source tree. We have to use fx subcommands for these tasks.
        if !remote_command.no_build {
            // Check that goma is started before building
            let _ = VDLFiles::remote_ssh_command(
                &ssh_base_args,
                &[],
                &format!("cd {} && ./.jiri_root/bin/fx goma", &dir),
                true,
            )?;
            // Trigger a remote build
            result = VDLFiles::remote_ssh_command(
                &ssh_base_args,
                &[],
                &format!("cd {} && ./.jiri_root/bin/fx build {}", &dir, emu_targets),
                true,
            )?;
            if !result.status.success() {
                ffx_bail!("Remote build failed: {}", String::from_utf8(result.stderr)?);
            }
        }

        // Get some details about the remote build results
        result = VDLFiles::remote_ssh_command(
            &ssh_base_args,
            &[],
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
        } else {
            let port = remote_command.port;
            TcpListener::bind(("127.0.0.1", port)).map_err(|e| {
                format_err!(
                    "Local port {} is not available. {} \n\
                    Please try specifying a different port using --port flag.",
                    port,
                    e
                )
            })?;
            println!("Listening on port {}", port);

            if !remote_command.no_open {
                let handle = thread::spawn(move || VDLFiles::launch_chrome(port));
                // TODO(doughertyda): Replace with non-fx emulator launch.
                // This command is not meant to stay here. It's a placeholder to let the chrome
                // window come up while we figure out the vdl way to start the remote emulator.
                let _ = Command::new("ssh")
                    .args(ssh_base_args)
                    .arg("-6")
                    .arg("-L")
                    .arg(format!("{}:localhost:8080", port))
                    .arg("-o")
                    .arg("ExitOnForwardFailure=yes")
                    .arg("cd ~/fuchsia && xvfb-run ./.jiri_root/bin/fx emu -x 8080")
                    .stdin(Stdio::inherit())
                    .stdout(Stdio::inherit())
                    .stderr(Stdio::inherit())
                    .output()?;
                handle.join().unwrap()?;
            }
        }

        // Clean up the session
        let result = VDLFiles::remote_ssh_command(&ssh_base_args, &["-O", "exit"], "", false)?;
        if !result.status.success() {
            ffx_bail!("SSH session cleanup failed: {}", String::from_utf8(result.stderr)?);
        }
        Ok(())
    }
}
