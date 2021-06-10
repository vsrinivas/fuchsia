// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::Error,
        extent::Extent,
        extent_cluster::ExtentCluster,
        format::{ExtentClusterHeader, ExtentInfo, Header},
        options::ExtractorOptions,
        properties::{DataKind, ExtentProperties},
        utils::{RangeOps, ReadAndSeek},
        ExtentKind,
    },
    flate2::{write::GzEncoder, Compression},
    std::{
        convert::TryFrom,
        fmt,
        fs::File,
        io::{Seek, Write},
        ops::Range,
    },
};

enum Streamer {
    UncompressedStream(Box<dyn Write>),
    CompressedStream(GzEncoder<Box<dyn Write>>),
}

/// `Extractor` helps to extract disk images.
///
/// Extractor works with storage software like filesystems, fvm, etc
/// to dump data of interest to a image file, which can be used to
/// debug storage issues.
///
/// Storage software tells what [`Extent`]s are useful adding data location
/// <start, lenght> and properties. Extractor maintains a list of added extents
/// and writes to the image file on calling [`write`].
///
/// # Example
///
/// ```
/// use extractor_lib::extractor::{Extractor, ExtractorOptions};
///
/// let options: ExtractorOptions = Default::default();
/// let mut extractor = Extractor::new(in_file, options, out_file);
/// extractor.add(10..11, default_properties(), None).unwrap();
/// extractor.add(12..14, default_properties(), None).unwrap();
/// extractor.write().unwrap();
/// ```
pub struct Extractor {
    streamer: Streamer,
    in_stream: Box<dyn ReadAndSeek>,
    options: ExtractorOptions,
    extent_cluster: ExtentCluster,
    current_offset: u64,
    write_header: bool,
}

impl fmt::Debug for Extractor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Extractor").field("extent_cluster", &self.extent_cluster).finish()
    }
}

impl Extractor {
    /// Creates a new Extractor.
    ///
    /// Data to be extracted is read from in_stream and extracted image is
    /// written to out_stream.
    /// in_stream needs to be `Seek`able as only a portion of in_stream may be
    /// read.
    ///
    /// Operations performed on in_stream and out_stream are byte granular.
    /// Extractor may not perform `alignment` granular operations.
    pub fn new(
        in_stream: Box<dyn ReadAndSeek>,
        options: ExtractorOptions,
        out_stream: Box<dyn Write>,
    ) -> Extractor {
        let cluster = ExtentCluster::new(&options);
        Extractor {
            streamer: match options.compress {
                true => {
                    Streamer::CompressedStream(GzEncoder::new(out_stream, Compression::default()))
                }
                false => Streamer::UncompressedStream(out_stream),
            },
            in_stream: in_stream,
            options: options,
            extent_cluster: cluster,
            current_offset: 0,
            write_header: true,
        }
    }

    /// Adds an extent to extractor.
    ///
    /// `Add` can lead to one of the following
    ///  * Create a new extent
    ///  * Replace an existing extent - because of higher priority
    ///  * Gets dropped by an existing extent - because of lower priority
    ///  * Merge into existing extent, because properties are the same and
    ///    + new extent overlaps with existing extent - (10..20) and (15, 25)
    ///    + nex extent is adjacent to an existing extent - (10..20) and (20..30)
    ///  * Split and existing entry because new extent has higher priority.
    ///  * Existing extent splits the new extent beause existing extent has higher priority.
    /// For all the above cases, `add` returns success.
    ///
    /// See [`ExtentProperties`] for how priority is decided.
    ///
    /// Note: Adding data and/or extent propertes with DataKind as Modified is
    /// yet to be implmented.
    pub fn add(
        &mut self,
        range: Range<u64>,
        mut properties: ExtentProperties,
        data: Option<Box<[u8]>>,
    ) -> Result<(), Error> {
        if !range.is_valid() {
            return Err(Error::InvalidRange);
        }
        if (range.length() % self.options.alignment != 0)
            || (range.start % self.options.alignment != 0)
        {
            return Err(Error::InvalidArgument);
        }
        match data {
            Some(_) => {
                todo!("adding data is not yet implemented");
            }
            None => {}
        }
        if properties.data_kind == DataKind::Modified {
            todo!("adding modified data is not yet implemented");
        }

        // Skip dumping pii if we are not asked to dump pii.
        if !self.options.force_dump_pii && properties.extent_kind == ExtentKind::Pii {
            properties.data_kind = DataKind::Skipped;
        }

        let extent = Extent::new(range, properties, data)?;
        self.extent_cluster.add_extent(&extent)
    }

