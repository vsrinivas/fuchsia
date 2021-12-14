// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::volume::VolumeConnection,
    crate::vslice::{VSliceRange, VSliceRanges},
    async_trait::async_trait,
    fuchsia_zircon::Status,
    log::{debug, info},
    rand::{prelude::IteratorRandom, rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    std::collections::HashMap,
    stress_test::actor::{Actor, ActorError},
};

#[derive(Clone, Debug)]
enum VolumeOperation {
    Append { index: usize, range: VSliceRange },
    Truncate { index: usize, range: VSliceRange },
    NewRange { range: VSliceRange },
    Verify { index: usize },
}

impl VolumeOperation {
    pub fn random(rng: &mut SmallRng, vslice_ranges: &VSliceRanges) -> Self {
        let extensions = vslice_ranges.extensions();
        assert!(extensions.len() > 0);
        let (index, extension) = extensions.choose(rng).unwrap();

        let append = {
            let range = extension.shrink_from_end(rng);
            VolumeOperation::Append { index: *index, range }
        };

        let new_range = {
            let range = extension.subrange(rng);
            VolumeOperation::NewRange { range }
        };

        let truncate = {
            let index = vslice_ranges.random_index(rng);
            let range = vslice_ranges.get(index);
            let range = range.shrink_from_start(rng);

            VolumeOperation::Truncate { index, range }
        };

        let verify = {
            let index = vslice_ranges.random_index(rng);
            VolumeOperation::Verify { index }
        };

        let operations = vec![append, truncate, new_range, verify];

        operations.into_iter().choose(rng).unwrap()
    }
}

// Performs operations on a single FVM volume
pub struct VolumeActor {
    // A connection to the volume that this actor works on
    pub volume: VolumeConnection,

    // Allocated virtual slice ranges
    vslice_ranges: VSliceRanges,

    // Seeds used to generate the data for each slice
    slice_seeds: HashMap<u64, u64>,

    // Random number generator used for all operations
    rng: SmallRng,

    // Contains details about the last pending operation.
    // If set, we will retry this operation. Note that this
    // operation must be idempotent for it to be safe to retry.
    pending_op: Option<VolumeOperation>,
}

fn generate_data(seed: u64, size: usize) -> Vec<u8> {
    let mut rng = SmallRng::seed_from_u64(seed);
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

        let mut actor = Self { volume, rng, vslice_ranges, slice_seeds, pending_op: None };
        actor.initialize_slice_zero().await;
        actor
    }

    async fn fill_range(&mut self, range: &VSliceRange) -> Result<Vec<u64>, Status> {
        let slice_size = self.volume.slice_size() as usize;
        let mut seeds = vec![];

        for slice_offset in range.start..range.end {
            // Fill the slice with data
            let seed = self.rng.gen();
            let data = generate_data(seed, slice_size);
            self.volume.write_slice_at(&data, slice_offset).await?;
            seeds.push(seed);
        }

        Ok(seeds)
    }

    async fn is_range_allocated(&self, range: &VSliceRange) -> Result<bool, Status> {
        // If the first slice is allocated, then assume that all the rest are.
        // This is because exactly one volume actor is operating on this volume,
        // the extend operation is atomic and when the operation is redone the same
        // range would be used.
        match self.volume.read_slice_at(range.start).await {
            Ok(_) => Ok(true),
            Err(Status::OUT_OF_RANGE) => Ok(false),
            Err(s) => Err(s),
        }
    }

    fn insert_seeds(&mut self, range: &VSliceRange, seeds: Vec<u64>) {
        assert_eq!(range.len(), seeds.len() as u64);

        let offsets = range.start..range.end;
        for (offset, seed) in offsets.zip(seeds) {
            let prev = self.slice_seeds.insert(offset, seed);
            assert!(prev.is_none());
        }
    }

    async fn append(&mut self, index: usize, append_range: VSliceRange) -> Result<(), Status> {
        debug!("Append Range = {:?}", append_range);

        match self.volume.extend(append_range.start, append_range.len()).await {
            Ok(()) => {}
            Err(Status::INVALID_ARGS) => {
                // This can happen if the range was already extended.
                assert!(self.is_range_allocated(&append_range).await?);
            }
            Err(s) => return Err(s),
        }

        // Fill the range with data
        let seeds = self.fill_range(&append_range).await?;

        // Update state
        self.insert_seeds(&append_range, seeds);

        let mut range = self.vslice_ranges.get_mut(index);
        range.end = append_range.end;

        Ok(())
    }

    async fn new_range(&mut self, new_range: VSliceRange) -> Result<(), Status> {
        debug!("New Range = {:?}", new_range);

        match self.volume.extend(new_range.start, new_range.len()).await {
            Ok(()) => {}
            Err(Status::INVALID_ARGS) => {
                // This can happen if the range was already extended.
                assert!(self.is_range_allocated(&new_range).await?);
            }
            Err(s) => return Err(s),
        }

        // Fill the range with data
        let seeds = self.fill_range(&new_range).await?;

        // Update state
        self.insert_seeds(&new_range, seeds);
        self.vslice_ranges.insert(new_range);

        Ok(())
    }

    async fn truncate(&mut self, index: usize, subrange: VSliceRange) -> Result<(), Status> {
        debug!("Truncate Range = {:?}", subrange);

        // Shrinking from offset 0 is forbidden
        if subrange.start == 0 {
            return Ok(());
        }

        // Do the shrink operation on the volume
        match self.volume.shrink(subrange.start, subrange.len()).await {
            Ok(()) => {}
            Err(Status::INVALID_ARGS) => {
                // This can happen if the range was already shrunk.
                assert!(!self.is_range_allocated(&subrange).await?);
            }
            Err(s) => return Err(s),
        }

        let mut range = self.vslice_ranges.get_mut(index);

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

    async fn verify(&mut self, index: usize) -> Result<(), Status> {
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
        let seeds = self.fill_range(&fill_range).await.unwrap();
        self.insert_seeds(&fill_range, seeds);
        self.vslice_ranges.insert(fill_range);
    }
}

#[async_trait]
impl Actor for VolumeActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let operation = if let Some(pending_op) = self.pending_op.take() {
            // Complete a pending operation, if one exists
            pending_op
        } else {
            // Get a new operation
            VolumeOperation::random(&mut self.rng, &self.vslice_ranges)
        };

        let result = match operation.clone() {
            VolumeOperation::Append { index, range } => self.append(index, range).await,
            VolumeOperation::NewRange { range } => self.new_range(range).await,
            VolumeOperation::Truncate { index, range } => self.truncate(index, range).await,
            VolumeOperation::Verify { index } => self.verify(index).await,
        };

        match result {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            // Any other error is assumed to come from an intentional crash.
            // The environment verifies that an intentional crash occurred
            // and will panic if that is not the case.
            Err(s) => {
                info!("Volume actor got status {} during operation {:?}", s, operation);

                // Record this operation as pending.
                // We will attempt to redo it when the connection is restored.
                self.pending_op = Some(operation);

                Err(ActorError::ResetEnvironment)
            }
        }
    }
}
