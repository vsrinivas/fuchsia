// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::Error,
        extent::Extent,
        extent_cluster::ExtentCluster,
        format::Header,
        options::ExtractorOptions,
        properties::{DataKind, ExtentProperties},
        utils::{RangeOps, ReadAndSeek},
    },
    std::{fmt, io::Write, ops::Range},
};

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
    out_stream: Box<dyn Write>,
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
            out_stream: out_stream,
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
        properties: ExtentProperties,
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
        let extent = Extent::new(range, properties, data)?;
        self.extent_cluster.add_extent(&extent)
    }

    /// Writes all pending extents and their data to the out_stream.
    pub fn write(&mut self) -> Result<u64, Error> {
        let mut bytes_written = 0;
        if self.write_header {
            assert_eq!(self.current_offset, 0);
            let mut header = Header::new(self.options.alignment);
            bytes_written = header.serialize_to(&mut self.out_stream)?;
            self.current_offset = bytes_written;
            self.write_header = false;
        }
        bytes_written = bytes_written
            + self.extent_cluster.write(
                &mut self.in_stream,
                self.current_offset,
                true,
                &mut self.out_stream,
            )?;
        self.out_stream.flush().map_err(|_| Error::WriteFailed)?;
        self.current_offset = self.current_offset + bytes_written;
        Ok(bytes_written)
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
        std::{
            convert::TryFrom,
            fs::File,
            io::{Cursor, Read, Seek},
        },
        tempfile::tempfile,
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

    fn new_file_based_extractor() -> (Extractor, ExtractorOptions, File, File) {
        let options: ExtractorOptions = Default::default();
        let out_file = tempfile().unwrap();
        let mut in_file = tempfile().unwrap();
        for i in 0..64 {
            let buf = vec![i; options.alignment as usize];
            in_file.write_all(&buf).unwrap();
        }

        let extractor = Extractor::new(
            Box::new(in_file.try_clone().unwrap()),
            Default::default(),
            Box::new(out_file.try_clone().unwrap()),
        );
        (extractor, options, out_file, in_file)
    }

    #[test]
    fn test_write_empty() {
        let (mut extractor, options, out_file, _) = new_file_based_extractor();
        let bytes_written = extractor.write().unwrap();
        assert_eq!(bytes_written, 2 * options.alignment);
        assert_eq!(bytes_written, out_file.metadata().unwrap().len());
    }

    #[test]
    fn test_write() {
        let (mut extractor, options, mut out_file, _) = new_file_based_extractor();

        // Add pii
        let pii_range = options.alignment..options.alignment * 2;
        let pii_properties =
            ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified };
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
}
