// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    argh::FromArgs,
    chrono::{TimeZone, Utc},
    fuchsia_async as fasync,
    fxfs::{
        mkfs, mount,
        object_handle::{GetProperties, ObjectHandle, ReadObjectHandle, WriteObjectHandle},
        object_store::{
            crypt::{Crypt, InsecureCrypt},
            directory::replace_child,
            filesystem::OpenFxFilesystem,
            fsck::{self},
            transaction::{Options, TransactionHandler},
            volume::root_volume,
            Directory, HandleOptions, ObjectDescriptor, ObjectStore,
        },
    },
    std::{
        io::{Read, Write},
        path::Path,
        sync::Arc,
    },
    storage_device::{file_backed_device::FileBackedDevice, DeviceHolder},
};

const DEFAULT_VOLUME: &str = "default";

#[derive(FromArgs, PartialEq, Debug)]
/// fxfs
struct TopLevel {
    /// path to the image file to read or write
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

async fn print_ls(dir: &Directory<ObjectStore>) -> Result<(), Error> {
    const DATE_FMT: &str = "%b %d %Y %T+00";
    let layer_set = dir.store().tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter = dir.iter(&mut merger).await?;
    while let Some((name, object_id, descriptor)) = iter.get() {
        match descriptor {
            ObjectDescriptor::File => {
                let handle = ObjectStore::open_object(
                    dir.owner(),
                    object_id,
                    HandleOptions::default(),
                    None,
                )
                .await?;
                let properties = handle.get_properties().await?;
                let size = properties.data_attribute_size;
                let mtime = Utc.timestamp(
                    properties.modification_time.secs as i64,
                    properties.modification_time.nanos,
                );
                println!(
                    "-rwx------    1 nobody   nogroup    {:>8} {:>12} {}",
                    size,
                    mtime.format(DATE_FMT),
                    name
                );
            }
            ObjectDescriptor::Directory => {
                let mtime = Utc.timestamp(0, 0);
                println!(
                    "d---------    1 nobody   nogroup           0 {:>12} {}",
                    mtime.format(DATE_FMT),
                    name
                );
            }
            ObjectDescriptor::Volume => unimplemented!(),
        }
        iter.advance().await?;
    }
    Ok(())
}

/// Opens a volume on a device and returns a Directory to it's root.
async fn open_volume(
    fs: &OpenFxFilesystem,
    crypt: Arc<dyn Crypt>,
) -> Result<Directory<ObjectStore>, Error> {
    let root_volume = root_volume(fs).await.unwrap();
    let store = root_volume.open_or_create_volume(DEFAULT_VOLUME, crypt).await?;
    Directory::open(&store, store.root_directory_object_id()).await
}

/// Walks a directory path from a given root.
async fn walk_dir(
    path: &Path,
    mut dir: Directory<ObjectStore>,
) -> Result<Directory<ObjectStore>, Error> {
    for path in path.to_str().unwrap().split('/') {
        if path.len() == 0 {
            continue;
        }
        if let Some((object_id, descriptor)) = dir.lookup(&path).await? {
            if descriptor != ObjectDescriptor::Directory {
                bail!("Not a directory: {}", path);
            }
            dir = Directory::open(&dir.owner(), object_id).await?;
        } else {
            bail!("Not found: {}", path);
        }
    }
    Ok(dir)
}

async fn unlink(fs: &OpenFxFilesystem, crypt: Arc<dyn Crypt>, path: &Path) -> Result<(), Error> {
    let dir = walk_dir(path.parent().unwrap(), open_volume(&fs, crypt).await?).await?;
    let mut transaction = (*fs).clone().new_transaction(&[], Options::default()).await?;
    replace_child(&mut transaction, None, (&dir, path.file_name().unwrap().to_str().unwrap()))
        .await?;
    transaction.commit().await?;
    Ok(())
}

// Used to fsck after mutating operations.
async fn fsck(fs: OpenFxFilesystem, crypt: Arc<dyn Crypt>, verbose: bool) -> Result<(), Error> {
    // Re-open the filesystem to ensure it's locked.
    let fs = mount::mount(fs.take_device().await).await?;
    let options = fsck::FsckOptions {
        fail_on_warning: false,
        halt_on_error: false,
        do_slow_passes: true,
        on_error: |err| eprintln!("{:?}", err.to_string()),
        verbose,
    };
    fsck::fsck_with_options(&fs, Some(crypt), options).await
}

#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::debug!("fxfs {:?}", std::env::args());

