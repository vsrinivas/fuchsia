// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl_fuchsia_io::{MAX_BUF, DirectoryProxy};
use fidl_fuchsia_pkg::{PackageResolverMarker, UpdatePolicy};
use fuchsia_app::client::Launcher;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use std::mem;
use structopt::StructOpt;

#[derive(StructOpt)]
#[structopt(name = "pkgctl")]
struct Options {
    #[structopt(
        long = "pkg-resolver-uri",
        help = "Package URI of the package resolver",
        default_value = "fuchsia-pkg://fuchsia.com/pkg_resolver#meta/pkg_resolver.cmx"
    )]
    pkg_resolver_uri: String,

    #[structopt(subcommand)]
    cmd: Command,
}

#[derive(StructOpt)]
enum Command {
    #[structopt(name = "resolve", about = "resolve a package")]
    Resolve {
        #[structopt(help = "URI of package to cache")]
        pkg_uri: String,

        #[structopt(help = "Package selectors")]
        selectors: Vec<String>,
    },
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let Options {
        pkg_resolver_uri,
        cmd,
    } = Options::from_args();

    // Launch the server and connect to the resolver service.
    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher
        .launch(pkg_resolver_uri, None)
        .context("Failed to launch resolver service")?;

    let resolver = app
        .connect_to_service(PackageResolverMarker)
        .context("Failed to connect to resolver service")?;

    let fut = async {
        match cmd {
            Command::Resolve { pkg_uri, selectors } => {
                println!("resolving {} with the selectors {:?}", pkg_uri, selectors);

                let (dir, dir_server_end) = fidl::endpoints::create_proxy()?;

                let res = await!(resolver.resolve(
                    &pkg_uri,
                    &mut selectors.iter().map(|s| s.as_str()),
                    &mut UpdatePolicy {
                        fetch_if_absent: true,
                        allow_old_versions: true,
                    },
                    dir_server_end,
                ))?;
                zx::Status::ok(res)?;

                let entries = await!(readdir(dir))?;
                println!("package resolved");

                for entry in entries {
                    println!("package directory: {:?}", entry);
                }

                Ok(())
            }
        }
    };

    executor.run_singlethreaded(fut)
}

async fn readdir(dir: DirectoryProxy) -> Result<Vec<String>, Error> {
    #[repr(packed)]
    struct Dirent {
        _ino: u64,
        size: u8,
        _type: u8,
    }

    let mut entries = vec![];
    loop {
        let (status, buf) = await!(dir.read_dirents(MAX_BUF))?;
        zx::Status::ok(status)?;

        if buf.is_empty() {
            break;
        }

        // The buffer contains an arbitrary number of dirents.
        let mut slice = buf.as_slice();
        while !slice.is_empty() {
            // Read the dirent, and figure out how long the name is.
            let (head, rest) = slice.split_at(mem::size_of::<Dirent>());

            // Cast the dirent bytes into a `Dirent`, and extract out the size of the name.
            let size = unsafe {
                let dirent: *const Dirent = mem::transmute(head.as_ptr());
                (*dirent).size as usize
            };

            // Package resolver paths are always utf8.
            entries.push(String::from_utf8(rest[..size].to_vec())?);

            // Finally, skip over the name.
            slice = &rest[size..];
        }
    }

    Ok(entries)
}
