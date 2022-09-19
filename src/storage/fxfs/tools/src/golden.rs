// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ops,
    anyhow::{bail, Context, Error},
    chrono::Local,
    fxfs::{
        crypt::{insecure::InsecureCrypt, Crypt},
        filesystem::{mkfs_with_default, FxFilesystem},
        serialized_types::LATEST_VERSION,
    },
    std::{
        io::Write,
        path::{Path, PathBuf},
        sync::Arc,
    },
    storage_device::{fake_device::FakeDevice, Device, DeviceHolder},
};

const IMAGE_BLOCKS: u64 = 8192;
const IMAGE_BLOCK_SIZE: u32 = 1024;
const EXPECTED_FILE_CONTENT: &[u8; 8] = b"content.";
const FXFS_GOLDEN_IMAGE_DIR: &str = "src/storage/fxfs/testdata";

/// Uses FUCHSIA_DIR environment variable to generate a path to the expected location of golden
/// images. Note that we do this largely for ergonomics because this binary is typically invoked
/// by running `fx fxfs create_golden` from an arbitrary directory.
fn golden_image_dir() -> Result<PathBuf, Error> {
    let fuchsia_dir = std::env::vars().find(|(k, _)| k == "FUCHSIA_DIR");
    if fuchsia_dir.is_none() {
        bail!("FUCHSIA_DIR environment variable is not set.");
    }
    let (_, fuchsia_dir) = fuchsia_dir.unwrap();
    Ok(PathBuf::from(fuchsia_dir).join(FXFS_GOLDEN_IMAGE_DIR))
}

/// Generates the filename where we expect to find a golden image for the current version of the
/// filesystem.
fn latest_image_filename() -> String {
    format!("fxfs_golden.{}.{}.img.zstd", LATEST_VERSION.major, LATEST_VERSION.minor)
}

/// Decompresses a zstd compressed local image into a RAM backed FakeDevice.
fn load_device(path: &Path) -> Result<FakeDevice, Error> {
    Ok(FakeDevice::from_image(zstd::Decoder::new(std::fs::File::open(path)?)?, IMAGE_BLOCK_SIZE)?)
}

/// Compresses a RAM backed FakeDevice into a zstd compressed local image.
async fn save_device(device: Arc<dyn Device>, path: &Path) -> Result<(), Error> {
    device.reopen(false);
    let mut writer = zstd::Encoder::new(std::fs::File::create(path)?, 6)?;
    let mut buf = device.allocate_buffer(device.block_size() as usize);
    let mut offset: u64 = 0;
    while offset < IMAGE_BLOCKS * IMAGE_BLOCK_SIZE as u64 {
        device.read(offset, buf.as_mut()).await?;
        writer.write_all(buf.as_ref().as_slice())?;
        offset += device.block_size() as u64;
    }
    writer.finish()?;
    Ok(())
}

/// Create a new golden image (at the current version).
pub async fn create_image() -> Result<(), Error> {
    let path = golden_image_dir()?.join(latest_image_filename());

    // TODO(fxbug.dev/95403): We need a way of testing crypt devices too.
    let crypt: Arc<dyn Crypt> = Arc::new(InsecureCrypt::new());
    {
        let device_holder = DeviceHolder::new(FakeDevice::new(IMAGE_BLOCKS, IMAGE_BLOCK_SIZE));
        let device = device_holder.clone();
        mkfs_with_default(device_holder, Some(crypt.clone())).await?;
        device.reopen(false);
        save_device(device, path.as_path()).await?;
    }
    let device_holder = DeviceHolder::new(load_device(&path)?);
    let device = device_holder.clone();
    let fs = FxFilesystem::open(device_holder).await?;
    let vol = ops::open_volume(&fs, crypt.clone()).await?;
    ops::mkdir(&fs, &vol, Path::new("some")).await?;
    ops::put(&fs, &vol, &Path::new("some/file.txt"), EXPECTED_FILE_CONTENT.to_vec()).await?;
    ops::put(&fs, &vol, &Path::new("some/deleted.txt"), EXPECTED_FILE_CONTENT.to_vec()).await?;
    ops::unlink(&fs, &vol, &Path::new("some/deleted.txt")).await?;
    fs.close().await?;
    save_device(device, &path).await?;

    let mut file = std::fs::File::create(golden_image_dir()?.join("images.gni").as_path())?;
    file.write_all(
        format!(
            "# Copyright {} The Fuchsia Authors. All rights reserved.\n\
             # Use of this source code is governed by a BSD-style license that can be\n\
             # found in the LICENSE file.\n\
             # Auto-generated by `fx fxfs create_golden` on {}\n",
            Local::now().format("%Y"),
            Local::now()
        )
        .as_bytes(),
    )?;
    file.write_all(b"fxfs_golden_images = [\n")?;
    let paths = std::fs::read_dir(golden_image_dir()?)?.collect::<Result<Vec<_>, _>>()?;
    for file_name in
        paths.iter().map(|e| e.file_name()).filter(|x| x.to_str().unwrap().ends_with(".zstd"))
    {
        file.write_all(format!("  \"{}\",\n", file_name.to_str().unwrap()).as_bytes())?;
    }
    file.write_all(b"]\n")?;
    Ok(())
}

/// Validates an image by looking for expected data and performing an fsck.
async fn check_image(path: &Path) -> Result<(), Error> {
    let crypt: Arc<dyn Crypt> = Arc::new(InsecureCrypt::new());
    {
        let device = DeviceHolder::new(load_device(path)?);
        let fs = FxFilesystem::open(device).await?;
        ops::fsck(&fs, true).await.context("fsck failed")?;
    }
    {
        let device = DeviceHolder::new(load_device(path)?);
        let fs = FxFilesystem::open(device).await?;
        let vol = ops::open_volume(&fs, crypt.clone()).await?;
        if ops::get(&vol, &Path::new("some/file.txt")).await? != EXPECTED_FILE_CONTENT.to_vec() {
            bail!("Expected file content incorrect.");
        }
        if ops::get(&vol, &Path::new("some/deleted.txt")).await.is_ok() {
            bail!("Found deleted file.");
        }
        fs.close().await
    }
}

pub async fn check_images(image_root: Option<String>) -> Result<(), Error> {
    let image_root = match image_root {
        Some(path) => std::env::current_exe()?.parent().unwrap().join(path),
        None => golden_image_dir()?,
    };

    // First check that there exists an image for the current version.
    let path = image_root.join(latest_image_filename());
    if std::fs::metadata(path.as_path()).is_err() {
        bail!(
            "Golden image is missing for version {} ({}). Please run 'fx fxfs create_golden'",
            LATEST_VERSION,
            path.display()
        )
    }
    // Next ensure that we can parse all golden images and validate expected content.
    let mut paths = std::fs::read_dir(image_root)?.collect::<Result<Vec<_>, _>>()?;
    paths.sort_unstable_by_key(|path| path.path().to_str().unwrap().to_string());
    for path_buf in
        paths.iter().map(|e| e.path().clone()).filter(|x| x.to_str().unwrap().ends_with(".zstd"))
    {
        println!("------------------------------------------------------------------------");
        println!("Validating golden image: {}", path_buf.file_name().unwrap().to_str().unwrap());
        println!("------------------------------------------------------------------------");
        if let Err(e) = check_image(path_buf.as_path()).await {
            bail!(
                "Failed to validate golden image {} with the latest code: {:?}",
                path_buf.display(),
                e
            )
        }
    }
    Ok(())
}
