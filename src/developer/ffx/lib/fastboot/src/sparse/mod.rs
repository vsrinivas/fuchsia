// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    bytes::{Bytes, BytesMut},
    core::fmt,
    futures::{AsyncReadExt, AsyncWriteExt},
    serde::Serialize,
    std::io::Write,
    std::mem,
    std::path::Path,
    tempfile::{NamedTempFile, TempPath},
};

/// `SparseHeader` represents the header section of a `SparseFile`
#[derive(Serialize)]
#[repr(C)]
struct SparseHeader {
    /// Magic Number.
    magic: u32,
    /// Highest Major Version number supported.
    major_version: u16,
    /// Lowest Minor Version number supported.
    minor_version: u16,
    /// Size of the Header. (Defaults to 0)
    file_hdr_sz: u16,
    /// Size of the Header per-chunk. (Defaults to 0)
    chunk_hdr_sz: u16,
    /// Size of each block (Defaults to 4096)
    blk_sz: u32,
    /// Total number of blocks in the output image
    total_blks: u32,
    /// Total number of chunks.
    total_chunks: u32,
    /// Image Checksum... unused
    image_checksum: u32,
}

impl fmt::Display for SparseHeader {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            r"SparseHeader:
magic:          {:#X}
major_version:  {}
minor_version:  {}
file_hdr_sz:    {}
chunk_hdr_sz:   {}
blk_sz:         {}
total_blks:     {}
total_chunks:   {}
image_checksum: {}",
            self.magic,
            self.major_version,
            self.minor_version,
            self.file_hdr_sz,
            self.chunk_hdr_sz,
            self.blk_sz,
            self.total_blks,
            self.total_chunks,
            self.image_checksum
        )
    }
}

impl SparseHeader {
    fn new(
        magic: u32,
        major_version: u16,
        minor_version: u16,
        file_hdr_sz: u16,
        chunk_hdr_sz: u16,
        blk_sz: u32,
        total_blks: u32,
        total_chunks: u32,
        image_checksum: u32,
    ) -> SparseHeader {
        SparseHeader {
            magic,
            major_version,
            minor_version,
            file_hdr_sz,
            chunk_hdr_sz,
            blk_sz,
            total_blks,
            total_chunks,
            image_checksum,
        }
    }
}

#[derive(Clone, PartialEq, Debug)]
enum Chunk<'a> {
    /// `Raw` represents a set of bytes to be written to disk as-is.
    Raw(Vec<&'a [u8]>),
    /// Represents a Chunk that has the `value` repeated `blocks` times
    Fill(u32, usize), // Value, Size
    /// `DontCare` represents a set of blocks that need to be "offset" by the
    /// image recipeint. If an image needs to be broken up into two sparse images,
    /// and we flash n bytes for Sparse Image 1, Sparse Image 2
    /// needs to start with a DontCareChunk with (n/blocksize) blocks as its "size" property.
    DontCare(usize), // Size
    /// `Crc32Chunk` is used as a checksum of a given set of Chunks for a SparseImage.
    /// This is not required and unused in most implementations of the Sparse Image
    /// format. The type is included for completeness. It has 4 bytes of CRC32 checksum
    /// as describable in a u32.
    #[allow(dead_code)]
    Crc32(u32),
}

