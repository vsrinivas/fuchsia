// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fatfs as _,
    serde::Deserialize,
    std::collections::{HashMap, HashSet},
    std::fs::{File, OpenOptions},
    std::io::{copy, BufReader, Write},
    std::path::Path,
};

// Reasonable defaults for sector and cluster sizes. Sector is the 'block' unit
// of the entire image, and we pick the minimal size so as to waste less space;
// cluster is the unit of file allocation, so it's reasonable enough to make
// that 'page'-sized.
const SECTOR_SIZE: u64 = 512;
const SECTORS_PER_CLUSTERS: u64 = 8;
const CLUSTER_SIZE: u64 = SECTORS_PER_CLUSTERS * SECTOR_SIZE;

/// `mkfatfs` creates a FAT filesystem image given a manifest of entries. The
/// size is reverse-engineered to be fairly minimal, the decision of whether to
/// use FAT12, FAT16, or FAT32 being a part of that.
#[derive(FromArgs, Debug, PartialEq)]
pub struct Args {
    /// the path to a manifest of filesystem entries, given as a JSON list of
    /// elements of the form `{{"source": <path>, "destination": <path> }}`.
    #[argh(option)]
    pub manifest: String,

    /// the path at which to write the output filesystem image.
    #[argh(option)]
    pub output: String,

    /// the path at which to write a depfile, enumerating the manifest entries.
    #[argh(option)]
    pub depfile: String,
}

// Specifies a filesystem entry.
#[derive(Debug, Deserialize)]
pub struct ManifestEntry {
    // The source path to a file on the host.
    pub source: String,

    // The destination path within the filesystem, spelled with whatever form
    // of slash the host uses for a path token.
    pub destination: String,
}

// std::fs::File is a thin wrapper around a file descriptor and does not track
// any associated filepath.
pub struct FileWithPath {
    pub file: File,
    pub path: String,
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();

    // Load filesystem entries from the input JSON manifest.
    let manifest = File::open(args.manifest)?;
    let manifest_reader = BufReader::new(manifest);
    let entries: Vec<ManifestEntry> = serde_json::de::from_reader(manifest_reader)?;

    // Record file sizes and directories to get a sense for the would-be size
    // of the data region.
    let mut files = HashMap::<String, FileWithPath>::new(); // destination -> source
    let mut dirs = HashSet::<String>::new();
    let mut num_clusters: u64 = 0;
    for entry in entries {
        // We estimate each directory as needing a single cluster for its
        // metadata.
        for ancestor in Path::new(&entry.destination).ancestors() {
            if let Some(dir) = ancestor.to_str() {
                if !dirs.insert(String::from(dir)) {
                    // This directory - along with its ancestors - was already
                    // recorded.
                    break;
                }
                num_clusters += 1;
            }
        }

        let src = File::open(&entry.source)?;
        let metadata = src.metadata()?;
        num_clusters += (metadata.len() + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
        files.insert(entry.destination, FileWithPath { file: src, path: entry.source });
    }

    // We estimate sectors based on FAT32, for it is simpler, even though we
    // might end up creating a FAT12 or FAT16 image. The estimate should work
    // well enough and can also revisit and fugde this arithmetic later.
    let sector_upper_bound: u64 = 32    // Up to 32 reserved sectors.
        + (4 * num_clusters + SECTOR_SIZE - 1) / SECTOR_SIZE    // 32 bytes per FAT.
        + num_clusters * SECTORS_PER_CLUSTERS; // Data region sectors.
    let fs_size: u64 = sector_upper_bound * SECTOR_SIZE * 11 / 10; // +10% for good measure.

    let image =
        OpenOptions::new().create(true).truncate(true).write(true).read(true).open(&args.output)?;
    image.set_len(fs_size)?;

    let options = fatfs::FormatVolumeOptions::new()
        .bytes_per_sector(SECTOR_SIZE as u16)
        .bytes_per_cluster(CLUSTER_SIZE as u32)
        .fats(1) // We don't need FAT redundancy.
        .total_sectors(sector_upper_bound as u32);
    fatfs::format_volume(&image, options)?;

    let mut depfile = File::create(args.depfile)?;
    depfile.write_all(format!("{}:", &args.output).as_bytes())?;
    let fs = fatfs::FileSystem::new(&image, fatfs::FsOptions::new())?;
    for (dest_path, src) in files.iter_mut() {
        depfile.write_all(format!(" {}", &src.path).as_bytes())?;
        let mut dir = fs.root_dir();
        let mut parts: Vec<&str> = dest_path.split("/").collect();
        let filename = parts.pop().unwrap();
        for dirname in parts {
            dir = dir.create_dir(dirname)?;
        }
        let mut dest = dir.create_file(filename)?;
        copy(&mut src.file, &mut dest)?;
    }
    fs.unmount()?;
    Ok(())
}
