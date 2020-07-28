// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_merkle::*,
    fuchsia_syslog as syslog,
    log::*,
    rand::rngs::SmallRng,
    rand::seq::SliceRandom,
    rand::FromEntropy,
    rand::Rng,
    rand_core::SeedableRng,
    std::fs::*,
    std::io::Write,
    std::path::PathBuf,
    test_utils_lib::{events::*, test_utils::*},
};

// In-memory representation of a single blob.
struct Blob {
    hash: String,
    data: Vec<u8>,
    path: PathBuf,
}

// In-memory state of blobfs. Stores information about blobs expected to exist on disk.
struct BlobfsState {
    rng: SmallRng,
    blobfs_path: PathBuf,
    blobs: Vec<Blob>,
}

impl BlobfsState {
    fn new(blobfs_path: PathBuf, seed: u64) -> BlobfsState {
        return BlobfsState { rng: SmallRng::seed_from_u64(seed), blobfs_path, blobs: vec![] };
    }

    // Creates an in-memory representation of a Blob from random data.
    fn create_blob(&mut self) -> Blob {
        // Determine size of the blob
        let bit_scale: u8 = self.rng.gen_range(7, 14);
        let mut blob_size: usize = 1 << bit_scale;
        blob_size |= self.rng.gen::<usize>() % blob_size;

        // Fill it with random data
        // TODO(57230): Make the data less random and hence more compressible.
        let data = vec![self.rng.gen(); blob_size];

        // Calculate its merkle root hash
        let tree = MerkleTree::from_reader(&data[..]).unwrap();
        let hash = tree.root().to_string();

        let path = self.blobfs_path.join(&hash);
        return Blob { hash, data, path };
    }

    // Writes a given blob to the filesystem.
    fn write_blob(blob: &Blob) {
        let mut file = File::create(&blob.path).unwrap();
        file.set_len(blob.data.len() as u64).unwrap();
        file.write(&blob.data).unwrap();
        file.flush().unwrap();
    }

    // Creates [num_blobs] unique blobs and writes them to the filesystem.
    fn create_and_write_blobs(&mut self, num_blobs: usize) {
        info!("Creating {} blobs...", num_blobs);
        for _ in 1..=num_blobs {
            let blob = {
                let mut blob: Blob;

                // The blob may already exist on disk
                // If so, generate another.
                loop {
                    blob = self.create_blob();
                    if self.blobs.iter().all(|x| x.hash != blob.hash) {
                        break;
                    }
                    warn!("Blob {} already exists!", blob.hash);
                }

                blob
            };
            BlobfsState::write_blob(&blob);
            self.blobs.push(blob);
        }
    }

    // Selects [num_blobs] random blobs and deletes them from the filesystem.
    fn delete_blobs(&mut self, num_blobs: usize) {
        info!("Deleting {} blobs...", num_blobs);

        assert!(num_blobs <= self.blobs.len());

        // Shuffle the blobs and remove the first num_blobs from the list
        self.blobs.shuffle(&mut self.rng);
        let to_delete: Vec<Blob> = self.blobs.drain(0..num_blobs).collect();

        for entry in to_delete {
            remove_file(&entry.path).unwrap();
        }
    }

    // Returns the number of blobs expected to exist in the filesystem.
    fn num_blobs(&self) -> usize {
        return self.blobs.len();
    }
}

#[fasync::run_singlethreaded(test)]
async fn test() {
    syslog::init_with_tags(&["blobfs_stress_test"]).unwrap();

    // Setup all components
    let test: OpaqueTest =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/root.cm")
            .await
            .unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    {
        let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
        event_source.start_component_tree().await;

        // Expect 4 components to be started (root, isolated-devmgr, driver_manager_test and disk-create)
        let expected_monikers = vec![
            "fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/root.cm",
            "fuchsia-pkg://fuchsia.com/blobfs-stress-test#meta/blobfs-mounter.cm",
            "fuchsia-pkg://fuchsia.com/isolated-devmgr#meta/isolated_devmgr.cm",
            "fuchsia-pkg://fuchsia.com/isolated-devmgr#meta/driver_manager_test.cm",
        ];
        for _ in 1..=4 {
            let event = event_stream.expect_exact::<Started>(EventMatcher::new()).await;
            assert!(expected_monikers.contains(&event.component_url()));
            event.resume().await.unwrap();
        }
    }

    // Wait for blobfs to be mounted
    let blobfs_path = test.get_hub_v2_path().join("children/blobfs-mounter/exec/out/blobfs");
    assert!(blobfs_path.exists());

    // Setup RNG
    // TODO(57230): Allow passing in a seed for this test.
    let mut initial_rng = SmallRng::from_entropy();
    let state_seed: u64 = initial_rng.gen();
    let test_seed: u64 = initial_rng.gen();
    info!("Seeds -> State = {}, Test = {}", state_seed, test_seed);
    let mut test_rng = SmallRng::seed_from_u64(test_seed);

    let mut state = BlobfsState::new(blobfs_path.clone(), state_seed);

    // This test will initialize blobfs and then cycle through 10 rounds of
    // random blob creation/deletion. These cycles roughly emulate the behavior
    // seen during an OTA.

    // Initialize blobfs with some blobs
    let num_blobs = test_rng.gen_range(900, 1100);
    state.create_and_write_blobs(num_blobs);

    // Delete a random number of blobs and add back the same number (+/- 50)
    for _ in 1..=10 {
        let num_blobs_to_delete = test_rng.gen_range(1, state.num_blobs());
        state.delete_blobs(num_blobs_to_delete);

        let num_blobs_to_create = test_rng.gen_range(num_blobs_to_delete, num_blobs_to_delete + 50);
        state.create_and_write_blobs(num_blobs_to_create);
    }

    // Verify the number of blobs on disk matches state
    assert!(read_dir(blobfs_path).unwrap().count() == state.num_blobs());
}
