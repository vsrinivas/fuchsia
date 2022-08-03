// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Error};
use fidl::endpoints::Proxy;
use fidl::HandleBased;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::env::{split_paths, var_os};
use std::ffi::{CStr, CString};
use std::path::{Component, PathBuf};

enum ChrootMode {
    Namespace,
    Out,
    Exposed,
}

struct ChrootParams {
    /// exposed/out/namespace
    pub mode: ChrootMode,

    /// path/name of binary
    pub bin_path: PathBuf,

    /// arguments to be passed to binary
    pub argv: Vec<String>,
}

impl ChrootParams {
    fn from_env() -> Self {
        let mut args = std::env::args();

        let self_bin_path = args.next().unwrap();
        let self_bin_path = PathBuf::from(self_bin_path);
        let mode = if self_bin_path.ends_with("chns") {
            ChrootMode::Namespace
        } else if self_bin_path.ends_with("chout") {
            ChrootMode::Out
        } else {
            ChrootMode::Exposed
        };

        let bin_path = if let Some(arg) = args.next() {
            arg
        } else {
            Self::exit_help(self_bin_path, mode);
        };
        let bin_path = PathBuf::from(bin_path);

        let argv: Vec<String> = args.collect();

        Self { mode, bin_path, argv }
    }

    pub fn exit_help(self_bin_path: PathBuf, mode: ChrootMode) -> ! {
        let dir = match mode {
            ChrootMode::Exposed => "exposed",
            ChrootMode::Namespace => "namespace",
            ChrootMode::Out => "outgoing",
        };

        eprintln!(
            "{} <binary> <arguments>

Runs a binary after changing the root to the component's {} directory

This binary is built to work with dash-launcher.",
            self_bin_path.display(),
            dir
        );

        std::process::exit(1);
    }
}

fn resolve_binary_path(bin_path: PathBuf) -> Result<PathBuf, Error> {
    if bin_path.is_file() {
        return Ok(bin_path);
    }

    let mut components: Vec<Component<'_>> = bin_path.components().collect();

    if components.len() == 1 {
        let component = components.remove(0);
        if let Component::Normal(executable) = component {
            if let Some(paths) = var_os("PATH") {
                for path in split_paths(&paths) {
                    let full_path = path.join(&executable);
                    if full_path.is_file() {
                        return Ok(full_path);
                    }
                }
            }
        }
    }

    bail!("'{}' does not match any known binary files", bin_path.display())
}

#[fuchsia::main(logging = false)]
async fn main() -> Result<(), Error> {
    let params = ChrootParams::from_env();

    // Get a path to the binary (use the PATH variable if needed)
    let bin_path = resolve_binary_path(params.bin_path)?;
    let bin_path = bin_path.display().to_string();

    let job = fuchsia_runtime::job_default();
    let options = fdio::SpawnOptions::CLONE_STDIO
        | fdio::SpawnOptions::CLONE_ENVIRONMENT
        | fdio::SpawnOptions::CLONE_JOB
        | fdio::SpawnOptions::DEFAULT_LOADER;

    // Construct the argv for the binary
    let mut argv = params.argv;
    argv.insert(0, bin_path.clone());
    let argv: Vec<CString> = argv.into_iter().map(|a| CString::new(a).unwrap()).collect();
    let argv_ref: Vec<&CStr> = argv.iter().map(|s| s.as_c_str()).collect();

    // Create the namespace for the binary based on the mode
    let (local_path, new_path) = match params.mode {
        ChrootMode::Namespace => ("/ns", "/"),
        ChrootMode::Exposed => ("/exposed", "/svc"),
        ChrootMode::Out => ("/out", "/"),
    };

    let local_dir = fuchsia_fs::open_directory_in_namespace(
        local_path,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .unwrap()
    .into_channel()
    .unwrap()
    .into_zx_channel()
    .into();
    let new_path = CString::new(new_path).unwrap();
    let ns_entry_action = fdio::SpawnAction::add_namespace_entry(&new_path, local_dir);

    let utc_clock =
        fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS).unwrap().into_handle();
    let utc_clock_action = fdio::SpawnAction::add_handle(
        fuchsia_runtime::HandleInfo::new(fuchsia_runtime::HandleType::ClockUtc, 0),
        utc_clock,
    );

    let mut actions = [ns_entry_action, utc_clock_action];

    // Launch the binary
    let bin_path = CString::new(bin_path).unwrap();
    let process = fdio::spawn_etc(&job, options, &bin_path, &argv_ref, None, &mut actions).unwrap();

    // Wait for it to terminate
    let _ = fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED).await;
    Ok(())
}
