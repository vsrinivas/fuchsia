// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Export a fuchsia.io/Directory as a factory filesystem partition on a provided block device.

#![deny(missing_docs)]

use {
    anyhow::{bail, Context, Error},
    byteorder::{LittleEndian, WriteBytesExt},
    fidl, fidl_fuchsia_io as fio,
    files_async::{readdir_recursive, DirEntry, DirentKind},
    fuchsia_zircon as zx,
    futures::StreamExt,
    remote_block_device::{cache::Cache, RemoteBlockDevice},
    std::io::Write,
};

const FACTORYFS_MAGIC: u64 = 0xa55d3ff91e694d21;
const BLOCK_SIZE: u32 = 4096;
const DIRENT_START_BLOCK: u32 = 1;
/// Size of the actual data in the superblock in bytes. We are only writing, and only care about the
/// latest version, so as long as this is updated whenever the superblock is changed we don't have
/// to worry about backwards-compatibility.
const SUPERBLOCK_DATA_SIZE: u32 = 52;
const FACTORYFS_MAJOR_VERSION: u32 = 1;
const FACTORYFS_MINOR_VERSION: u32 = 0;

/// Rounds the given num up to the next multiple of multiple.
fn round_up_to_align(x: u32, align: u32) -> u32 {
    debug_assert_ne!(align, 0);
    debug_assert_eq!(align & (align - 1), 0);
    (x + align - 1) & !(align - 1)
}

/// Return the number of blocks needed to store the provided number of bytes. This function will
/// overflow for values of [`bytes`] within about BLOCK_SIZE of u32::MAX, but it shouldn't be a
/// problem because that's already getting dangerously close to the maximum file size, which is the
/// same amount.
fn num_blocks(bytes: u32) -> u32 {
    // note: integer division in rust truncates the result
    (bytes + BLOCK_SIZE - 1) / BLOCK_SIZE
}

/// Round the provided number of bytes up to the next block boundary. Similar to the C++
/// `fbl::round_up(bytes, BLOCK_SIZE)`.
fn round_up_to_block_size(bytes: u32) -> u32 {
    num_blocks(bytes) * BLOCK_SIZE
}

/// Align the writer to the next block boundary by padding the rest of the current block with zeros.
/// [`written_bytes`] is the number of bytes written since the last time this writer was block
/// aligned. If the writer is already block aligned, no bytes are written.
fn block_align<Writer>(writer: &mut Writer, written_bytes: u32) -> Result<(), Error>
where
    Writer: Write,
{
    let fill = round_up_to_block_size(written_bytes) - written_bytes;
    for _ in 0..fill {
        writer.write_u8(0)?;
    }
    Ok(())
}

/// in-memory representation of a factoryfs partition
struct FactoryFS {
    major_version: u32,
    minor_version: u32,
    flags: u32,
    block_size: u32,
    entries: Vec<DirectoryEntry>,
}

impl FactoryFS {
    fn serialize_superblock<Writer>(&self, writer: &mut Writer) -> Result<(u32, u32), Error>
    where
        Writer: Write,
    {
        // on-disk format of the superblock for a factoryfs partition, in rust-ish notation
        // #[repr(C, packed)]
        // struct Superblock {
        //     /// Must be FACTORYFS_MAGIC.
        //     magic: u64,
        //     major_version: u32,
        //     minor_version: u32,
        //     flags: u32,
        //     /// Total number of data blocks.
        //     data_blocks: u32,
        //     /// Size in bytes of all the directory entries.
        //     directory_size: u32,
        //     /// Number of directory entries.
        //     directory_entries: u32,
        //     /// Time of creation of all files. We aren't going to write anything here though.
        //     create_time: u64,
        //     /// Filesystem block size.
        //     block_size: u32,
        //     /// Number of blocks for directory entries.
        //     directory_ent_blocks: u32,
        //     /// Start block for directory entries.
        //     directory_ent_start_block: u32,
        //     /// Reserved for future use. Written to disk as all zeros.
        //     reserved: [u32; rest_of_the_block],
        // }

        writer.write_u64::<LittleEndian>(FACTORYFS_MAGIC).context("failed to write magic")?;
        writer.write_u32::<LittleEndian>(self.major_version)?;
        writer.write_u32::<LittleEndian>(self.minor_version)?;
        writer.write_u32::<LittleEndian>(self.flags)?;

        // calculate the number of blocks all the data will take
        let data_blocks = self
            .entries
            .iter()
            .fold(0, |blocks, entry| blocks + num_blocks(entry.data.len() as u32));
        writer.write_u32::<LittleEndian>(data_blocks)?;

        // calculate the size of all the directory entries
        let entries_bytes = self.entries.iter().fold(0, |size, entry| size + entry.metadata_size());
        let entries_blocks = num_blocks(entries_bytes);
        writer.write_u32::<LittleEndian>(entries_bytes)?;
        writer.write_u32::<LittleEndian>(self.entries.len() as u32)?;

        // filesystem was created at the beginning of time
        writer.write_u64::<LittleEndian>(0)?;

        writer.write_u32::<LittleEndian>(self.block_size)?;

        writer.write_u32::<LittleEndian>(entries_blocks)?;
        writer.write_u32::<LittleEndian>(DIRENT_START_BLOCK)?;

        Ok((entries_bytes, entries_blocks))
    }

