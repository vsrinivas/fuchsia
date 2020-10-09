// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        error::Error,
        extent::Extent,
        format::ExtentClusterHeader,
        options::ExtractorOptions,
        properties::{DataKind, ExtentKind},
        utils::{RangeOps, ReadAndSeek},
    },
    std::io::{Read, Write},
};

/// Returns `true` if the extent's data needs to be dumped.
fn should_dump_data(extent: &Extent, dump_pii: bool) -> bool {
    let properties = extent.properties();
    // For ExtentKind::Data, we do not need to dump data when DataKind is
    // either Skipped or it is Zeroes.
    // We dump Pii only when we forced to dump Pii or it was `Modified` by the
    // storage software to obfuscate the Pii data.
    match properties.extent_kind {
        ExtentKind::Data => match properties.data_kind {
            DataKind::Modified => true,
            DataKind::Unmodified => true,
            _ => false,
        },
        ExtentKind::Pii => match properties.data_kind {
            DataKind::Modified => true,
            DataKind::Unmodified => dump_pii,
            _ => false,
        },
        _ => false,
    }
}

/// This is in-memory representation of a collection of Extents.
///
/// We hold extents in memory, (coalescing, splitting, merging if needed), till
/// write() is issued.
/// The cluster of extents lives in image file in contiguous location.
#[derive(Debug)]
pub struct ExtentCluster {
    pub(crate) extent_tree: std::collections::BTreeMap<u64, Extent>,
    options: ExtractorOptions,
}

impl ExtentCluster {
    pub fn new(options: &ExtractorOptions) -> Self {
        Self { extent_tree: Default::default(), options: options.clone() }
    }

    // insert_extent's logic is as follows.
    //  * create a list of all affected extents by this insertion.
    //  * for each extent in affected extents, split/merge with current extents.
    //  * replace affected and current extent with the new split/merged extents list.
    fn insert_extent(&mut self, mut current_extent: Extent) -> Result<(), Error> {
        let mut affected_extents = vec![];

        // Get all extents that may be affected by this extent insertion.
        for ext in self.extent_tree.iter_mut() {
            if !(current_extent.overlaps(&ext.1) || current_extent.is_adjacent(&ext.1)) {
                continue;
            }
            affected_extents.push(ext.1.clone());
        }

        // Remove all the affected extents.
        for ext in &affected_extents {
            self.extent_tree.remove(&ext.start());
        }

        // Perform split/merge of current extent with one affected extent at a time.
        // Note:
        //   1. This is performed on extents in the ascending order of extent.start.
        //   2. The existing extents are assumed to be non-overlapping with all other
        //      existing extents.
        //   3. Order of insertion changes in intermediate state of the cluster.
        let mut new_extents = vec![];
        let mut remaining = Some(current_extent.clone());
        for (i, ext) in affected_extents.iter().enumerate() {
            assert!(current_extent.overlaps(&ext) || current_extent.is_adjacent(&ext));
            match current_extent.split_or_merge(&ext, &mut new_extents) {
                Some(x) => {
                    current_extent = x.clone();
                    remaining = Some(x);
                }

                None => {
                    assert_eq!(affected_extents.len() - 1, i);
                    remaining = None;
                }
            }
        }

        // Insert all the split/merged extent into the extent tree.
        for ext in new_extents {
            self.extent_tree.insert(ext.start(), ext.clone());
        }

        // It may happen that the current extent maybe unaffected or partially affected.
        // If so insert it into the tree.
        match remaining {
            Some(e) => {
                self.extent_tree.insert(e.start(), e);
            }
            _ => {}
        }

        Ok(())
    }

    /// Adds an extent to extent cluster.
    pub fn add_extent(&mut self, extent: &Extent) -> Result<(), Error> {
        if !extent.storage_range().is_valid() {
            return Err(Error::InvalidRange);
        }
        self.insert_extent(extent.clone())
    }

    /// Returns number of extent in extent cluster.
    pub fn extent_count(&self) -> u64 {
        self.extent_tree.len() as u64
    }

