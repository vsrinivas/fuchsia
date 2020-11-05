// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fvm::Volume,
    fuchsia_async::Task,
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

    // Returns a subrange that ends at the same position, but
    // may be smaller.
    fn shrink_from_start(&self, rng: &mut SmallRng) -> VSliceRange {
        let start = rng.gen_range(self.start, self.end);
        VSliceRange::new(start, self.end)
    }

    // Returns a subrange that starts at the same position, but
    // may be smaller.
    fn shrink_from_end(&self, rng: &mut SmallRng) -> VSliceRange {
        let end = rng.gen_range(self.start, self.end) + 1;
        VSliceRange::new(self.start, end)
    }

    // Returns a subrange that may be smaller
    fn subrange(&self, rng: &mut SmallRng) -> VSliceRange {
        let start = rng.gen_range(self.start, self.end);
        let end = rng.gen_range(start, self.end) + 1;
        VSliceRange::new(start, end)
    }
}

impl std::fmt::Debug for VSliceRange {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}:{})", self.start, self.end)
    }
}

struct VSliceRanges {
    // List of virtual slice ranges, sorted by start slice index.
    ranges: Vec<VSliceRange>,

    // Maximum number of allocatable virtual slices
    max_vslice_count: u64,

    // Limits the maximum slices in a single extend operation
    max_slices_in_extend: u64,
}