    /// Write the bytes of a FactoryFS partition to a byte writer. We assume the writer is seeked to
    /// the beginning. Serialization returns immediately after encountering a writing error for the
    /// first time, and as such may be in the middle of writing the partition. On error, there is no
    /// guarantee of a consistent partition.
    ///
    /// NOTE: if the superblock serialization is changed in any way, make sure SUPERBLOCK_DATA_SIZE
    /// is still correct.
    fn serialize<Writer>(&self, writer: &mut Writer) -> Result<(), Error>
    where
        Writer: Write,
    {
        let (entries_bytes, entries_blocks) =
            self.serialize_superblock(writer).context("failed to serialize superblock")?;

        // write out zeros for the rest of the first block
        block_align(writer, SUPERBLOCK_DATA_SIZE)?;

        // data starts after the superblock and all the blocks for the directory entries
        let mut data_offset = DIRENT_START_BLOCK + entries_blocks;
        // write out the directory entry metadata
        for entry in &self.entries {
            entry.serialize_metadata(writer, data_offset)?;
            data_offset += num_blocks(entry.data.len() as u32);
        }

        block_align(writer, entries_bytes)?;

        // write out the entry data
        for entry in &self.entries {
            entry.serialize_data(writer)?;
        }

        Ok(())
    }
}

/// a directory entry (aka file). factoryfs is a flat filesystem - conceptually there is a single
/// root directory that contains all the entries.
#[derive(Debug, PartialEq, Eq)]
struct DirectoryEntry {
    name: Vec<u8>,
    data: Vec<u8>,
}

impl DirectoryEntry {
    /// Get the size of the serialized metadata for this directory entry in bytes.
    fn metadata_size(&self) -> u32 {
        let name_len = self.name.len() as u32;
        let padding = round_up_to_align(name_len, 4) - name_len;

        // size of name_len...
        4
        // ...plus the size of data_len...
        + 4
        // ...plus the size of data_offset...
        + 4
        // ...plus the number of bytes in the name...
        + name_len
        // ...plus some padding to align to name to a 4-byte boundary
        + padding
    }

    /// Write the directory entry metadata to the provided byte writer. We assume that the calling
    /// function has seeked to the expected metadata location. data_offset is the expected block at
    /// which the data associated with this entry will be written in the future.
    fn serialize_metadata<Writer>(&self, writer: &mut Writer, data_offset: u32) -> Result<(), Error>
    where
        Writer: Write,
    {
        // on-disk format of a directory entry, in rust-ish notation
        // #[repr(C, packed)]
        // struct DirectoryEntry {
        //     /// Length of the name[] field at the end.
        //     name_len: u32,

        //     /// Length of the file in bytes. This is an exact size that is not rounded, though
        //     /// the file is always padded with zeros up to a multiple of block size (aka
        //     /// block-aligned).
        //     data_len: u32,

        //     /// Block offset where file data starts for this entry.
        //     data_off: u32,

        //     /// Pathname of the file, a UTF-8 string. It must not begin with a '/', but it may
        //     /// contain '/' separators. The string is not null-terminated. The end of the struct
        //     /// must be padded to align on a 4 byte boundary.
        //     name: [u8; self.name_len]
        // }

        let name_len = self.name.len() as u32;
        writer.write_u32::<LittleEndian>(name_len)?;
        writer.write_u32::<LittleEndian>(self.data.len() as u32)?;
        writer.write_u32::<LittleEndian>(data_offset)?;
        writer.write_all(&self.name)?;

        // align the directory entry to a 4-byte boundary
        let padding = round_up_to_align(name_len, 4) - name_len;
        for _ in 0..padding {
            writer.write_u8(0)?;
        }

        Ok(())
    }

