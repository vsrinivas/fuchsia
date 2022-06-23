// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Error};
use argh::FromArgs;
use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::env::{split_paths, var_os};
use std::ffi::{CStr, CString};
use std::path::{Component, PathBuf};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    name = "chroot",
    description = "Runs a binary after changing the root to the component's namespace/exposed/out directory",
    note = "This binary's behavior is determined by the name it was launched as.
    
`chns` sets the `/ns` directory as the root for the binary
`chexp` sets the `/exposed` directory as the root for the binary
`chout` sets the `/out` directory as the root for the binary

This binary is built to work with dash-launcher."
)]

pub struct ChrootParams {
    #[argh(positional)]
    /// path/name of binary
    pub bin_path: PathBuf,

    #[argh(positional)]
    /// arguments to be passed to binary
    pub argv: Vec<String>,
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
    let params: ChrootParams = argh::from_env();
    let self_bin_path = std::env::args().next().unwrap();

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

    // Create the namespace for the binary based on our name
    let (local_path, new_path) = if self_bin_path.ends_with("chns") {
        ("/ns", "/")
    } else if self_bin_path.ends_with("chout") {
        ("/out", "/")
    } else {
        // The /exposed directory puts protocols at the top-level.
        ("/exposed", "/svc")
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
    let mut actions = [ns_entry_action];

    // Launch the binary
    let bin_path = CString::new(bin_path).unwrap();
    let process = fdio::spawn_etc(&job, options, &bin_path, &argv_ref, None, &mut actions).unwrap();

    // Wait for it to terminate
    let _ = fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED).await;
    Ok(())
}
