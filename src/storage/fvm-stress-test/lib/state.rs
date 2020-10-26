// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fvm::Volume,
    fuchsia_zircon::Status,
    log::{debug, info},
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    std::collections::HashMap,
};

#[derive(Debug)]
enum FvmOperation {
    Append,
    Truncate,
    NewRange,
    Verify,
}

#[derive(Eq, PartialEq)]
struct VSliceRange {
    // The slice index that this virtual range begins at
    start: u64,

    // The slice index that this virtual range ends at (non-inclusive)
    end: u64,
}

impl VSliceRange {
    fn new(start: u64, end: u64) -> Self {
        assert!(end > start);
        Self { start, end }
    }

    fn len(&self) -> u64 {
        self.end - self.start
    }

    fn does_overlap(&self, other: &Self) -> bool {
        // Netiher the start nor the end of |other| range should exist inside |self|
        (self.start <= other.start && other.start < self.end)
            || (self.start < other.end && other.end <= self.end)
    }
}

impl std::fmt::Debug for VSliceRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}:{})", self.start, self.end)
    }
}

struct VSliceRanges {
    // List of discontiguous virtual slice ranges, sorted by start slice index.
    ranges: Vec<VSliceRange>,

    // Maximum number of allocatable virtual slices
    max_vslice_count: u64,
}

impl VSliceRanges {
    pub fn new(max_vslice_count: u64) -> Self {
        Self { ranges: vec![], max_vslice_count }
    }

    // returns index of the range to the right of |new_range| after insertion.
    fn get_insert_position(&self, new_range: &VSliceRange) -> usize {
        for (i, range) in self.ranges.iter().enumerate() {
            assert!(!range.does_overlap(new_range));

            // A new slice will be inserted at position |i| if the i'th range
            // starts after the new range ends.
            //
            //         |------new-------|
            // -----|                      |---------i----------|   |--------i+1---------|
            if new_range.end <= range.start {
                return i;
            }
        }

        // This element must be inserted at the end
        self.ranges.len()
    }

    pub fn unallocated(&self) -> Vec<VSliceRange> {
        assert!(self.ranges.len() > 0);

        let mut iter = self.ranges.iter().peekable();
        let mut unallocated_ranges = vec![];

        // Create ranges between two consecutive allocated ranges
        while let Some(cur_range) = iter.next() {
            if let Some(next_range) = iter.peek() {
                let range = VSliceRange::new(cur_range.end, next_range.start);
                unallocated_ranges.push(range);
            }
        }

        let first_range = self.ranges.first().unwrap();
        let last_range = self.ranges.last().unwrap();

        // If possible, create a range to the first slice
        if first_range.start > 0 {
            let range = VSliceRange::new(0, first_range.start);
            unallocated_ranges.push(range);
        }

        // If possible, create a range from the last slice
        if last_range.end < self.max_vslice_count {
            let range = VSliceRange::new(last_range.end, self.max_vslice_count);
            unallocated_ranges.push(range);
        }

        unallocated_ranges
    }

    pub fn get_mut(&mut self, index: usize) -> &mut VSliceRange {
        self.ranges.get_mut(index).unwrap()
    }

    pub fn insert(&mut self, range: VSliceRange) {
        let insert_pos = self.get_insert_position(&range);
        self.ranges.insert(insert_pos, range);
    }

    pub fn remove(&mut self, index: usize) {
        self.ranges.remove(index);
    }

    pub fn random_index(&self, rng: &mut SmallRng) -> usize {
        rng.gen_range(0, self.ranges.len())
    }

    pub fn num_allocated_slices(&self) -> u64 {
        self.ranges.iter().fold(0, |val, r| val + r.len())
    }
}

// In-memory state of an FVM Volume
pub struct VolumeOperator {
    // The volume on which operations are being run
    volume: Volume,

    // Allocated virtual slice ranges
    vslice_ranges: VSliceRanges,

    // Seeds used to generate the data for each slice
    slice_seeds: HashMap<u64, u128>,

    // Random number generator used for all operations
    rng: SmallRng,

    // Limits the maximum slices in a single extend operation
    max_slices_in_extend: u64,
}

fn generate_data(seed: u128, size: usize) -> Vec<u8> {
    let mut rng = SmallRng::from_seed(seed.to_le_bytes());
    let mut data = Vec::with_capacity(size as usize);
    for _ in 0..size {
        data.push(rng.gen());
    }
    data
}

impl VolumeOperator {
    pub async fn new(
        mut volume: Volume,
        mut rng: SmallRng,
        max_slices_in_extend: u64,
        max_vslice_count: Option<u64>,
    ) -> Self {
        let max_vslice_count =
            if let Some(count) = max_vslice_count { count } else { volume.max_vslice_count() };

        let mut vslice_ranges = VSliceRanges::new(max_vslice_count);
        let mut slice_seeds = HashMap::new();

        // Initialize slice 0
        vslice_ranges.insert(VSliceRange::new(0, 1));
        let seed = rng.gen();
        slice_seeds.insert(0, seed);
        let slice_size = volume.slice_size() as usize;
        let data = generate_data(seed, slice_size);
        volume.write_slice_at(&data, 0).await;

        VolumeOperator { volume, rng, vslice_ranges, slice_seeds, max_slices_in_extend }
    }

