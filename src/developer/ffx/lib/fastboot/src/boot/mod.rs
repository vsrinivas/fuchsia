// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::{file::FileResolver, map_fidl_error, stage_file},
    anyhow::{anyhow, Result},
    byteorder::{ByteOrder, LittleEndian},
    fidl_fuchsia_developer_bridge::FastbootProxy,
    std::convert::TryInto,
    std::fs::{metadata, File},
    std::io::{BufRead, BufReader, BufWriter, Read, Write},
    std::path::PathBuf,
    tempfile::{tempdir, TempDir},
};

const PAGE_SIZE: u32 = 4096;
const BOOT_MAGIC: &str = "ANDROID!";
const BOOT_SIZE: usize = 8;
const V4_HEADER_SIZE: u32 = 1580;

fn copy<R: Read, W: Write>(mut reader: BufReader<R>, writer: &mut BufWriter<W>) -> Result<()> {
    loop {
        let buffer = reader.fill_buf()?;
        let length = buffer.len();
        if length == 0 {
            return Ok(());
        }
        writer.write(buffer)?;
        reader.consume(length);
    }
}

async fn get_boot_image<W: Write, F: FileResolver + Sync>(
    writer: &mut W,
    file_resolver: &mut F,
    zbi: &String,
    vbmeta: &Option<String>,
    temp_dir: &TempDir,
) -> Result<PathBuf> {
    match vbmeta {
        None => {
            let mut path = PathBuf::new();
            path.push(file_resolver.get_file(writer, &zbi).await?);
            Ok(path)
        }
        Some(v) => {
            // if vbmeta exists, concat the two into a single boot image file
            let zbi_path = file_resolver.get_file(writer, &zbi).await?;
            let v_path = file_resolver.get_file(writer, &v).await?;
            let mut path = PathBuf::new();
            path.push(temp_dir.path());
            path.push("boot_image.bin");
            let mut outfile = BufWriter::new(File::create(&path)?);
            let zbi_file = BufReader::new(File::open(&zbi_path)?);
            let vbmeta_file = BufReader::new(File::open(&v_path)?);
            copy(zbi_file, &mut outfile)?;
            outfile.flush()?;
            copy(vbmeta_file, &mut outfile)?;
            outfile.flush()?;
            Ok(path)
        }
    }
}

pub async fn boot<W: Write, F: FileResolver + Sync>(
    writer: &mut W,
    file_resolver: &mut F,
    zbi: String,
    vbmeta: Option<String>,
    fastboot_proxy: &FastbootProxy,
) -> Result<()> {
    writeln!(writer, "Creating boot image...")?;
    let temp_dir = tempdir()?;
    let boot_image = get_boot_image(writer, file_resolver, &zbi, &vbmeta, &temp_dir).await?;

    let page_mask: u32 = PAGE_SIZE - 1;
    let kernal_size: u32 = metadata(&boot_image)?.len().try_into()?;
    let kernal_actual: u32 = (kernal_size + page_mask) & (!page_mask);

    let mut path = PathBuf::new();
    path.push(temp_dir.path());
    path.push("bootimg.bin");

    let mut outfile = BufWriter::new(File::create(&path)?);

    let mut header: [u8; 4096] = [0u8; 4096];
    header[0..BOOT_SIZE].copy_from_slice(&BOOT_MAGIC.as_bytes()[..]);
    LittleEndian::write_u32(&mut header[BOOT_SIZE..BOOT_SIZE + 4], kernal_size);
    LittleEndian::write_u32(&mut header[BOOT_SIZE + 12..BOOT_SIZE + 16], V4_HEADER_SIZE);
    LittleEndian::write_u32(
        &mut header[BOOT_SIZE + 32..BOOT_SIZE + 36],
        4, /* header version*/
    );
    outfile.write(&header)?;

    let in_file = BufReader::new(File::open(&boot_image)?);
    copy(in_file, &mut outfile)?;

    // Pad to page size.
    let padding = kernal_actual - kernal_size;
    let padding_bytes: [u8; 4096] = [0u8; 4096];
    outfile.write(&padding_bytes[..padding.try_into()?])?;
    outfile.flush()?;

    stage_file(
        writer,
        file_resolver,
        false, /* resolve */
        path.to_str().ok_or(anyhow!("Could not get temp boot image path"))?,
        fastboot_proxy,
    )
    .await?;

    fastboot_proxy
        .boot()
        .await
        .map_err(map_fidl_error)?
        .map_err(|e| anyhow!("Fastboot error: {:?}", e))?;

    Ok(())
}