    /// Write the data associated with this directory entry to the provided byte writer. We assume
    /// that the calling function has seeked to the expected block-aligned data location. This
    /// function fills the rest of the block with zeros, leaving the cursor position at the
    /// beginning of the next unused block.
    fn serialize_data<Writer>(&self, writer: &mut Writer) -> Result<(), Error>
    where
        Writer: Write,
    {
        writer.write_all(&self.data)?;
        block_align(writer, self.data.len() as u32)?;
        Ok(())
    }
}

async fn get_entries(dir: &fio::DirectoryProxy) -> Result<Vec<DirectoryEntry>, Error> {
    let out: Vec<DirEntry> = readdir_recursive(dir, None).map(|x| x.unwrap()).collect().await;

    let mut entries = vec![];
    for ent in out {
        if ent.kind != DirentKind::File {
            // If we run into anything in this partition that isn't a file, there is some kind of
            // problem with our environment. Surface that information so that it can get fixed.
            bail!(format!(
                "Directory entry '{}' is not a file. FactoryFS can only contain files.",
                ent.name
            ))
        }

        // TODO(sdemos): we are loading all the files we are going to serialize into memory first.
        // if the partition is too big this will be a problem.
        let (file_proxy, server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>()
            .context("failed to create fidl proxy")?;
        dir.open(
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            &ent.name,
            fidl::endpoints::ServerEnd::new(server_end.into_channel()),
        )
        .context(format!("failed to open file {}", ent.name))?;
        let (status, attrs) = file_proxy
            .get_attr()
            .await
            .context(format!("failed to get attributes of file {}: (fidl failure)", ent.name))?;
        if zx::Status::from_raw(status) != zx::Status::OK {
            bail!("failed to get attributes of file {}", ent.name);
        }
        let (status, data) = file_proxy
            .read(attrs.content_size)
            .await
            .context(format!("failed to read contents of file {}: (fidl failure)", ent.name))?;
        if zx::Status::from_raw(status) != zx::Status::OK {
            bail!("failed to get attributes of file {}", ent.name);
        }

        entries.push(DirectoryEntry { name: ent.name.as_bytes().to_vec(), data });
    }

    Ok(entries)
}

async fn write_directory<W: Write>(dir: &fio::DirectoryProxy, device: &mut W) -> Result<(), Error> {
    let entries = get_entries(dir).await.context("failed to get entries from directory")?;

    let factoryfs = FactoryFS {
        major_version: FACTORYFS_MAJOR_VERSION,
        minor_version: FACTORYFS_MINOR_VERSION,
        flags: 0,
        block_size: BLOCK_SIZE,
        entries,
    };

    factoryfs.serialize(device).context("failed to serialize factoryfs")?;

    Ok(())
}

/// Export the contents of a fuchsia.io/Directory as a flat FactoryFS partition on the provided
/// device. All files are extracted from the directory and placed in the FactoryFS partition, with a
/// name that corresponds with the complete path of the file in the original directory, relative to
/// that directory. It takes ownership of the channel to the device, which it assumes speaks
/// fuchsia.hardware.Block, and closes it after all the writes are issued to the block device.
pub async fn export_directory(dir: &fio::DirectoryProxy, device: zx::Channel) -> Result<(), Error> {
    // TODO(sdemos): for now we are taking a device as a channel, but this might change as we
    // integrate.
    let device = RemoteBlockDevice::new_sync(device)
        .context("failed to create remote block device client")?;
    let mut device = Cache::new(device).context("failed to create cache layer for block device")?;

    write_directory(dir, &mut device).await.context("failed to write out directory")?;

    // TODO(sdemos): for now we just flush the writer. for RemoteBlockDevice, that just means we
    // make sure all the write requests are sent to the disk. it's possible that we need to do more
    // than this - perhaps Seal() or some other flushing that makes sure that all the contents are
    // written to disk before we return.
    device.flush().context("failed to flush to device")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        block_align, export_directory, get_entries, num_blocks, round_up_to_block_size,
        DirectoryEntry, FactoryFS, BLOCK_SIZE, FACTORYFS_MAJOR_VERSION, FACTORYFS_MINOR_VERSION,
        SUPERBLOCK_DATA_SIZE,
    };

    use {
        fidl::endpoints,
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        matches::assert_matches,
        ramdevice_client::RamdiskClient,
        vfs::{
            directory::entry::DirectoryEntry as _, execution_scope::ExecutionScope,
            file::pcb::read_only_static, pseudo_directory,
        },
    };

    #[test]
    fn test_num_blocks() {
        assert_eq!(num_blocks(0), 0);
        assert_eq!(num_blocks(1), 1);
        assert_eq!(num_blocks(10), 1);
        assert_eq!(num_blocks(BLOCK_SIZE - 1), 1);
        assert_eq!(num_blocks(BLOCK_SIZE), 1);
        assert_eq!(num_blocks(BLOCK_SIZE + 1), 2);
    }

    #[test]
    fn test_round_up() {
        assert_eq!(round_up_to_block_size(0), 0);
        assert_eq!(round_up_to_block_size(1), BLOCK_SIZE);
        assert_eq!(round_up_to_block_size(BLOCK_SIZE - 1), BLOCK_SIZE);
        assert_eq!(round_up_to_block_size(BLOCK_SIZE), BLOCK_SIZE);
        assert_eq!(round_up_to_block_size(BLOCK_SIZE + 1), BLOCK_SIZE * 2);
    }

    #[test]
    fn test_block_align() {
        let mut cases = vec![
            // (bytes already written, pad bytes to block align)
            (0, 0),
            (1, BLOCK_SIZE - 1),
            (BLOCK_SIZE - 1, 1),
            (BLOCK_SIZE, 0),
            (BLOCK_SIZE + 1, BLOCK_SIZE - 1),
        ];

        for case in &mut cases {
            let mut w = vec![];
            assert_matches!(block_align(&mut w, case.0), Ok(()));
            assert_eq!(w.len(), case.1 as usize);
            assert!(w.into_iter().all(|v| v == 0));
        }
    }

    #[test]
    fn test_superblock_data() {
        let name = "test_name".as_bytes();
        let data = vec![1, 2, 3, 4, 5];

        let entry = DirectoryEntry { name: name.to_owned(), data: data.clone() };

        let metadata_size = entry.metadata_size();
        let metadata_blocks = num_blocks(metadata_size);

        let factoryfs = FactoryFS {
            major_version: FACTORYFS_MAJOR_VERSION,
            minor_version: FACTORYFS_MINOR_VERSION,
            flags: 0,
            block_size: BLOCK_SIZE,
            entries: vec![entry],
        };

        let mut out = vec![];
        assert_eq!(
            factoryfs.serialize_superblock(&mut out).unwrap(),
            (metadata_size, metadata_blocks),
        );

        assert_eq!(out.len() as u32, SUPERBLOCK_DATA_SIZE);
    }

    #[test]
    fn test_dirent_metadata() {
        let data_offset = 12;
        let data = vec![1, 2, 3, 4, 5];

        let mut out: Vec<u8> = vec![];
        let name = "test_name".as_bytes();
        let dirent = DirectoryEntry { name: name.to_owned(), data };

        assert_matches!(dirent.serialize_metadata(&mut out, data_offset), Ok(()));
        assert_eq!(dirent.metadata_size(), out.len() as u32);

        // TODO(sdemos): check more of the output
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_export() {
        let dir = pseudo_directory! {
            "a" => read_only_static("a content"),
            "b" => pseudo_directory! {
                "c" => read_only_static("c content"),
            },
        };
        let (dir_proxy, dir_server) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
        dir.open(
            scope,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::empty(),
            endpoints::ServerEnd::new(dir_server.into_channel()),
        );

        isolated_driver_manager::launch_isolated_driver_manager().unwrap();
        ramdevice_client::wait_for_device("/dev/misc/ramctl", std::time::Duration::from_secs(10))
            .unwrap();
        let ramdisk = RamdiskClient::create(512, 1 << 16).unwrap();
        let channel = ramdisk.open().unwrap();

        assert_matches!(export_directory(&dir_proxy, channel).await, Ok(()));

        // TODO(sdemos): test should check the ramdisk for at least a bit of the expected output
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_entries() {
        let dir = pseudo_directory! {
            "a" => read_only_static("a content"),
            "b" => pseudo_directory! {
                "c" => read_only_static("c content"),
            },
        };
        let (dir_proxy, dir_server) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
        dir.open(
            scope,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::empty(),
            endpoints::ServerEnd::new(dir_server.into_channel()),
        );

        let entries = get_entries(&dir_proxy).await.unwrap();

        assert_eq!(
            entries,
            vec![
                DirectoryEntry { name: b"a".to_vec(), data: b"a content".to_vec() },
                DirectoryEntry { name: b"b/c".to_vec(), data: b"c content".to_vec() },
            ],
        );
    }
}