impl VSliceRanges {
    pub fn new(max_vslice_count: u64, max_slices_in_extend: u64) -> Self {
        Self { ranges: vec![], max_vslice_count, max_slices_in_extend }
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

    // Return the indices of extents that have empty space next to them, paired with the ranges
    // that they could be extended into.
    //
    //                             |----------X-----------|
    // |-----------i--------------|                        |-----------i+1-----------|
    //
    // In the above scenario, (i, range X) is returned
    fn extensions(&mut self) -> Vec<(usize, VSliceRange)> {
        assert!(self.ranges.len() > 0);

        let mut iter = self.ranges.iter().enumerate().peekable();
        let mut extensions = vec![];

        // Create extensions between two consecutive allocated ranges
        while let Some((index, cur_range)) = iter.next() {
            if let Some((_, next_range)) = iter.peek() {
                let end = if next_range.start == cur_range.end {
                    // There is no space between these ranges
                    continue;
                } else if next_range.start - cur_range.end > self.max_slices_in_extend {
                    // There is too much space between these ranges.
                    // Limit to |max_slices_in_extend|.
                    cur_range.end + self.max_slices_in_extend
                } else {
                    next_range.start
                };

                let extension = VSliceRange::new(cur_range.end, end);
                extensions.push((index, extension));
            }
        }

        let last_index = self.ranges.len() - 1;
        let last_range = self.ranges.last().unwrap();

        // If possible, create an extension from the last slice
        if last_range.end < self.max_vslice_count {
            let end = if self.max_vslice_count - last_range.end > self.max_slices_in_extend {
                // There is too much space at the end.
                // Limit to |max_slices_in_extend|.
                last_range.end + self.max_slices_in_extend
            } else {
                self.max_vslice_count
            };
            let extension = VSliceRange::new(last_range.end, end);
            extensions.push((last_index, extension));
        }

        extensions
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
    // The volume used by this operator
    volume: Volume,

    // Allocated virtual slice ranges
    vslice_ranges: VSliceRanges,

    // Seeds used to generate the data for each slice
    slice_seeds: HashMap<u64, u128>,

    // Random number generator used for all operations
    rng: SmallRng,

    // Number of operations this operator must complete
    num_operations: u64,
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
    pub fn new(
        volume: Volume,
        rng: SmallRng,
        max_slices_in_extend: u64,
        max_vslice_count: u64,
        num_operations: u64,
    ) -> Self {
        let vslice_ranges = VSliceRanges::new(max_vslice_count, max_slices_in_extend);
        let slice_seeds = HashMap::new();

        VolumeOperator { volume, rng, vslice_ranges, slice_seeds, num_operations }
    }

    async fn extend_volume(&mut self, range: &VSliceRange) -> Result<(), Status> {
        // Do the extend operation on the volume. This is allowed to fail if there
        // are not enough physical slices left.
        let result = self.volume.extend(range.start, range.len()).await;
        match result {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Err(Status::NO_SPACE),
            Err(s) => panic!("Unrecoverable error during extend: {}", s),
        }
    }

    async fn fill_range(&mut self, range: &VSliceRange) -> Result<(), Status> {
        let slice_size = self.volume.slice_size() as usize;
        for slice_offset in range.start..range.end {
            // Fill the slice with data
            let seed = self.rng.gen();
            let data = generate_data(seed, slice_size);
            self.volume.write_slice_at(&data, slice_offset).await?;

            // Update state
            let prev = self.slice_seeds.insert(slice_offset, seed);
            assert!(prev.is_none());
        }
        Ok(())
    }

    async fn append(&mut self) -> Result<(), Status> {
        let extensions = self.vslice_ranges.extensions();
        assert!(extensions.len() > 0);

        // Choose an extension
        let (index, extension) = extensions.choose(&mut self.rng).unwrap();
        let append_range = extension.shrink_from_end(&mut self.rng);
        debug!("Append Range = {:?}", append_range);

        // Do the extend operation on the volume
        self.extend_volume(&append_range).await?;

        // Once the extend call succeeds, filling the range should not fail
        self.fill_range(&append_range).await.unwrap();

        // Update state
        let mut range = self.vslice_ranges.get_mut(*index);
        range.end = append_range.end;

        Ok(())
    }

    async fn new_range(&mut self) -> Result<(), Status> {
        let extensions = self.vslice_ranges.extensions();
        assert!(extensions.len() > 0);

        // Choose an extension
        let (_, extension) = extensions.choose(&mut self.rng).unwrap();
        let new_range = extension.subrange(&mut self.rng);
        debug!("New Range = {:?}", new_range);

        // Do the extend operation on the volume
        self.extend_volume(&new_range).await?;

        // Once the extend call succeeds, filling the range should not fail
        self.fill_range(&new_range).await.unwrap();

        // Update state
        self.vslice_ranges.insert(new_range);

        Ok(())
    }

    async fn truncate(&mut self) -> Result<(), Status> {
        let index = self.vslice_ranges.random_index(&mut self.rng);
        let mut range = self.vslice_ranges.get_mut(index);
        let subrange = range.shrink_from_start(&mut self.rng);

        debug!("Truncate Range = {:?}", subrange);

        // Shrinking from offset 0 is forbidden
        if subrange.start == 0 {
            return Ok(());
        }

        // Do the shrink operation on the volume
        self.volume.shrink(subrange.start, subrange.len()).await?;

        // Update state
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

        Ok(())
    }

    async fn verify(&mut self) -> Result<(), Status> {
        let index = self.vslice_ranges.random_index(&mut self.rng);
        let range = self.vslice_ranges.get_mut(index);

        debug!("Verify Range = {:?}", range);

        // Perform verification on each slice
        for slice_offset in range.start..range.end {
            let seed = self.slice_seeds.get(&slice_offset).unwrap();
            let slice_size = self.volume.slice_size() as usize;
            let expected_data = generate_data(*seed, slice_size);
            let actual_data = self.volume.read_slice_at(slice_offset).await?;
            assert_eq!(expected_data, actual_data);
        }

        Ok(())
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

    // Perform a single operation on the given volume.
    async fn do_operation(&mut self, operation: &FvmOperation) -> Result<(), Status> {
        match operation {
            FvmOperation::Append => self.append().await?,
            FvmOperation::Truncate => self.truncate().await.expect("Truncation failed"),
            FvmOperation::NewRange => self.new_range().await?,
            FvmOperation::Verify => self.verify().await.expect("Verification failed"),
        }
        Ok(())
    }

    async fn initialize_slice_zero(&mut self) {
        let fill_range = VSliceRange::new(0, 1);
        self.fill_range(&fill_range).await.unwrap();
        self.vslice_ranges.insert(fill_range);
    }

    async fn do_operations(mut self) {
        self.initialize_slice_zero().await;

        for i in 1..=self.num_operations {
            let operations = self.get_operation_list();
            let operation = operations.choose(&mut self.rng).unwrap();

            debug!(">>>>>>>>> [OPERATION {}] {:?}", i, operation);
            let result = self.do_operation(operation).await;
            debug!("<<<<<<<<< [OPERATION {}] {:?} [Result: {:?}]", i, operation, result);
        }

        // Attempt to destroy the volume, returning all slices back to the
        // FVM volume manager.
        info!("Freeing {} slices in volume", self.vslice_ranges.num_allocated_slices());
        self.volume.destroy().await.unwrap();
    }

    // Start a new thread for this operator and run as many operations
    // as possible before the volume disconnects.
    pub fn run(self) -> Task<()> {
        Task::blocking(async move { self.do_operations().await })
    }
}
