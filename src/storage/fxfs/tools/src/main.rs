// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fuchsia_async as fasync,
    fxfs::{
        crypt::{Crypt, InsecureCrypt},
        mkfs, mount,
        object_store::fsck::{self},
    },
    std::{io::Read, path::Path, sync::Arc},
    storage_device::{file_backed_device::FileBackedDevice, DeviceHolder},
    tools::ops,
};

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    /// whether to run the tool verbosely
    #[argh(switch, short = 'v')]
    verbose: bool,
    #[argh(subcommand)]
    subcommand: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    ImageEdit(ImageEditCommand),
    CreateGolden(CreateGoldenSubCommand),
    CheckGolden(CheckGoldenSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "image", description = "disk image manipulation commands")]
struct ImageEditCommand {
    /// path to the image file to read or write
    #[argh(option, short = 'f')]
    file: String,
    #[argh(subcommand)]
    subcommand: ImageSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum ImageSubCommand {
    Format(FormatSubCommand),
    Fsck(FsckSubCommand),
    Get(GetSubCommand),
    Ls(LsSubCommand),
    Mkdir(MkdirSubCommand),
    Put(PutSubCommand),
    Rm(RmSubCommand),
    Rmdir(RmdirSubCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
/// copies files from the image to the host filesystem, overwriting existing files.
#[argh(subcommand, name = "get")]
struct GetSubCommand {
    /// source file in image.
    #[argh(positional)]
    src: String,
    /// destination filename on host filesystem.
    #[argh(positional)]
    dst: String,
}
#[derive(FromArgs, PartialEq, Debug)]
/// copies files from the host filesystem to the image, overwriting existing files.
#[argh(subcommand, name = "put")]
struct PutSubCommand {
    /// source file on host filesystem.
    #[argh(positional)]
    src: String,
    /// destination filename in image.
    #[argh(positional)]
    dst: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// copies files from the host filesystem to the image, overwriting existing files.
#[argh(subcommand, name = "rm")]
struct RmSubCommand {
    /// path to remove from image.
    #[argh(positional)]
    path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// format the file or block device as an empty Fxfs filesystem
#[argh(subcommand, name = "mkfs")]
struct FormatSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// verify the integrity of the filesystem image
#[argh(subcommand, name = "fsck")]
struct FsckSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// List all files
#[argh(subcommand, name = "ls")]
struct LsSubCommand {
    #[argh(positional)]
    /// path to list.
    path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a new directory
#[argh(subcommand, name = "mkdir")]
struct MkdirSubCommand {
    #[argh(positional)]
    /// path to create.
    path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a new directory
#[argh(subcommand, name = "rmdir")]
struct RmdirSubCommand {
    #[argh(positional)]
    /// path to create.
    path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a golden image at current filesystem version.
#[argh(subcommand, name = "create_golden")]
struct CreateGoldenSubCommand {}

#[derive(FromArgs, PartialEq, Debug)]
/// Check all golden images at current filesystem version.
#[argh(subcommand, name = "check_golden")]
struct CheckGoldenSubCommand {
    #[argh(option)]
    /// path to golden images directory. derived from FUCHSIA_DIR if not set.
    images_dir: Option<String>,
}

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
    log::debug!("fxfs {:?}", std::env::args());

    let args: TopLevel = argh::from_env();
    match args.subcommand {
        SubCommand::ImageEdit(cmd) => {
            // TODO(jfsulliv): Add support for side-loaded encryption keys.
            let crypt: Arc<dyn Crypt> = Arc::new(InsecureCrypt::new());
            let device = DeviceHolder::new(FileBackedDevice::new(
                std::fs::OpenOptions::new().read(true).write(true).open(cmd.file)?,
            ));
            match cmd.subcommand {
                ImageSubCommand::Rm(rmargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt.clone()).await?;
                    ops::unlink(&fs, &vol, &Path::new(&rmargs.path)).await?;
                    fs.close().await?;
                    ops::fsck(&fs, crypt, args.verbose).await
                }
                ImageSubCommand::Get(getargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt).await?;
                    let data = ops::get(&vol, &Path::new(&getargs.src)).await?;
                    let mut reader = std::io::Cursor::new(&data);
                    let mut writer = std::fs::File::create(getargs.dst)?;
                    std::io::copy(&mut reader, &mut writer)?;
                    Ok(())
                }
                ImageSubCommand::Put(putargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt.clone()).await?;
                    let mut data = Vec::new();
                    std::fs::File::open(&putargs.src)?.read_to_end(&mut data)?;
                    ops::put(&fs, &vol, &Path::new(&putargs.dst), data).await?;
                    fs.close().await?;
                    ops::fsck(&fs, crypt, args.verbose).await
                }
                ImageSubCommand::Format(_) => {
                    log::set_max_level(log::LevelFilter::Info);
                    mkfs::mkfs(device, crypt).await?;
                    Ok(())
                }
                ImageSubCommand::Fsck(_) => {
                    log::set_max_level(log::LevelFilter::Info);
                    let fs = mount::mount(device).await?;
                    let options = fsck::FsckOptions {
                        fail_on_warning: false,
                        halt_on_error: false,
                        do_slow_passes: true,
                        on_error: |err| eprintln!("{:?}", err.to_string()),
                        verbose: args.verbose,
                    };
                    fsck::fsck_with_options(&fs, Some(crypt), options).await
                }
                ImageSubCommand::Ls(lsargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt).await?;
                    let dir = ops::walk_dir(&vol, &Path::new(&lsargs.path)).await?;
                    ops::print_ls(&dir).await?;
                    Ok(())
                }
                ImageSubCommand::Mkdir(mkdirargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt.clone()).await?;
                    ops::mkdir(&fs, &vol, &Path::new(&mkdirargs.path)).await?;
                    fs.close().await?;
                    ops::fsck(&fs, crypt, args.verbose).await
                }
                ImageSubCommand::Rmdir(rmdirargs) => {
                    let fs = mount::mount(device).await?;
                    let vol = ops::open_volume(&fs, crypt.clone()).await?;
                    ops::unlink(&fs, &vol, &Path::new(&rmdirargs.path)).await?;
                    fs.close().await?;
                    ops::fsck(&fs, crypt, args.verbose).await
                }
            }
        }
        SubCommand::CreateGolden(_) => tools::golden::create_image().await,
        SubCommand::CheckGolden(args) => tools::golden::check_images(args.images_dir).await,
    }
}