    /// Returns number of data bytes that will be written
    /// in this cluster.
    fn data_size(&self) -> u64 {
        let mut size: u64 = 0;
        for (_, extent) in &self.extent_tree {
            if !should_dump_data(&extent, self.options.force_dump_pii) {
                continue;
            }
            size = size + extent.storage_range().length();
        }
        size
    }

    /// Writes ExtentCluster metadata to out_stream. crc32 to is crc of metadata and any
    /// padding required.
    fn write_metadata(&self, out_stream: &mut dyn Write) -> Result<u64, Error> {
        let mut size =
            ExtentClusterHeader::serialize_to(self.extent_count() as u64, 0, out_stream)?;

        for (_, extent) in &self.extent_tree {
            size = size + extent.write(out_stream)?;
        }
        let zero_fill_len = (((size + self.options.alignment - 1) / self.options.alignment)
            * self.options.alignment)
            - size;
        let zeroes: Vec<u8> = vec![0; zero_fill_len as usize];
        out_stream.write_all(&zeroes).map_err(move |_| Error::WriteFailed)?;
        Ok(size + zero_fill_len)
    }

    /// Write all the data in extents to the out_stream.
    ///
    /// The extent's data is read from the in_stream.
    fn write_data(
        &self,
        out_stream: &mut dyn Write,
        in_stream: &mut dyn ReadAndSeek,
    ) -> Result<u64, Error> {
        let mut size = 0;
        for (_, extent) in &self.extent_tree {
            // No need to dump the data. Only ExtentInfo will be written to the image file.
            if !should_dump_data(&extent, self.options.force_dump_pii) {
                continue;
            }
            let bytes_to_read = extent.storage_range().length();
            let mut read_buffer: Vec<u8> = vec![0; bytes_to_read as usize];
            let offset = extent.storage_range().start;
            // Seek to the storage location.
            in_stream.seek(std::io::SeekFrom::Start(offset)).map_err(move |_| Error::SeekFailed)?;
            let mut r = in_stream.take(bytes_to_read);

            // Read from the storage.
            r.read(&mut read_buffer[..bytes_to_read as usize])
                .map_err(move |_| Error::ReadFailed)?;

            // Write to the image file
            out_stream.write_all(&read_buffer).map_err(move |_| Error::WriteFailed)?;
            size = size + bytes_to_read;
        }
        Ok(size)
    }

    /// Iterate over the extent_tree and check that they are in ascending order
    /// of start offset and they do not overlap.
    fn check_extent_tree(&self) {
        let mut prev_or: Option<Extent> = None;

        for (_, extent) in &self.extent_tree {
            if prev_or.is_some() {
                let prev = prev_or.unwrap();
                assert!(extent.start() >= prev.end());
            }

            assert!(extent.storage_range().is_valid());
            prev_or = Some(extent.clone());
        }
    }

