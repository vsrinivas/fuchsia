// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    chrono::{TimeZone, Utc},
    fxfs::{
        crypt::Crypt,
        filesystem::OpenFxFilesystem,
        fsck,
        object_handle::{GetProperties, ObjectHandle, ReadObjectHandle, WriteObjectHandle},
        object_store::{
            directory::replace_child,
            transaction::{Options, TransactionHandler},
            volume::root_volume,
            Directory, HandleOptions, ObjectDescriptor, ObjectStore,
        },
    },
    std::{io::Write, ops::Deref, path::Path, sync::Arc},
};

const DEFAULT_VOLUME: &str = "default";

pub async fn print_ls(dir: &Directory<ObjectStore>) -> Result<(), Error> {
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
pub async fn open_volume(
    fs: &OpenFxFilesystem,
    crypt: Arc<dyn Crypt>,
) -> Result<Arc<ObjectStore>, Error> {
    let root_volume = root_volume(fs.deref().clone()).await?;
    root_volume.volume(DEFAULT_VOLUME, Some(crypt)).await.map(|v| v.into())
}

/// Walks a directory path from a given root.
pub async fn walk_dir(
    vol: &Arc<ObjectStore>,
    path: &Path,
) -> Result<Directory<ObjectStore>, Error> {
    let mut dir: Directory<ObjectStore> =
        Directory::open(vol, vol.root_directory_object_id()).await?;
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

pub async fn unlink(
    fs: &OpenFxFilesystem,
    vol: &Arc<ObjectStore>,
    path: &Path,
) -> Result<(), Error> {
    let dir = walk_dir(vol, path.parent().unwrap()).await?;
    let mut transaction = (*fs).clone().new_transaction(&[], Options::default()).await?;
    replace_child(&mut transaction, None, (&dir, path.file_name().unwrap().to_str().unwrap()))
        .await?;
    transaction.commit().await?;
    Ok(())
}

// Used to fsck after mutating operations.
pub async fn fsck(fs: &OpenFxFilesystem, verbose: bool) -> Result<(), Error> {
    // Re-open the filesystem to ensure it's locked.
    fsck::fsck_with_options(
        fs.deref().clone(),
        &fsck::FsckOptions {
            on_error: Box::new(|err| eprintln!("{:?}", err.to_string())),
            verbose,
            ..Default::default()
        },
    )
    .await
}

/// Read a file's contents into a Vec and return it.
pub async fn get(vol: &Arc<ObjectStore>, src: &Path) -> Result<Vec<u8>, Error> {
    let dir = walk_dir(vol, src.parent().unwrap()).await?;
    if let Some((object_id, descriptor)) =
        dir.lookup(&src.file_name().unwrap().to_str().unwrap()).await?
    {
        if descriptor != ObjectDescriptor::File {
            bail!("Expected File. Found {:?}", descriptor);
        }
        let handle =
            ObjectStore::open_object(dir.owner(), object_id, HandleOptions::default(), None)
                .await?;
        let mut out: Vec<u8> = Vec::new();
        let mut buf = handle.allocate_buffer(handle.block_size() as usize);
        let mut ofs = 0;
        loop {
            let bytes = handle.read(ofs, buf.as_mut()).await?;
            ofs += bytes as u64;
            out.write_all(&buf.as_ref().as_slice()[..bytes])?;
            if bytes as u64 != handle.block_size() {
                break;
            }
        }
        Ok(out)
    } else {
        bail!("File not found");
    }
}

/// Write the contents of a Vec to a file int he image.
pub async fn put(
    fs: &OpenFxFilesystem,
    vol: &Arc<ObjectStore>,
    dst: &Path,
    data: Vec<u8>,
) -> Result<(), Error> {
    let dir = walk_dir(vol, dst.parent().unwrap()).await?;
    let filename = dst.file_name().unwrap().to_str().unwrap();
    let mut transaction = (*fs).clone().new_transaction(&[], Options::default()).await?;
    if let Some(_) = dir.lookup(filename).await? {
        bail!("{} already exists", filename);
    }
    let handle = dir.create_child_file(&mut transaction, &filename).await?;
    transaction.commit().await?;
    let mut buf = handle.allocate_buffer(data.len());
    buf.as_mut_slice().copy_from_slice(&data);
    handle.write_or_append(Some(0), buf.as_ref()).await?;
    handle.flush().await
}

/// Create a directory.
pub async fn mkdir(
    fs: &OpenFxFilesystem,
    vol: &Arc<ObjectStore>,
    path: &Path,
) -> Result<(), Error> {
    let dir = walk_dir(vol, path.parent().unwrap()).await?;
    let filename = path.file_name().unwrap().to_str().unwrap();
    let mut transaction = (*fs).clone().new_transaction(&[], Options::default()).await?;
    if let Some(_) = dir.lookup(filename).await? {
        bail!("{} already exists", filename);
    }
    dir.create_child_dir(&mut transaction, &filename).await?;
    transaction.commit().await?;
    Ok(())
}
