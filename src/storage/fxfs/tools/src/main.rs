// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fuchsia_async as fasync,
    fxfs::{
        mkfs, mount,
        object_store::{
            crypt::{Crypt, InsecureCrypt},
            fsck::{self},
        },
    },
    std::sync::Arc,
    storage_device::{file_backed_device::FileBackedDevice, DeviceHolder},
};

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    /// path to the image file file to read or write
    #[argh(option, short = 'i')]
    image: String,
    /// whether to run the tool verbosely
    #[argh(switch, short = 'v')]
    verbose: bool,
    #[argh(subcommand)]
    subcommand: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Format(FormatSubCommand),
    Fsck(FsckSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// format the file or block device as an empty Fxfs filesystem
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// verify the integrity of the filesystem image
#[argh(subcommand, name = "fsck")]
struct FsckSubCommand {}

struct SimpleLogger;

impl log::Log for SimpleLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            if record.level() <= log::Level::Warn {
                eprintln!("[{}] {}", record.level(), record.args());
            } else {
                println!("[{}] {}", record.level(), record.args());
            }
        }
    }

    fn flush(&self) {}
}

const LOGGER: SimpleLogger = SimpleLogger {};

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(log::LevelFilter::Info);
    log::debug!("fxfs {:?}", std::env::args());

    let args: TopLevel = argh::from_env();

    // TODO(jfsulliv): Add support for side-loaded encryption keys.
    let crypt: Arc<dyn Crypt> = Arc::new(InsecureCrypt::new());
    let device = DeviceHolder::new(FileBackedDevice::new(
        std::fs::OpenOptions::new().read(true).write(true).open(args.image)?,
    ));

    match args.subcommand {
        SubCommand::Format(_) => {
            mkfs::mkfs(device, crypt).await?;
            Ok(())
        }
        SubCommand::Fsck(_) => {
            let fs = mount::mount(device, crypt).await?;
            let options = fsck::FsckOptions {
                fail_on_warning: false,
                halt_on_error: false,
                do_slow_passes: true,
                on_error: |err| eprintln!("{:?}", err.to_string()),
                verbose: args.verbose,
            };
            fsck::fsck_with_options(&fs, options).await
        }
    }
}