    let args: TopLevel = argh::from_env();

    // TODO(jfsulliv): Add support for side-loaded encryption keys.
    let crypt: Arc<dyn Crypt> = Arc::new(InsecureCrypt::new());
    let device = DeviceHolder::new(FileBackedDevice::new(
        std::fs::OpenOptions::new().read(true).write(true).open(args.image)?,
    ));

    match args.subcommand {
        SubCommand::Rm(rmargs) => {
            let fs = mount::mount(device).await?;
            unlink(&fs, crypt.clone(), &Path::new(&rmargs.path)).await?;
            fs.close().await?;
            fsck(fs, crypt, args.verbose).await
        }
        SubCommand::Get(getargs) => {
            let fs = mount::mount(device).await?;
            let src = Path::new(&getargs.src);
            let dir = walk_dir(src.parent().unwrap(), open_volume(&fs, crypt).await?).await?;
            if let Some((object_id, descriptor)) =
                dir.lookup(&src.file_name().unwrap().to_str().unwrap()).await?
            {
                if descriptor != ObjectDescriptor::File {
                    bail!("Expected File. Found {:?}", descriptor);
                }
                let handle = ObjectStore::open_object(
                    dir.owner(),
                    object_id,
                    HandleOptions::default(),
                    None,
                )
                .await?;
                let mut writer = std::fs::File::create(getargs.dst)?;
                let mut buf = handle.allocate_buffer(handle.block_size() as usize);
                let mut ofs = 0;
                loop {
                    let bytes = handle.read(ofs, buf.as_mut()).await?;
                    ofs += bytes as u64;
                    writer.write(&buf.as_ref().as_slice()[..bytes])?;
                    if bytes as u64 != handle.block_size() {
                        break;
                    }
                }
            } else {
                println!("File not found {}", getargs.src);
            }
            Ok(())
        }
        SubCommand::Put(putargs) => {
            let fs = mount::mount(device).await?;
            let dst = Path::new(&putargs.dst);
            let dir =
                walk_dir(dst.parent().unwrap(), open_volume(&fs, crypt.clone()).await?).await?;
            let filename = dst.file_name().unwrap().to_str().unwrap();
            let mut transaction = fs.clone().new_transaction(&[], Options::default()).await?;
            if let Some(_) = dir.lookup(filename).await? {
                bail!("{} already exists", filename);
            }
            let handle = dir.create_child_file(&mut transaction, &filename).await?;
            transaction.commit().await?;
            let mut data = Vec::new();
            std::fs::File::open(&putargs.src)?.read_to_end(&mut data)?;
            let mut buf = handle.allocate_buffer(data.len());
            buf.as_mut_slice().copy_from_slice(&data);
            handle.write_or_append(Some(0), buf.as_ref()).await?;
            handle.flush().await?;
            fs.close().await?;
            fsck(fs, crypt, args.verbose).await
        }
        SubCommand::Format(_) => {
            log::set_max_level(log::LevelFilter::Info);
            mkfs::mkfs(device, crypt).await?;
            Ok(())
        }
        SubCommand::Fsck(_) => {
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
        SubCommand::Ls(lsargs) => {
            let path = Path::new(&lsargs.path);
            let fs = mount::mount(device).await?;
            let dir = walk_dir(&path, open_volume(&fs, crypt).await?).await?;
            print_ls(&dir).await?;
            Ok(())
        }
        SubCommand::Mkdir(mkdirargs) => {
            let fs = mount::mount(device).await?;
            let path = Path::new(&mkdirargs.path);
            let dir =
                walk_dir(path.parent().unwrap(), open_volume(&fs, crypt.clone()).await?).await?;
            let filename = path.file_name().unwrap().to_str().unwrap();
            let mut transaction = fs.clone().new_transaction(&[], Options::default()).await?;
            if let Some(_) = dir.lookup(filename).await? {
                bail!("{} already exists", filename);
            }
            dir.create_child_dir(&mut transaction, &filename).await?;
            transaction.commit().await?;
            fs.close().await?;
            fsck(fs, crypt, args.verbose).await
        }
        SubCommand::Rmdir(rmdirargs) => {
            let fs = mount::mount(device).await?;
            unlink(&fs, crypt.clone(), &Path::new(&rmdirargs.path)).await?;
            fs.close().await?;
            fsck(fs, crypt, args.verbose).await
        }
    }
}