impl Chunk<'_> {
    /// Return number of blocks the chunk expands to when written to the partition.
    fn output_blocks(&self) -> u32 {
        match self {
            Self::Raw(raw) => {
                ((raw.iter().map(|x| x.len()).sum::<usize>() + BLK_SIZE - 1) / BLK_SIZE) as u32
            }
            Self::Fill(_value, size) => (*size / BLK_SIZE) as u32,
            Self::DontCare(size) => *size as u32,
            Self::Crc32(_) => 0,
        }
    }

    /// `chunk_type` returns the integer flag to represent the type of chunk
    /// to use in the ChunkHeader
    fn chunk_type(&self) -> u16 {
        match self {
            Self::Raw(_) => 0xCAC1,
            Self::Fill(_, _) => 0xCAC2,
            Self::DontCare(_) => 0xCAC3,
            Self::Crc32(_) => 0xCAC4,
        }
    }

    /// `chunk_data_len` returns the length of the chunk's header plus the
    /// length of the data when serialized
    fn chunk_data_len(&self) -> usize {
        let header_size = mem::size_of::<ChunkHeader>();
        let data_size = match self {
            Self::Raw(raw) => raw.iter().map(|x| x.len()).sum(),
            Self::Fill(_, _) => mem::size_of::<u32>(),
            Self::DontCare(_) => 0,
            Self::Crc32(_) => mem::size_of::<u32>(),
        };
        header_size + data_size
    }

    // TODO(colnnelson): have this take an `impl Writer` and just write the
    // bytes directly

    /// Returns the bytes representing the chunk data structure in the sparse structure.
    fn chunk_data(&self) -> Result<Bytes> {
        let header = ChunkHeader::new(
            self.chunk_type(),
            0x0,
            self.output_blocks(),
            self.chunk_data_len() as u32,
        );

        let mut output = BytesMut::with_capacity(self.chunk_data_len());
        let header_bytes: Vec<u8> = bincode::serialize(&header)?;
        output.extend_from_slice(&header_bytes);

        match self {
            Self::Raw(raw) => {
                raw.iter().for_each(|r| output.extend_from_slice(&r));
                // If our data is not an even multiple of BLK_SIZE, we need to
                // pad it with 0. Make a 0'd out Bytes and write the subset
                output.resize(self.chunk_data_len(), 0);
            }
            Self::Fill(value, _) => {
                let data = bincode::serialize(value)?;
                output.extend_from_slice(&data);
            }
            Self::DontCare(_) => {
                // DontCare has no data to write
            }
            Self::Crc32(checksum) => {
                let checksum_bytes = bincode::serialize(checksum)?;
                output.extend_from_slice(&checksum_bytes);
            }
        }
        Ok(output.freeze())
    }

    /// Returns the expected bytes written to the partition
    fn partition_data(&self) -> Bytes {
        match self {
            Self::Raw(raw) => {
                let mut buf = BytesMut::new();
                raw.iter().for_each(|x| buf.extend_from_slice(x));
                buf.freeze()
            }
            Self::Fill(byte, size) => {
                let ser = bincode::serialize(byte).expect("can serialize u32");
                Bytes::copy_from_slice(&ser.repeat(*size))
            }
            Self::DontCare(size) => {
                // Dont care chunks are treated as zeroes
                Bytes::copy_from_slice(&[0x0].repeat(*size * BLK_SIZE))
            }
            Self::Crc32(_) => {
                // No data
                Bytes::new()
            }
        }
    }
}

impl TryFrom<&Chunk<'_>> for Vec<u8> {
    type Error = anyhow::Error;

    fn try_from(var: &Chunk<'_>) -> Result<Self, Self::Error> {
        let data = var.chunk_data()?;
        Ok(data.to_vec())
    }
}

impl fmt::Display for Chunk<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::Raw(raw) => {
                format!("RawChunk: total bytes: {}", raw.iter().map(|x| x.len()).sum::<usize>())
            }
            Self::Fill(byte, size) => {
                format!("FillChunk: value: {}, n_blocks: {}", byte, size)
            }
            Self::DontCare(size) => {
                format!("DontCareChunk: n_blocks: {}", size)
            }
            Self::Crc32(checksum) => format!("Crc32Chunk: checksum: {:?}", checksum),
        };
        write!(f, "{}", message)
    }
}

/// `ChunkHeader` represents the header portion of a Chunk.
#[derive(Debug, Serialize)]
#[repr(C)]
struct ChunkHeader {
    chunk_type: u16,
    reserved1: u16,
    chunk_sz: u32,
    total_sz: u32,
}

impl ChunkHeader {
    fn new(chunk_type: u16, reserved1: u16, chunk_sz: u32, total_sz: u32) -> ChunkHeader {
        ChunkHeader { chunk_type, reserved1, chunk_sz, total_sz }
    }
}

impl fmt::Display for ChunkHeader {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "\tchunk_type: {:#X}\n\treserved1: {}\n\tchunk_sz: {}\n\ttotal_sz: {}\n",
            self.chunk_type, self.reserved1, self.chunk_sz, self.total_sz
        )
    }
}

/// Size of blocks to write
const BLK_SIZE: usize = 0x1000;

// Header constants.
const MAGIC: u32 = 0xED26FF3A;
/// Maximum Major Version Supported.
const MAJOR_VERSION: u16 = 0x1;
// Minimum Minor Version Supported.
const MINOR_VERSION: u16 = 0x0;
/// The Checksum... hardcoded not used.
const CHECKSUM: u32 = 0xCAFED00D;