    async fn fill_data(&mut self, start: u64, end: u64) {
        assert!(start < end);
        for slice_offset in start..end {
            let seed = self.rng.gen();
            let prev = self.slice_seeds.insert(slice_offset, seed);
            assert!(prev.is_none());

            let slice_size = self.volume.slice_size() as usize;
            let data = generate_data(seed, slice_size);
            self.volume.write_slice_at(&data, slice_offset).await;
        }
    }

    pub async fn append(&mut self) {
        let index = self.vslice_ranges.random_index(&mut self.rng);
        let mut range = self.vslice_ranges.get_mut(index);
        let num_slices_to_append = self.rng.gen_range(1, self.max_slices_in_extend);

        debug!("Range = {:?}", range);
        debug!("Num slices to append = {}", num_slices_to_append);

        // This is allowed to fail if we:
        // 1. run out of space
        // 2. collide with an existing range
        // 3. give unsupported ranges
        let result = self.volume.extend(range.end, num_slices_to_append).await;
        match result {
            Err(Status::NO_SPACE) => {
                debug!("Could not extend range. Ran out of space!");
                return;
            }
            Err(Status::INVALID_ARGS) => {
                debug!("Could not extend range. Invalid arguments.");
                return;
            }
            Err(s) => {
                panic!("Unexpected error: {}", s);
            }
            Ok(()) => {}
        }

        let old_end = range.end;
        let new_end = old_end + num_slices_to_append;
        range.end = new_end;

        // Generate data to fill the range
        self.fill_data(old_end, new_end).await;
    }

    pub async fn truncate(&mut self) {
        let index = self.vslice_ranges.random_index(&mut self.rng);
        let mut range = self.vslice_ranges.get_mut(index);
        let subrange = {
            let start = if range.len() == 1 {
                range.start
            } else {
                self.rng.gen_range(range.start, range.end)
            };
            VSliceRange::new(start, range.end)
        };

        debug!("Range = {:?}", range);
        debug!("Subrange = {:?}", subrange);

        // Shrinking from offset 0 is forbidden
        if subrange.start == 0 {
            return;
        }

        self.volume.shrink(subrange.start, subrange.len()).await;

        if subrange == *range {
            // Remove the entire range
            self.vslice_ranges.remove(index)
        } else {
            // Shrink the range
            range.end = subrange.start;
        }

        // Remove the seeds of the subrange
        for slice_offset in subrange.start..subrange.end {
            self.slice_seeds.remove(&slice_offset).unwrap();
        }
    }

    pub async fn new_range(&mut self) {
        let unalloc_ranges = self.vslice_ranges.unallocated();
        assert!(unalloc_ranges.len() > 0);

        let range = unalloc_ranges.choose(&mut self.rng).unwrap();
        let subrange = {
            let start = self.rng.gen_range(range.start, range.end);
            let end = self.rng.gen_range(start, start + self.max_slices_in_extend) + 1;
            VSliceRange::new(start, end)
        };

        debug!("Range = {:?}", subrange);

        // This is allowed to fail if we:
        // 1. run out of space
        // 2. collide with an existing range
        // 3. give unsupported ranges
        let result = self.volume.extend(subrange.start, subrange.len()).await;
        match result {
            Err(Status::NO_SPACE) => {
                debug!("Could not create new range. Ran out of space!");
                return;
            }
            Err(Status::INVALID_ARGS) => {
                debug!("Could not create new range. Invalid arguments.");
                return;
            }
            Err(s) => {
                panic!("Unexpected error: {}", s);
            }
            Ok(()) => {}
        }

        if let Err(status) = result {
            assert_eq!(status, Status::NO_SPACE);
            debug!("Could not create new range. Ran out of space!");
            return;
        }

        self.fill_data(subrange.start, subrange.end).await;
        self.vslice_ranges.insert(subrange);
    }

    pub async fn verify(&mut self) {
        let index = self.vslice_ranges.random_index(&mut self.rng);
        let range = self.vslice_ranges.get_mut(index);

        debug!("Range = {:?}", range);

        // Perform verification on each slice
        for slice_offset in range.start..range.end {
            let seed = self.slice_seeds.get(&slice_offset).unwrap();
            let slice_size = self.volume.slice_size() as usize;
            let expected_data = generate_data(*seed, slice_size);
            let actual_data = self.volume.read_slice_at(slice_offset).await;
            assert_eq!(expected_data, actual_data);
        }
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn get_operation_list(&mut self) -> Vec<FvmOperation> {
        vec![
            FvmOperation::Verify,
            FvmOperation::Append,
            FvmOperation::Truncate,
            FvmOperation::NewRange,
        ]
    }

    pub async fn do_random_operation(&mut self, iteration: u64) {
        let operations = self.get_operation_list();
        let operation = operations.choose(&mut self.rng).unwrap();
        debug!("-------> [OPERATION {}] {:?}", iteration, operation);
        match operation {
            FvmOperation::Append => {
                self.append().await;
            }
            FvmOperation::Truncate => {
                self.truncate().await;
            }
            FvmOperation::NewRange => {
                self.new_range().await;
            }
            FvmOperation::Verify => {
                self.verify().await;
            }
        }
        debug!("<------- [OPERATION {}] {:?}", iteration, operation);
    }

    pub async fn destroy(self) {
        info!("Destroying partition. Freeing {} slices", self.vslice_ranges.num_allocated_slices());
        self.volume.destroy().await;
    }
}
