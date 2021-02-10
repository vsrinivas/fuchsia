// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::volume::VolumeConnection,
    crate::vslice::{VSliceRange, VSliceRanges},
    async_trait::async_trait,
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    std::collections::HashMap,
    stress_test::actor::{Actor, ActorError},
};

#[derive(Debug)]
enum VolumeOperation {
    Append,
    Truncate,
    NewRange,
    Verify,
}

// Performs operations on a single FVM volume
pub struct VolumeActor {
    // A connection to the volume that this actor works on
    pub volume: VolumeConnection,

    // Allocated virtual slice ranges
    vslice_ranges: VSliceRanges,

    // Seeds used to generate the data for each slice
    slice_seeds: HashMap<u64, u128>,

    // Random number generator used for all operations
    rng: SmallRng,
}

fn generate_data(seed: u128, size: usize) -> Vec<u8> {
    let mut rng = SmallRng::from_seed(seed.to_le_bytes());
    let mut data = Vec::with_capacity(size as usize);
    for _ in 0..size {
        data.push(rng.gen());
    }
    data
}

impl VolumeActor {
    pub async fn new(
        volume: VolumeConnection,
        rng: SmallRng,
        max_slices_in_extend: u64,
        max_vslice_count: u64,
    ) -> Self {
        let vslice_ranges = VSliceRanges::new(max_vslice_count, max_slices_in_extend);
        let slice_seeds = HashMap::new();

        let mut actor = Self { volume, rng, vslice_ranges, slice_seeds };
        actor.initialize_slice_zero().await;
        actor
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
        self.volume.extend(append_range.start, append_range.len()).await?;

        // Fill the range with data
        self.fill_range(&append_range).await?;

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
        self.volume.extend(new_range.start, new_range.len()).await?;

        // Fill the range with data
        self.fill_range(&new_range).await?;

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

    async fn initialize_slice_zero(&mut self) {
        let fill_range = VSliceRange::new(0, 1);
        self.fill_range(&fill_range).await.unwrap();
        self.vslice_ranges.insert(fill_range);
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn valid_operations(&self) -> Vec<VolumeOperation> {
        vec![
            VolumeOperation::Verify,
            VolumeOperation::Append,
            VolumeOperation::Truncate,
            VolumeOperation::NewRange,
        ]
    }
}

#[async_trait]
impl Actor for VolumeActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let operations = self.valid_operations();
        let operation = operations.choose(&mut self.rng).unwrap();

        let result = match operation {
            VolumeOperation::Append => self.append().await,
            VolumeOperation::NewRange => self.new_range().await,
            VolumeOperation::Truncate => self.truncate().await,
            VolumeOperation::Verify => self.verify().await,
        };

        match result {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            Err(Status::PEER_CLOSED) | Err(Status::CANCELED) => Err(ActorError::ResetEnvironment),
            Err(s) => panic!("Error occurred during {:?}: {}", operation, s),
        }
    }
}