    /// Writes all pending extents and their data to the out_stream.
    pub fn write(&mut self) -> Result<u64, Error> {
        let mut bytes_written = 0;
        let stream: &mut dyn Write = match &mut self.streamer {
            Streamer::UncompressedStream(o) => o,
            Streamer::CompressedStream(c) => c,
        };
        if self.write_header {
            assert_eq!(self.current_offset, 0);
            let mut header = Header::new(self.options.alignment);
            bytes_written = header.serialize_to(stream)?;
            self.current_offset = bytes_written;
            self.write_header = false;
        }
        bytes_written = bytes_written
            + self.extent_cluster.write(&mut self.in_stream, self.current_offset, true, stream)?;
        stream.flush().map_err(|_| Error::WriteFailed)?;
        self.current_offset = self.current_offset + bytes_written;
        match &mut self.streamer {
            Streamer::CompressedStream(c) => {
                c.try_finish().map_err(|_| Error::WriteFailed)?;
                c.get_mut().flush().map_err(|_| Error::WriteFailed)?;
            }
            _ => {}
        }
        Ok(bytes_written)
    }

    /// Deflates an extracted image.
    ///
    /// The function reads extracted image from `in_stream`, verifies the image integrity
    /// and writes the deflated image to `out_stream`. After successful return, `out_stream`
    /// should look like the original source(from which the `in_stream` was extracted) modulo
    /// any intentionally skipped ranges.
    /// See [`add()`] and [`ExtentProperties`]
    pub fn deflate(
        mut in_stream: Box<dyn ReadAndSeek>,
        mut out_stream: Box<File>,
        mut verbose_stream: Option<Box<dyn Write>>,
    ) -> Result<(), Error> {
        let header = Header::deserialize_from(&mut in_stream)?;
        let mut offset = header.serialized_size();
        if let Some(stream) = &mut verbose_stream {
            let _ = writeln!(stream, "Header: {:?}", header).map_err(|_| Error::WriteFailed)?;
        }

        in_stream.seek(std::io::SeekFrom::Start(offset)).map_err(|_| Error::SeekFailed)?;
        let cluster_header = ExtentClusterHeader::deserialize_from(&mut in_stream)?;
        if let Some(stream) = &mut verbose_stream {
            let _ = writeln!(stream, "Cluster header: {:?}", cluster_header)
                .map_err(|_| Error::WriteFailed)?;
        }
        offset += cluster_header.serialized_size();
        in_stream.seek(std::io::SeekFrom::Start(offset)).map_err(|_| Error::SeekFailed)?;
        let mut extents = vec![];
        let mut max_output_size = 0;
        for i in 0..cluster_header.get_extent_count() {
            let extent = Extent::try_from(ExtentInfo::deserialize_from(&mut in_stream)?)?;
            offset += extent.serialized_size();
            if let Some(stream) = &mut verbose_stream {
                let _ = writeln!(stream, "Extent {}: {:?}", i, extent)
                    .map_err(|_| Error::WriteFailed)?;
            }
            if max_output_size < extent.storage_range().end {
                max_output_size = extent.storage_range().end;
            }
            extents.push(extent);
        }

        // Truncate the output file. This may fail. Ignore the error with a warning.
        // Truncate may fail if output happens to be a block device file or the
        // offset being too large for the containing filesystem.
        match out_stream.set_len(max_output_size) {
            Err(e) => println!("Truncate failed with {:?}", e),
            _ => {}
        };

        offset = ((offset + header.alignment - 1) / header.alignment) * header.alignment;
        in_stream.seek(std::io::SeekFrom::Start(offset)).map_err(|_| Error::SeekFailed)?;
        for extent in &extents {
            if !extent.properties().data_kind.has_data() {
                continue;
            }

            let mut buffer = vec![
                0;
                extent.storage_range().end as usize
                    - extent.storage_range().start as usize
            ];

            in_stream.read_exact(&mut buffer).map_err(|_| Error::ReadFailed)?;
            out_stream
                .seek(std::io::SeekFrom::Start(extent.storage_range().start))
                .map_err(|_| Error::SeekFailed)?;
            out_stream.write_all(&buffer).map_err(|_| Error::WriteFailed)?;
        }
        out_stream.flush().map_err(|_| Error::WriteFailed)?;

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            format::{ExtentClusterHeader, ExtentInfo, Header},
            properties::{DataKind, ExtentKind},
        },
        flate2::write::GzDecoder,
        std::{
            convert::TryFrom,
            fs::File,
            io::{Cursor, Read, Seek, SeekFrom, Write},
        },
        tempfile::{tempfile, NamedTempFile},
    };

    fn default_properties() -> ExtentProperties {
        ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified }
    }

    fn pii_properties() -> ExtentProperties {
        ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified }
    }

    fn new_default_extractor() -> Extractor {
        let out_buffer: Box<Vec<u8>> = Box::new(vec![]);
        let in_buffer = Box::new(Cursor::new(vec![0; 2 * 1024 * 1024]));

        let mut options: ExtractorOptions = Default::default();
        options.alignment = 1;
        options.compress = false;
        let extractor = Extractor::new(in_buffer, options, out_buffer);
        extractor
    }

    #[test]
    fn test_add() {
        let mut extractor = new_default_extractor();
        extractor.add(10..11, default_properties(), None).unwrap();
        extractor.add(12..14, default_properties(), None).unwrap();
        assert_eq!(extractor.extent_cluster.extent_count(), 2);
        println!("{:?}", extractor);
    }

    #[test]
    fn test_compact_one_extent() {
        let mut extractor = new_default_extractor();
        extractor.add(12..14, default_properties(), None).unwrap();
        assert_eq!(extractor.extent_cluster.extent_count(), 1);
        println!("{:?}", extractor);
    }

    #[test]
    fn test_add_huge_extent() {
        let mut extractor = new_default_extractor();
        extractor.add(0..10000000, default_properties(), None).unwrap();
        assert_eq!(extractor.extent_cluster.extent_count(), 1);
    }

    #[test]
    fn test_compact_three_extents_compacted() {
        let mut extractor = new_default_extractor();
        extractor.add(7..11, default_properties(), None).unwrap();
        extractor.add(12..14, default_properties(), None).unwrap();
        extractor.add(10..12, default_properties(), None).unwrap();
        println!("{:?}", extractor);
        assert_eq!(extractor.extent_cluster.extent_count(), 1);
    }

    #[test]
    fn test_compact_two_different_properties_compacted() {
        let mut extractor = new_default_extractor();
        extractor.add(12..14, default_properties(), None).unwrap();
        extractor.add(10..12, pii_properties(), None).unwrap();
        assert_eq!(extractor.extent_cluster.extent_count(), 2);
    }

    #[test]
    fn test_add_override_entire_extent() {
        let mut override_properties = default_properties();
        override_properties.extent_kind = ExtentKind::Pii;
        let mut extractor = new_default_extractor();
        extractor.add(10..11, default_properties(), None).unwrap();
        assert!(extractor.add(10..11, override_properties, None).is_ok());
    }

    fn new_file_based_extractor(compress: bool) -> (Extractor, ExtractorOptions, File, File) {
        let mut options: ExtractorOptions = Default::default();
        options.compress = compress;
        let out_file = tempfile().unwrap();
        let mut in_file = tempfile().unwrap();
        for i in 0..64 {
            let buf = vec![i; options.alignment as usize];
            in_file.write_all(&buf).unwrap();
        }

        let extractor = Extractor::new(
            Box::new(in_file.try_clone().unwrap()),
            options,
            Box::new(out_file.try_clone().unwrap()),
        );
        (extractor, options, out_file, in_file)
    }

    #[test]
    fn test_write_empty() {
        let (mut extractor, options, out_file, _) = new_file_based_extractor(false);
        let bytes_written = extractor.write().unwrap();
        assert_eq!(bytes_written, 2 * options.alignment);
        assert_eq!(bytes_written, out_file.metadata().unwrap().len());
    }

    #[test]
    fn test_write() {
        let (mut extractor, options, mut out_file, _) = new_file_based_extractor(false);

        // Add pii
        let pii_range = options.alignment..options.alignment * 2;
        let pii_properties =
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Skipped };
        let pii_extent = Extent::new(pii_range.clone(), pii_properties, None).unwrap();
        extractor.add(pii_range.clone(), pii_properties, None).unwrap();

        // Add data
        let data_offset = 4;
        let data_range = options.alignment * data_offset..options.alignment * 5;
        let data_properties =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
        let data_extent = Extent::new(data_range.clone(), data_properties, None).unwrap();
        extractor.add(data_range.clone(), data_properties, None).unwrap();

        // Add skipped data block
        let skipped_range = options.alignment * 8..options.alignment * 10;
        let skipped_properties =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Skipped };
        let skipped_extent = Extent::new(skipped_range.clone(), skipped_properties, None).unwrap();
        extractor.add(skipped_range.clone(), skipped_properties, None).unwrap();

        // Try to hand rolled deserializer.
        // The out_file should contain 3 blocks - one for header, one for extent
        // cluster and one for data
        assert_eq!(extractor.write().unwrap(), 3 * options.alignment);
        assert_eq!(out_file.metadata().unwrap().len(), 3 * options.alignment);
        out_file.seek(std::io::SeekFrom::Start(0)).unwrap();
        let header: Header = Header::deserialize_from(&mut out_file).unwrap();
        assert!(header.test_check());

        // Get cluster header.
        out_file.seek(std::io::SeekFrom::Start(options.alignment)).unwrap();
        let extent_cluster = ExtentClusterHeader::deserialize_from(&mut out_file).unwrap();
        assert!(extent_cluster.test_check(3, 0));

        let pii_extent_info = ExtentInfo::deserialize_from(&mut out_file).unwrap();
        assert_eq!(pii_extent, Extent::try_from(pii_extent_info).unwrap());

        let data_extent_info: ExtentInfo = ExtentInfo::deserialize_from(&mut out_file).unwrap();
        assert_eq!(data_extent, Extent::try_from(data_extent_info).unwrap());

        let skipped_extent_info = ExtentInfo::deserialize_from(&mut out_file).unwrap();
        assert_eq!(skipped_extent, Extent::try_from(skipped_extent_info).unwrap());

        // Get data.
        out_file.seek(std::io::SeekFrom::Start(2 * options.alignment)).unwrap();
        let mut buffer = Vec::new();
        out_file.read_to_end(&mut buffer).unwrap();
        assert_eq!(buffer.len(), options.alignment as usize);
        for byte in &buffer {
            // We wrote data from block offset `data_offset`. At that offset all
            // blocks contain value data_offset.
            assert_eq!(*byte, data_offset as u8);
        }
    }

    // In this test we extract same image twice; once with compression on and once with off and
    // check that the size of compressed image is smaller.
    #[test]
    fn test_compression() {
        let (mut uncompressed_extractor, options, mut uncompressed_out_file, _) =
            new_file_based_extractor(false);

        // Add pii
        let pii_range = options.alignment..options.alignment * 2;
        let pii_properties =
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified };
        uncompressed_extractor.add(pii_range.clone(), pii_properties, None).unwrap();

        // Add data
        let data_offset = 4;
        let data_range = options.alignment * data_offset..options.alignment * 5;
        let data_properties =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
        uncompressed_extractor.add(data_range.clone(), data_properties, None).unwrap();

        // Add skipped data block
        let skipped_range = options.alignment * 8..options.alignment * 10;
        let skipped_properties =
            ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Skipped };
        uncompressed_extractor.add(skipped_range.clone(), skipped_properties, None).unwrap();

        assert!(uncompressed_extractor.write().unwrap() > 0);
        assert_eq!(uncompressed_out_file.metadata().unwrap().len(), 3 * options.alignment);

        let (mut compressed_extractor, _options, mut compressed_out_file, _) =
            new_file_based_extractor(true);
        compressed_extractor.add(pii_range.clone(), pii_properties, None).unwrap();
        compressed_extractor.add(data_range.clone(), data_properties, None).unwrap();
        compressed_extractor.add(skipped_range.clone(), skipped_properties, None).unwrap();
        assert!(compressed_extractor.write().unwrap() > 0);
        assert!(compressed_out_file.metadata().unwrap().len() > 0);
        assert_ne!(
            uncompressed_out_file.metadata().unwrap().len(),
            compressed_out_file.metadata().unwrap().len()
        );

        let mut raw_image = Vec::new();
        uncompressed_out_file.seek(SeekFrom::Start(0)).unwrap();
        uncompressed_out_file.read_to_end(&mut raw_image).unwrap();

        let mut compressed_image = Vec::new();
        compressed_out_file.seek(SeekFrom::Start(0)).unwrap();
        compressed_out_file.read_to_end(&mut compressed_image).unwrap();
        let mut uncompressed_image = Vec::new();
        let mut decoder = GzDecoder::new(uncompressed_image);
        decoder.write_all(&compressed_image[..]).unwrap();
        uncompressed_image = decoder.finish().unwrap();
        assert_eq!(uncompressed_image.len(), raw_image.len());
        assert_eq!(
            uncompressed_image.iter().zip(&raw_image).filter(|&(a, b)| a == b).count(),
            uncompressed_image.len()
        );
    }

    fn deflate_test_helper(verbose: bool) {
        let (mut extractor, options, mut out_file, _) = new_file_based_extractor(false);
        let mut expected_file = tempfile().unwrap();
        let mut found_file = NamedTempFile::new().unwrap();
        let deflated_file =
            std::fs::OpenOptions::new().read(true).write(true).open(found_file.path()).unwrap();

        // Extract
        {
            // Add pii
            let pii_range = options.alignment..options.alignment * 2;
            let pii_properties =
                ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified };
            extractor.add(pii_range.clone(), pii_properties, None).unwrap();

            // Add data
            let data_offset = 4;
            let data_range = options.alignment * data_offset..options.alignment * 5;
            let data_properties =
                ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
            extractor.add(data_range.clone(), data_properties, None).unwrap();
            let buffer = vec![data_offset as u8; options.alignment as usize];
            expected_file.seek(SeekFrom::Start(data_range.start)).unwrap();
            expected_file.write_all(&buffer).unwrap();

            // Add skipped data block
            let skipped_range = options.alignment * 8..options.alignment * 10;
            let skipped_properties =
                ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Skipped };
            extractor.add(skipped_range.clone(), skipped_properties, None).unwrap();

            // Add data
            let data_offset = 14;
            let data_range = options.alignment * data_offset..options.alignment * 16;
            let data_properties =
                ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };
            extractor.add(data_range.clone(), data_properties, None).unwrap();
            let buffer = vec![data_offset as u8; 1 * options.alignment as usize];
            expected_file.seek(SeekFrom::Start(data_range.start)).unwrap();
            expected_file.write_all(&buffer).unwrap();
            let buffer = vec![(data_offset + 1) as u8; options.alignment as usize];
            expected_file.write_all(&buffer).unwrap();

            assert_eq!(extractor.write().unwrap(), 5 * options.alignment);
            assert_eq!(out_file.metadata().unwrap().len(), 5 * options.alignment);
        }

        let mut verbose_file = NamedTempFile::new().unwrap();
        // deflate and verify the deflated file has expected content.
        {
            let verbose_stream: Option<Box<dyn Write + 'static>> = match verbose {
                true => Some(Box::new(
                    std::fs::OpenOptions::new()
                        .read(true)
                        .write(true)
                        .open(verbose_file.path())
                        .unwrap(),
                )),
                false => None,
            };
            out_file.seek(SeekFrom::Start(0)).unwrap();
            Extractor::deflate(
                Box::new(out_file.try_clone().unwrap()),
                Box::new(deflated_file),
                verbose_stream,
            )
            .unwrap();
            let mut expected = vec![];
            let mut found = vec![];
            expected_file.seek(SeekFrom::Start(0)).unwrap();
            expected_file.read_to_end(&mut expected).unwrap();
            found_file.read_to_end(&mut found).unwrap();

            assert_eq!(expected.len(), found.len());
            for (i, j) in expected.iter().enumerate() {
                assert_eq!(*j, found[i]);
            }
            assert_eq!(expected, found);
        }
        let mut found = vec![];
        verbose_file.read_to_end(&mut found).unwrap();
        let expected = match verbose {
            // The following block is formatted for readability of verbose mode over
            // column width.
            true => "Header: Header { magic1: 16390322304438255204, magic2: 9989032307365739461, version: 1, extent_cluster_offset: 8192, alignment: 8192, crc32: 214251885, _padding: 0 }\n\
                     Cluster header: ExtentClusterHeader { magic1: 10289631803642904059, magic2: 4978309320308575589, extent_count: 4, next_cluster_offset: 0, footer_offset: 0, crc32: 1354654985, _padding: 0 }\n\
                     Extent 0: Extent { storage_range: 8192..16384, properties: ExtentProperties { extent_kind: Pii, data_kind: Skipped }, data: None }\n\
                     Extent 1: Extent { storage_range: 32768..40960, properties: ExtentProperties { extent_kind: Data, data_kind: Unmodified }, data: None }\n\
                     Extent 2: Extent { storage_range: 65536..81920, properties: ExtentProperties { extent_kind: Data, data_kind: Skipped }, data: None }\n\
                     Extent 3: Extent { storage_range: 114688..131072, properties: ExtentProperties { extent_kind: Data, data_kind: Unmodified }, data: None }\n\
                     ",
            false => "",
        };
        assert_eq!(found.len(), expected.len());
        assert_eq!(std::str::from_utf8(&found).unwrap(), expected);
    }

    #[test]
    fn test_deflate_verbose() {
        deflate_test_helper(true);
    }

    #[test]
    fn test_deflate_verbose_disabled() {
        deflate_test_helper(false);
    }
}