    /// Writes extent cluster to the image file.
    ///
    /// # Arguments
    ///
    /// `out_stream`    : Points to the image file stream.
    /// `in_stream`     : Stream from where extent data will be read from.
    /// `current_offset`: Starting position of the cluster within the image file.
    /// `last_cluster`  : True if this is the last cluster within the extracted image file.
    ///                   Multi-cluster image files are not yet supported.
    ///
    /// This includes computing checksum of the cluster header and all the extent infos,
    /// then writing cluster header, extent info and extent data to the image file.
    pub fn write(
        &mut self,
        mut out_stream: &mut dyn Write,
        mut in_stream: &mut dyn ReadAndSeek,
        _current_offset: u64,
        last_cluster: bool,
    ) -> Result<u64, Error> {
        if !last_cluster {
            todo!("Support for more than one cluster is not implemented.")
        }

        self.check_extent_tree();
        // Update all the extent location w.r.t. starting of the extent cluster.
        let _data_size = self.data_size();

        let mut size = self.write_metadata(out_stream)?;
        size = size + self.write_data(&mut out_stream, &mut in_stream)?;
        Ok(size)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            extent_cluster::ExtentCluster,
            properties::{DataKind, ExtentKind, ExtentProperties},
        },
        std::io::Cursor,
        std::ops::Range,
    };

    static INSERTED_ADDRESS_START: u64 = 20;
    static INSERTED_ADDRESS_END: u64 = 30;

    static LOW_PRIORITY_PROPERTIES: ExtentProperties =
        ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Zeroes };

    static HIGH_PRIORITY_PROPERTIES: ExtentProperties =
        ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Zeroes };

    static INSERTED_PROPERTIES: ExtentProperties =
        ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Unmodified };

    static OVERLAPPING_RIGHT_ADDRESS: Range<u64> =
        INSERTED_ADDRESS_END - 5..INSERTED_ADDRESS_END + 6;

    fn inserted_range() -> Range<u64> {
        INSERTED_ADDRESS_START..INSERTED_ADDRESS_END
    }

    fn inserted_extent() -> Extent {
        Extent::new(
            inserted_range(),
            INSERTED_PROPERTIES,
            // data: Some(vec![1; inserted_range().length() as usize]),
            None,
        )
        .unwrap()
    }

    // Non-overlapping extent to the right of inserted_extent().
    fn right_extent() -> Extent {
        Extent::new(
            INSERTED_ADDRESS_END + 5..INSERTED_ADDRESS_END + 15,
            INSERTED_PROPERTIES,
            // data: Some(vec![1; inserted_range().length() as usize]),
            None,
        )
        .unwrap()
    }
    fn setup_extent_cluster() -> (ExtentCluster, Vec<Extent>) {
        let mut cluster = ExtentCluster::new(&Default::default());
        match cluster.add_extent(&inserted_extent()) {
            Err(why) => println!("why: {:?}", why),
            Ok(_) => {}
        }
        return (cluster, vec![inserted_extent().clone()]);
    }

    fn verify(file: &str, line: u32, cluster: &ExtentCluster, extents: &Vec<Extent>) {
        if cluster.extent_tree.len() != extents.len() {
            println!("{}:{} Expected: {:?}\nFound: {:?}", file, line, extents, cluster.extent_tree);
        }
        assert_eq!(cluster.extent_tree.len(), extents.len());

        for (_, ext) in cluster.extent_tree.iter() {
            let mut found = false;
            for inserted in extents.iter() {
                if inserted == ext {
                    found = true;
                    break;
                }
            }
            if !found {
                println!("Could not find {:?}", *ext);
            }
            assert!(found);
        }
    }

    #[test]
    fn test_setup() {
        let (cluster, expected_extents) = setup_extent_cluster();
        assert!(INSERTED_PROPERTIES > LOW_PRIORITY_PROPERTIES);
        assert!(INSERTED_PROPERTIES < HIGH_PRIORITY_PROPERTIES);
        assert!(LOW_PRIORITY_PROPERTIES < HIGH_PRIORITY_PROPERTIES);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_non_overlapping() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let e = Extent::new(40..50, INSERTED_PROPERTIES, None).unwrap();
        cluster.add_extent(&e).unwrap();
        expected_extents.push(e);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_is_adjacent_not_mergable() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let e = Extent::new(
            INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
            LOW_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&e).unwrap();
        expected_extents.push(e);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_is_adjacent() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let e =
            Extent::new(INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5, INSERTED_PROPERTIES, None)
                .unwrap();
        cluster.add_extent(&e).unwrap();
        expected_extents.clear();
        expected_extents.push(
            Extent::new(
                INSERTED_ADDRESS_START..INSERTED_ADDRESS_END + 5,
                INSERTED_PROPERTIES,
                None,
            )
            .unwrap(),
        );
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_adjacent_in_the_middle() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        verify(file!(), line!(), &cluster, &expected_extents);
        let e =
            Extent::new(INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5, INSERTED_PROPERTIES, None)
                .unwrap();
        cluster.add_extent(&e).unwrap();
        expected_extents.clear();
        expected_extents.push(
            Extent::new(
                INSERTED_ADDRESS_START..INSERTED_ADDRESS_END + 15,
                INSERTED_PROPERTIES,
                None,
            )
            .unwrap(),
        );
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_adjacent_in_the_middle_not_mergable() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        verify(file!(), line!(), &cluster, &expected_extents);
        let m = Extent::new(
            INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
            HIGH_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&m).unwrap();
        expected_extents.push(m);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster
            .add_extent(
                &Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), INSERTED_PROPERTIES, None).unwrap(),
            )
            .unwrap();
        expected_extents[0].set_end(OVERLAPPING_RIGHT_ADDRESS.end);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_low_priority() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let mut e =
            Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), LOW_PRIORITY_PROPERTIES, None).unwrap();
        cluster.add_extent(&e).unwrap();
        e.set_start(INSERTED_ADDRESS_END);
        expected_extents.push(e);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_high_priority() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster
            .add_extent(
                &Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), HIGH_PRIORITY_PROPERTIES, None)
                    .unwrap(),
            )
            .unwrap();
        expected_extents[0].set_end(OVERLAPPING_RIGHT_ADDRESS.start);
        expected_extents.push(
            Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), HIGH_PRIORITY_PROPERTIES, None).unwrap(),
        );
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_in_middlex() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        verify(file!(), line!(), &cluster, &expected_extents);

        let middle_extent =
            Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), INSERTED_PROPERTIES, None).unwrap();
        cluster.add_extent(&middle_extent).unwrap();
        expected_extents.clear();
        expected_extents.push(
            Extent::new(INSERTED_ADDRESS_START..right_extent().end(), INSERTED_PROPERTIES, None)
                .unwrap(),
        );
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_in_middle_low_priority() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        verify(file!(), line!(), &cluster, &expected_extents);

        cluster
            .add_extent(
                &Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), LOW_PRIORITY_PROPERTIES, None)
                    .unwrap(),
            )
            .unwrap();
        expected_extents.push(
            Extent::new(
                INSERTED_ADDRESS_END..INSERTED_ADDRESS_END + 5,
                LOW_PRIORITY_PROPERTIES,
                None,
            )
            .unwrap(),
        );
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_in_middle_high_priority() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        verify(file!(), line!(), &cluster, &expected_extents);

        let middle_extent =
            Extent::new(OVERLAPPING_RIGHT_ADDRESS.clone(), HIGH_PRIORITY_PROPERTIES, None).unwrap();
        cluster.add_extent(&middle_extent).unwrap();
        expected_extents[0].set_end(OVERLAPPING_RIGHT_ADDRESS.start);
        expected_extents[1].set_start(OVERLAPPING_RIGHT_ADDRESS.end);
        expected_extents.push(middle_extent);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_multiple() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        let extreme_right_extent = Extent::new(
            right_extent().end() + 10..right_extent().end() + 20,
            INSERTED_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&extreme_right_extent).unwrap();
        expected_extents.push(extreme_right_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);

        let overlapping_extent = Extent::new(
            INSERTED_ADDRESS_START - 5..extreme_right_extent.end() + 10,
            INSERTED_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&overlapping_extent).unwrap();
        expected_extents.clear();
        expected_extents.push(overlapping_extent);
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_overlapping_multiple_not_mergeable() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        cluster.add_extent(&right_extent()).unwrap();
        expected_extents.push(right_extent().clone());
        let extreme_right_extent = Extent::new(
            right_extent().end() + 10..right_extent().end() + 20,
            INSERTED_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&extreme_right_extent).unwrap();
        expected_extents.push(extreme_right_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);

        let overlapping_extent = Extent::new(
            INSERTED_ADDRESS_START - 5..extreme_right_extent.end() + 10,
            LOW_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&overlapping_extent).unwrap();
        // The overlapping_extent gets divided into multiple extents.
        let mut split_extent = overlapping_extent.clone();
        split_extent.set_end(inserted_extent().start());
        expected_extents.push(split_extent.clone());
        split_extent.set_start(inserted_extent().end());
        split_extent.set_end(right_extent().start());
        expected_extents.push(split_extent.clone());
        split_extent.set_start(right_extent().end());
        split_extent.set_end(extreme_right_extent.start());
        expected_extents.push(split_extent.clone());
        split_extent.set_start(extreme_right_extent.end());
        split_extent.set_end(overlapping_extent.end());
        expected_extents.push(split_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    #[test]
    fn test_add_splits_existing_extent() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let small_extent = Extent::new(
            inserted_extent().start() + 3..inserted_extent().end() - 3,
            HIGH_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&small_extent).unwrap();
        expected_extents.push(expected_extents[0].clone());
        expected_extents[0].set_end(small_extent.start());
        expected_extents[1].set_start(small_extent.end());
        expected_extents.push(small_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);
    }
    #[test]
    fn test_add_splits_existing_extent_at_start() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let small_extent = Extent::new(
            inserted_extent().start()..inserted_extent().end() - 3,
            HIGH_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&small_extent).unwrap();
        expected_extents[0].set_start(small_extent.end());
        expected_extents.push(small_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);
    }
    #[test]
    fn test_add_splits_existing_extent_at_end() {
        let (mut cluster, mut expected_extents) = setup_extent_cluster();
        let small_extent = Extent::new(
            inserted_extent().start() + 3..inserted_extent().end(),
            HIGH_PRIORITY_PROPERTIES,
            None,
        )
        .unwrap();
        cluster.add_extent(&small_extent).unwrap();
        expected_extents[0].set_end(small_extent.start());
        expected_extents.push(small_extent.clone());
        verify(file!(), line!(), &cluster, &expected_extents);
    }

    fn dumpable_data_properties() -> ExtentProperties {
        ExtentProperties { extent_kind: ExtentKind::Data, data_kind: DataKind::Modified }
    }

    fn skippable_data_properties() -> ExtentProperties {
        ExtentProperties { extent_kind: ExtentKind::Unmmapped, data_kind: DataKind::Skipped }
    }

    fn pii_data_properties() -> ExtentProperties {
        ExtentProperties { extent_kind: ExtentKind::Pii, data_kind: DataKind::Unmodified }
    }

    fn setup_cluster_write_test(
        dump_pii: bool,
    ) -> (ExtentCluster, ExtractorOptions, Vec<u8>, Cursor<Vec<u8>>) {
        let mut options: ExtractorOptions = Default::default();
        options.force_dump_pii = dump_pii;
        let cluster = ExtentCluster::new(&options);
        let out_buffer: Vec<u8> = vec![];
        let in_buffer = Cursor::new(vec![0; 2 * 1024 * 1024]);

        (cluster, options, out_buffer, in_buffer)
    }

    #[test]
    fn test_extent_cluster_write() {
        let (mut cluster, options, mut out_buffer, mut in_buffer) = setup_cluster_write_test(false);

        let size = cluster.write(&mut out_buffer, &mut in_buffer, 0, true).unwrap();
        assert!(size > 0);
        assert_eq!(size % options.alignment, 0);
        assert_eq!(out_buffer.len() as u64, size);
    }

    #[test]
    fn test_extent_cluster_write_no_data() {
        let (mut cluster, options, mut out_buffer, mut in_buffer) = setup_cluster_write_test(false);
        let size = cluster.write(&mut out_buffer, &mut in_buffer, 0, true).unwrap();
        assert!(size > 0);
        assert_eq!(size % options.alignment, 0);
        assert_eq!(out_buffer.len() as u64, size);

        let properties = skippable_data_properties();
        let mut extent = Extent::new(0..1, properties, None).unwrap();

        // Write as many extents as it takes to fill one alignment unit.
        let extent_count = options.alignment / extent.serialized_size() as u64;
        for i in 0..extent_count {
            let start = i * 2 * options.alignment;
            extent.set_start(start);
            extent.set_end(start + options.alignment);
            cluster.add_extent(&extent).unwrap();
        }

        let mut new_buffer: Vec<u8> = vec![];
        let new_size = cluster.write(&mut new_buffer, &mut in_buffer, 0, true).unwrap();
        assert!(new_size > size);
        assert_eq!(new_size % options.alignment, 0);
        assert_eq!(new_buffer.len() as u64, new_size);
    }

    #[test]
    fn test_extent_cluster_write_with_data() {
        let (mut cluster, options, mut out_buffer, mut in_buffer) = setup_cluster_write_test(false);

        let no_data_extent =
            Extent::new(8192..(2 * 8192), skippable_data_properties(), None).unwrap();
        let data_extent1 =
            Extent::new((2 * 8192)..(3 * 8192), dumpable_data_properties(), None).unwrap();
        let data_extent2 =
            Extent::new((5 * 8192)..(6 * 8192), dumpable_data_properties(), None).unwrap();
        let pii_extent = Extent::new(8 * 8192..(9 * 8192), pii_data_properties(), None).unwrap();

        cluster.add_extent(&no_data_extent).unwrap();
        cluster.add_extent(&data_extent1).unwrap();
        cluster.add_extent(&data_extent2).unwrap();
        cluster.add_extent(&pii_extent).unwrap();

        let new_size = cluster.write(&mut out_buffer, &mut in_buffer, 0, true).unwrap();
        assert_eq!(new_size % options.alignment, 0);
        assert_eq!(out_buffer.len() as u64, new_size);

        // We end up writing three aligned segments.
        // - one cluster header and extents
        // - two data blocks
        assert_eq!(new_size, options.alignment * 3)
    }

    #[test]
    fn test_extent_cluster_write_with_pii() {
        let (mut cluster, options, mut out_buffer, mut in_buffer) = setup_cluster_write_test(true);

        // Add skippable data followed by pii at same range.
        let no_data_extent =
            Extent::new(8192..(15 * 8192), skippable_data_properties(), None).unwrap();
        let pii_extent = Extent::new(8192..(5 * 8192), pii_data_properties(), None).unwrap();

        cluster.add_extent(&no_data_extent).unwrap();
        cluster.add_extent(&pii_extent).unwrap();

        let new_size = cluster.write(&mut out_buffer, &mut in_buffer, 0, true).unwrap();
        assert_eq!(new_size % options.alignment, 0);
        assert_eq!(out_buffer.len() as u64, new_size);

        // We end up writing three aligned segments.
        // - one cluster header and extents
        // - four for pii blocks
        assert_eq!(new_size, options.alignment * 5)
    }

    fn verify_should_dump_data(ekind: ExtentKind, dkind: DataKind, dump_pii: bool, dump: bool) {
        let properties = ExtentProperties { extent_kind: ekind, data_kind: dkind };
        let extent = Extent::new(4..10, properties, None).unwrap();
        assert_eq!(
            should_dump_data(&extent, dump_pii),
            dump,
            "{:?} {} {}",
            properties,
            dump_pii,
            dump
        );
    }

    #[test]
    fn test_should_dump_data() {
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Skipped, true, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Zeroes, true, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Unmodified, true, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Modified, true, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Skipped, false, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Zeroes, false, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Unmodified, false, false);
        verify_should_dump_data(ExtentKind::Unmmapped, DataKind::Modified, false, false);

        verify_should_dump_data(ExtentKind::Unused, DataKind::Skipped, true, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Zeroes, true, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Unmodified, true, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Modified, true, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Skipped, false, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Zeroes, false, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Unmodified, false, false);
        verify_should_dump_data(ExtentKind::Unused, DataKind::Modified, false, false);

        verify_should_dump_data(ExtentKind::Data, DataKind::Skipped, true, false);
        verify_should_dump_data(ExtentKind::Data, DataKind::Zeroes, true, false);
        verify_should_dump_data(ExtentKind::Data, DataKind::Unmodified, true, true);
        verify_should_dump_data(ExtentKind::Data, DataKind::Modified, true, true);
        verify_should_dump_data(ExtentKind::Data, DataKind::Skipped, false, false);
        verify_should_dump_data(ExtentKind::Data, DataKind::Zeroes, false, false);
        verify_should_dump_data(ExtentKind::Data, DataKind::Unmodified, false, true);
        verify_should_dump_data(ExtentKind::Data, DataKind::Modified, false, true);

        verify_should_dump_data(ExtentKind::Pii, DataKind::Skipped, true, false);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Zeroes, true, false);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Unmodified, true, true);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Modified, true, true);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Skipped, false, false);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Zeroes, false, false);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Unmodified, false, false);
        verify_should_dump_data(ExtentKind::Pii, DataKind::Modified, false, true);
    }
}