/// `SparseFile` Represents an Image file in the Sparse Image format.
/// It is made up of a set of Chunks.
#[derive(Clone, Debug, PartialEq)]
#[repr(C)]
struct SparseFile<'a> {
    chunks: Vec<Chunk<'a>>,
}

impl<'a> SparseFile<'a> {
    fn new(chunks: Vec<Chunk<'a>>) -> SparseFile<'a> {
        SparseFile { chunks }
    }

    fn total_blocks(&self) -> u32 {
        self.chunks.iter().map(|c| c.output_blocks()).sum()
    }

    /// Returns the Byte representation of the SparseFile. Suitable to be
    /// written to disk.
    fn sparse_file_data(&self) -> Result<Bytes> {
        // let mut data = Vec::<u8>::new();
        let mut data = BytesMut::new();
        let header = SparseHeader::new(
            MAGIC,
            MAJOR_VERSION,
            MINOR_VERSION,
            mem::size_of::<SparseHeader>() as u16, // File header size
            mem::size_of::<ChunkHeader>() as u16,  // chunk header size
            BLK_SIZE.try_into().unwrap(),          // Size of the blocks
            self.total_blocks(),                   // Total blocks in this image
            self.chunks.len().try_into().unwrap(), // Total chunks in this image
            CHECKSUM,                              // Checksum verification unused
        );

        tracing::trace!("Created SparseFile header {}", header);

        let header_bytes: Vec<u8> = bincode::serialize(&header)?;
        data.extend_from_slice(&header_bytes);

        for chunk in &self.chunks {
            // Get all chunks... and then extend the data with them
            data.extend_from_slice(&Vec::<u8>::try_from(chunk)?);
        }

        Ok(data.freeze())
    }

    /// Return the expected bytes from writing the sparse image to a partition.
    /// All fill chunks are expanded with their fill value
    /// and all don't care chunks are expanded with zeroes.
    #[allow(dead_code)]
    fn expected_partition_data(&self) -> Result<Bytes> {
        let mut data = Vec::new();

        self.chunks.iter().for_each(|c| data.extend_from_slice(&c.partition_data()));
        Ok(Bytes::copy_from_slice(&data))
    }
}

impl fmt::Display for SparseFile<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, r"SparseFile: {} Chunks:", self.chunks.len())
    }
}

impl TryFrom<SparseFile<'_>> for Vec<u8> {
    type Error = anyhow::Error;

    fn try_from(var: SparseFile<'_>) -> Result<Self, Self::Error> {
        let data = var.sparse_file_data()?;
        Ok(data.to_vec())
    }
}

/// `add_sparse_chunk` takes the input vec, v and given `Chunk`, chunk, and
/// attempts to add the chunk to the end of the vec. If the current last chunk
/// is the same kind of Chunk as the `chunk`, then it will merge the two chunks
/// into one chunk.
///
/// Example: A `FillChunk` with value 0 and size 1 is the last chunk
/// in `v`, and `chunk` is a FillChunk with value 0 and size 1, the returned
/// `Vec`'s last element will be a FillChunk with value 0 and size 2.
fn add_sparse_chunk<'a>(r: &Vec<Chunk<'a>>, chunk: Chunk<'a>) -> Result<Vec<Chunk<'a>>> {
    let mut ret = r.clone();
    match ret.pop() {
        // We've got something in the Vec... if they are both the same type,
        // merge them, otherwise, just push the new one
        Some(last) => match (&last, &chunk) {
            (Chunk::Raw(last_data), Chunk::Raw(new_data)) => {
                let mut data = last_data.clone();
                new_data.iter().for_each(|c| data.push(c));
                ret.push(Chunk::Raw(data.to_vec()));
            }
            (Chunk::Fill(value, size), Chunk::Fill(new_value, new_size)) if value == new_value => {
                ret.push(Chunk::Fill(*value, size + new_size));
            }
            (Chunk::DontCare(size), Chunk::DontCare(new_size)) => {
                ret.push(Chunk::DontCare(size + new_size));
            }
            _ => {
                // If the chunk types differ they cannot be merged.
                // If they are both Fill but have different values, they cannot be merged.
                // Crc32 cannot be merged.
                ret.push(last);
                ret.push(chunk);
            }
        },
        None => {
            // Dont have any chunks... add it
            ret.push(chunk);
        }
    }

    Ok(ret)
}

/// `resparse` takes a SparseFile and a maximum size and will
/// break the single SparseFile into multiple SparseFiles whose
/// size will not exceed the maximum_download_size.
///
/// This will return an error if max_download_size is <= BLK_SIZE
fn resparse<'a>(
    sparse_file: SparseFile<'a>,
    max_download_size: u32,
) -> Result<Vec<SparseFile<'a>>> {
    if max_download_size as usize <= BLK_SIZE {
        anyhow::bail!(
            "Given maximum download size ({}) is less than the block size ({})",
            max_download_size,
            BLK_SIZE
        );
    }
    let mut ret = Vec::<SparseFile<'a>>::new();

    // File length already starts with a header for the SparseFile as
    // well as the size of a potential DontCare and Crc32 Chunk
    let sunk_file_length = mem::size_of::<SparseHeader>()
        + (Chunk::DontCare(1).chunk_data_len() + Chunk::Crc32(2345).chunk_data_len());

    let mut chunk_pos = 0;
    while chunk_pos < sparse_file.chunks.len() {
        tracing::trace!("Starting a new file at chunk position: {}", chunk_pos);

        let mut file_len = 0;
        file_len += sunk_file_length;

        let mut chunks = Vec::<Chunk<'a>>::new();
        if chunk_pos > 0 {
            // If we already have some chunks... add a DontCare block to
            // move the pointer
            tracing::trace!("Adding a DontCare chunk offset: {}", chunk_pos);
            let dont_care = Chunk::DontCare(chunk_pos);
            chunks.push(dont_care);
        }

        loop {
            match sparse_file.chunks.get(chunk_pos) {
                Some(chunk) => {
                    let curr_chunk_data_len = chunk.chunk_data_len();
                    if (file_len + curr_chunk_data_len) as u32 > max_download_size {
                        tracing::trace!("Current file size is: {} and adding another chunk of len: {} would put us over our max: {}", file_len, curr_chunk_data_len, max_download_size);

                        // Add a dont care chunk to do the last offset.
                        // While this is not strictly speaking needed, other tools
                        // (simg2simg) produce this chunk, and the Sparse image inspection tool
                        // simg_dump will produce a warning if a sparse file does not have the same
                        // number of output blocks as declared in the header.
                        let remainder_chunks = sparse_file.chunks.len() - chunk_pos;
                        let dont_care = Chunk::DontCare(remainder_chunks);
                        chunks.push(dont_care);
                        break;
                    }
                    tracing::trace!("chunk: {} curr_chunk_data_len: {} current file size: {} max_download_size: {} diff: {}", chunk_pos, curr_chunk_data_len, file_len, max_download_size, (max_download_size as usize - file_len - curr_chunk_data_len) );
                    chunks = add_sparse_chunk(&chunks, chunk.clone())?;
                    file_len += curr_chunk_data_len;
                    chunk_pos = chunk_pos + 1;
                }
                None => {
                    tracing::trace!("Finished iterating chunks");
                    break;
                }
            }
        }
        let resparsed = SparseFile::new(chunks);
        tracing::trace!("resparse: Adding new SparseFile: {}", resparsed);
        ret.push(resparsed);
    }

    Ok(ret)
}

/// Takes the given `file_to_upload` for the `named` partition and creates a
/// set of temporary files in the given `dir` in Sparse Image Format. With the
/// provided `max_download_size` constraining file size.
///
/// # Arguments
///
/// * `writer` - Used for writing log information.
/// * `name` - Name of the partition the image. Used for logs only.
/// * `file_to_upload` - Path to the file to translate to sparse image format.
/// * `dir` - Path to write the Sparse file(s).
/// * `max_download_size` - Maximum size that can be downloaded by the device.
pub async fn build_sparse_files<W: Write>(
    writer: &mut W,
    name: &str,
    file_to_upload: &str,
    dir: &Path,
    max_download_size: u32,
) -> Result<Vec<TempPath>> {
    writeln!(writer, "Building sparse files for: {}. File: {}", name, file_to_upload)?;
    let mut in_file = async_fs::File::open(file_to_upload).await?;
    let mut in_file_bytes: Vec<u8> = Vec::<u8>::new();
    in_file.read_to_end(&mut in_file_bytes).await?;

    tracing::trace!("File: {}. Read in {} bytes", file_to_upload, in_file_bytes.len());

    let mut chunks = Vec::<Chunk<'_>>::new();

    for input_block in in_file_bytes.chunks(BLK_SIZE) {
        // If all the values in the vec are the same, we need to make a fill block
        let is_fill =
            input_block.chunks(4).collect::<Vec<&[u8]>>().windows(2).all(|w| w[0] == w[1]);

        if is_fill {
            let value: u32 = bincode::deserialize(&input_block[0..4])?;
            // Add a fill chunk
            let fill = Chunk::Fill(value, input_block.len());
            tracing::trace!("Sparsing file: {}. Created: {}", file_to_upload, fill);
            chunks.push(fill);
        } else {
            // Add a raw chunk
            let raw = Chunk::Raw([input_block].to_vec());
            tracing::trace!("Sparsing file: {}. Created: {}", file_to_upload, raw);
            chunks.push(raw);
        }
    }
    tracing::trace!("Creating sparse file from: {} chunks", chunks.len());

    let sparse_file = SparseFile::new(chunks);
    tracing::trace!("Created sparse file: {}", sparse_file);

    let mut ret = Vec::<TempPath>::new();
    tracing::trace!("Resparsing sparse file");
    for re_sparsed_file in resparse(sparse_file, max_download_size)? {
        let (file, temp_path) = NamedTempFile::new_in(dir)?.into_parts();
        let mut file_create = async_fs::File::from(file);

        tracing::trace!("Writing resparsed {} to disk", re_sparsed_file);
        let bytes = Vec::<u8>::try_from(re_sparsed_file)?;
        file_create.write_all(&bytes).await?;
        file_create.flush().await?;

        ret.push(temp_path);
    }

    writeln!(writer, "Finished building sparse files")?;

    Ok(ret)
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::test::setup;
    use rand::rngs::SmallRng;
    use rand::{RngCore, SeedableRng};
    use std::io;
    use std::process::Command;

    const WANT_RAW_BYTES: [u8; 17] = [193, 202, 0, 0, 1, 0, 0, 0, 17, 0, 0, 0, 49, 50, 51, 52, 53];

    const WANT_SPARSE_FILE_RAW_BYTES: [u8; 71] = [
        58, 255, 38, 237, 1, 0, 0, 0, 28, 0, 12, 0, 0, 16, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 13, 208,
        254, 202, 194, 202, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 5, 0, 0, 0, 193, 202, 0, 0, 1, 0, 0, 0,
        15, 0, 0, 0, 49, 50, 51, 195, 202, 0, 0, 1, 0, 0, 0, 12, 0, 0, 0,
    ];

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fill_partition_data() -> Result<()> {
        let (_, _) = setup();
        let fill_chunk = Chunk::Fill(3, 5);
        let output = fill_chunk.partition_data();
        assert_eq!(output, Bytes::from(&b"\x03\0\0\0\x03\0\0\0\x03\0\0\0\x03\0\0\0\x03\0\0\0"[..]));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fill_into_bytes() -> Result<()> {
        let (_, _) = setup();
        let fill_chunk = Chunk::Fill(365, 5);
        let output = Vec::<u8>::try_from(&fill_chunk)?;
        assert_eq!(output, [194, 202, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 109, 1, 0, 0]);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_raw_partition_data() -> Result<()> {
        let data = [&b"12345"[..]].to_vec();
        let chunk = Chunk::Raw(data);
        let output = chunk.partition_data();
        assert_eq!(output, Bytes::from(&b"12345"[..]));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_raw_into_bytes() -> Result<()> {
        let chunk = Chunk::Raw([&b"12345"[..]].to_vec());
        let output = Vec::<u8>::try_from(&chunk)?;
        assert_eq!(output, WANT_RAW_BYTES);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dont_care_partition_data() -> Result<()> {
        let chunk = Chunk::DontCare(5);
        let output = chunk.partition_data();
        let mut want = String::new();
        want.push_str("\0".repeat(5 * BLK_SIZE).as_str());
        assert_eq!(output, Bytes::copy_from_slice(&want.as_bytes()));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_dont_care_into_bytes() -> Result<()> {
        let chunk = Chunk::DontCare(5);
        let output = Vec::<u8>::try_from(&chunk)?;
        assert_eq!(output, [195, 202, 0, 0, 5, 0, 0, 0, 12, 0, 0, 0]);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_sparse_file_partition_data() -> Result<()> {
        let mut chunks = Vec::<Chunk<'_>>::new();
        // Add a fill chunk
        let fill = Chunk::Fill(5, 1);
        chunks.push(fill);
        // Add a raw chunk
        let raw = Chunk::Raw([&b"123"[..]].to_vec());
        chunks.push(raw);
        // Add a dontcare chunk
        let dontcare = Chunk::DontCare(1);
        chunks.push(dontcare);

        let sparsefile = SparseFile::new(chunks);

        let output = sparsefile.expected_partition_data()?;
        let mut want = String::new();
        want.push_str("\x05\0\0\0123");
        want.push_str("\0".repeat(BLK_SIZE).as_str());
        assert_eq!(output, Bytes::copy_from_slice(&want.as_bytes()));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_sparse_file_into_bytes() -> Result<()> {
        let mut chunks = Vec::<Chunk<'_>>::new();
        // Add a fill chunk
        let fill = Chunk::Fill(5, 1);
        chunks.push(fill);
        // Add a raw chunk
        let raw = Chunk::Raw([&b"123"[..]].to_vec());
        chunks.push(raw);
        // Add a dontcare chunk
        let dontcare = Chunk::DontCare(1);
        chunks.push(dontcare);

        let sparsefile = SparseFile::new(chunks);

        let output = Vec::<u8>::try_from(sparsefile)?;
        assert_eq!(output, WANT_SPARSE_FILE_RAW_BYTES);
        Ok(())
    }

    ////////////////////////////////////////////////////////////////////////////
    // Tests for resparse

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resparse_bails_on_too_small_size() -> Result<()> {
        let sparse = SparseFile::new(Vec::<Chunk<'_>>::new());
        assert!(resparse(sparse, 4095).is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_resparse_splits() -> Result<()> {
        let max_download_size = 4096 * 2;
        let fourtynintysixones = &b"1"[..].repeat(4096);
        let temp_bytes = Chunk::Raw([fourtynintysixones.as_slice()].to_vec());

        let mut chunks = Vec::<Chunk<'_>>::new();
        chunks.push(temp_bytes.clone());
        chunks.push(Chunk::Fill(2, 4));
        // We want 2 sparse files with the second sparse file having a
        // DontCare chunk and then this chunk
        chunks.push(temp_bytes.clone());

        let input_sparse_file = SparseFile::new(chunks);
        let resparsed_files = resparse(input_sparse_file, max_download_size)?;
        assert_eq!(2, resparsed_files.len());

        // Make assertions about the first resparsed file
        assert_eq!(3, resparsed_files[0].chunks.len());
        assert_eq!(
            Chunk::Raw([b"1"[..].repeat(4096).as_slice()].to_vec()),
            resparsed_files[0].chunks[0]
        );
        assert_eq!(Chunk::Fill(2, 4), resparsed_files[0].chunks[1]);
        assert_eq!(Chunk::DontCare(1), resparsed_files[0].chunks[2]);

        // Make assertsion about the second resparsed file
        assert_eq!(2, resparsed_files[1].chunks.len());
        assert_eq!(Chunk::DontCare(2), resparsed_files[1].chunks[0]);
        assert_eq!(
            Chunk::Raw([b"1"[..].repeat(4096).as_slice()].to_vec()),
            resparsed_files[1].chunks[1]
        );
        Ok(())
    }

    ////////////////////////////////////////////////////////////////////////////
    // Tests for add_sparse_chunk

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_sparse_chunk_adds_empty() -> Result<()> {
        let init_vec = Vec::<Chunk<'_>>::new();
        let res = add_sparse_chunk(&init_vec, Chunk::Fill(0, 1))?;
        assert_eq!(0, init_vec.len());
        assert_ne!(init_vec, res);
        assert_eq!(Chunk::Fill(0, 1), res[0]);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_sparse_chunk_fill() -> Result<()> {
        // Test merge
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Fill(0, 1));
            let res = add_sparse_chunk(&init_vec, Chunk::Fill(0, 1))?;
            assert_eq!(1, res.len());
            assert_eq!(Chunk::Fill(0, 2), res[0]);
        }

        // Test dont merge on different value
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Fill(0, 1));
            let res = add_sparse_chunk(&init_vec, Chunk::Fill(1, 1))?;
            assert_ne!(res, init_vec);
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::Fill(0, 1), Chunk::Fill(1, 1)]);
        }

        // Test dont merge on different type
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Fill(0, 1));
            let res = add_sparse_chunk(&init_vec, Chunk::DontCare(1))?;
            assert_ne!(res, init_vec);
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::Fill(0, 1), Chunk::DontCare(1)]);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_sparse_chunk_dont_care() -> Result<()> {
        // Test they merge
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::DontCare(1));
            let res = add_sparse_chunk(&init_vec, Chunk::DontCare(1))?;
            assert_eq!(1, res.len());
            assert_eq!(Chunk::DontCare(2), res[0]);
        }

        // Test they dont merge on different type
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::DontCare(1));
            let res = add_sparse_chunk(&init_vec, Chunk::Fill(1, 1))?;
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::DontCare(1), Chunk::Fill(1, 1)]);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_sparse_chunk_raw() -> Result<()> {
        // Test they merge
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Raw([&b"foo"[..]].to_vec()));
            let res = add_sparse_chunk(&init_vec, Chunk::Raw([&b"bar"[..]].to_vec()))?;
            assert_eq!(1, res.len());
            assert_eq!(Chunk::Raw([&b"foo"[..], &b"bar"[..]].to_vec()), res[0]);
        }

        // Test they dont merge on different type
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Raw([&b"foo"[..]].to_vec()));
            let res = add_sparse_chunk(&init_vec, Chunk::Fill(1, 1))?;
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::Raw([&b"foo"[..]].to_vec()), Chunk::Fill(1, 1)]);
        }

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_sparse_chunk_crc32() -> Result<()> {
        // Test they dont merge on same type (Crc32 is special)
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Crc32(1234));
            let res = add_sparse_chunk(&init_vec, Chunk::Crc32(2345))?;
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::Crc32(1234), Chunk::Crc32(2345)]);
        }

        // Test they dont merge on different type
        {
            let mut init_vec = Vec::<Chunk<'_>>::new();
            init_vec.push(Chunk::Crc32(1234));
            let res = add_sparse_chunk(&init_vec, Chunk::Fill(1, 1))?;
            assert_eq!(2, res.len());
            assert_eq!(res, [Chunk::Crc32(1234), Chunk::Fill(1, 1)]);
        }

        Ok(())
    }

    ////////////////////////////////////////////////////////////////////////////
    // Integration
    //

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_roundtrip() -> Result<()> {
        // TODO(colnnelson): Add the simg2img binary to our test rollers
        if !cfg!(fastboot_integration) {
            return Ok(());
        }

        let stdout = io::stdout();
        let mut handle = stdout.lock();

        // Generate a large temporary file
        let (file, temp_path) = NamedTempFile::new()?.into_parts();
        let mut file_create = async_fs::File::from(file);

        // Fill buffer and file with random data
        let mut rng = SmallRng::from_entropy();
        let mut buf = Vec::<u8>::new();
        buf.resize(4096 * 100, 0);
        rng.fill_bytes(&mut buf);

        file_create.write_all(&buf).await?;
        file_create.flush().await?;

        // build sparse files for it
        let files = build_sparse_files(
            &mut handle,
            "test",
            temp_path.to_str().unwrap(),
            std::env::temp_dir().as_path(),
            4096 * 2,
        )
        .await?;

        // Use simg2img to stitch them back together
        let (out_file, out_path) = NamedTempFile::new()?.into_parts();
        let mut out_create = async_fs::File::from(out_file);

        // Add the files to the command args
        let mut args = Vec::<&str>::new();
        files.iter().for_each(|f| args.push(f.to_str().unwrap()));
        // Add the output file name
        args.push(out_path.to_str().unwrap());

        Command::new("simg2img")
            .args(args)
            .output()
            .expect("failed to stitch images back together");

        // Compare
        let mut stitched_file_bytes: Vec<u8> = Vec::<u8>::new();
        out_create.read_to_end(&mut stitched_file_bytes).await?;
        assert_eq!(stitched_file_bytes, buf);
        Ok(())
    }
}
