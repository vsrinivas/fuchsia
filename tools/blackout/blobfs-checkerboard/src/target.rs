// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    async_trait::async_trait,
    blackout_target::{Test, TestServer},
    byteorder::{NativeEndian, ReadBytesExt, WriteBytesExt},
    fs_management::Blobfs,
    fuchsia_fs::directory::readdir,
    fuchsia_merkle::MerkleTreeBuilder,
    rand::{distributions::Standard, rngs::StdRng, Rng, SeedableRng},
    std::{collections::HashMap, fs::File, io::Write},
};

fn write_blob(rng: &mut impl Rng, root: &str, i: u64) -> Result<String> {
    let mut data = vec![];
    data.write_u64::<NativeEndian>(i)?;
    // length of extra random data in bytes
    let rand_length: usize = rng.gen_range(0..6000);
    data.extend(rng.sample_iter::<u8, _>(&Standard).take(rand_length));

    // generate merkle root for new blob
    let mut builder = MerkleTreeBuilder::new();
    builder.write(&data);
    let merkle = builder.finish();
    let path = format!("{}/{}", root, merkle.root());

    // blob writing dance
    let mut blob = File::create(&path)?;
    blob.set_len(data.len() as u64)?;
    blob.write_all(&data)?;

    Ok(path)
}

#[derive(Copy, Clone)]
struct BlobfsCheckerboard;

#[async_trait]
impl Test for BlobfsCheckerboard {
    async fn setup(
        &self,
        _device_label: String,
        device_path: Option<String>,
        seed: u64,
    ) -> Result<()> {
        let block_device = device_path.unwrap();
        tracing::info!("provided block device: {}", block_device);
        let dev = blackout_target::find_dev(&block_device).await?;
        tracing::info!("using equivalent block device: {}", dev);
        let blobfs = Blobfs::new(&dev)?;

        let mut rng = StdRng::seed_from_u64(seed);

        tracing::info!("formatting provided block device with blobfs");
        blobfs.format().await.context("failed to format blobfs")?;

        let root = format!("/test-fs-root-{}", rng.gen::<u16>());
        tracing::info!("mounting blobfs into default namespace at {}", root);
        let mut blobfs = blobfs.serve().await.context("failed to mount blobfs")?;
        blobfs.bind_to_path(&root).context("failed to bind path")?;

        // Normally these tests just format in the setup, but I want a pile of files that I'm never
        // going to touch again, so this is the best place to set them up. Each file has a number
        // followed by a random amount of random garbage up to 6k (so the data for each blob takes
        // up one block at most). We want to stay within the bounds of the provided partition, so
        // query the size of the filesystem, and fill about 3/4ths of it with blobs.
        let q = blobfs.query().await?;
        tracing::info!("got query results - {:#?}", q);
        let num_blobs = (((q.total_bytes - q.used_bytes) / q.block_size as u64) * 3) / 4;
        let num_blobs = num_blobs - (num_blobs % 2);
        tracing::info!("just kidding - creating {} blobs on disk for setup", num_blobs);

        for i in 0..num_blobs {
            let _ = write_blob(&mut rng, &root, i)?;
        }

        tracing::info!("unmounting blobfs");
        blobfs.shutdown().await.context("failed to unmount blobfs")?;

        Ok(())
    }

    async fn test(
        &self,
        _device_label: String,
        device_path: Option<String>,
        seed: u64,
    ) -> Result<()> {
        let block_device = device_path.unwrap();
        tracing::info!("provided block device: {}", block_device);
        let dev = blackout_target::find_dev(&block_device).await?;
        tracing::info!("using equivalent block device: {}", dev);
        let blobfs = Blobfs::new(&dev)?;

        let mut rng = StdRng::seed_from_u64(seed);
        let root = format!("/test-fs-root-{}", rng.gen::<u16>());

        tracing::info!("mounting blobfs into default namespace at {}", root);
        let mut blobfs = blobfs.serve().await.context("failed to mount blobfs")?;
        blobfs.bind_to_path(&root).context(format!("failed to bind to {}", root))?;

        tracing::info!("some prep work...");
        // Get a list of all the blobs on the partition so we can generate our load gen state. We
        // have exclusive access to this block device, so they were either made by us in setup or
        // made by us in a previous iteration of the test. This test is designed to be run multiple
        // times in a row and could be in any state when we cut the power, so we have to
        // reconstruct it based off the test invariants. Those invariants are
        //   1. even number of blob "slots"
        //   2. odd number blobs are never modified
        //   3. even number blobs can be deleted and rewritted with new data
        //   4. that means they might not be there when we start (hence "slots")
        //   5. blobs start with their number, which is a u64 written in native endian with
        //      byteorder

        #[derive(Clone, Debug)]
        enum Slot {
            Empty,
            Blob { path: String },
        }
        let mut blobs: HashMap<u64, Slot> = HashMap::new();

        // let root_proxy = blobfs.open(fuchsia_fs::OpenFlags::RIGHT_READABLE)?;
        let root_proxy =
            fuchsia_fs::directory::open_in_namespace(&root, fuchsia_fs::OpenFlags::RIGHT_READABLE)?;
        // first we figure out what blobs are there.
        for entry in readdir(&root_proxy).await? {
            let path = format!("{}/{}", root, entry.name);
            let mut blob = File::open(&path)?;
            let slot_num = blob.read_u64::<NativeEndian>()?;
            debug_assert!(!blobs.contains_key(&slot_num));
            blobs.insert(slot_num, Slot::Blob { path });
        }
        tracing::info!("found {} blobs", blobs.len());

        // What is the max slot number we found? If it's even, it's the number of slots, if it's
        // odd, then it's the number of slots - 1. There should always be at least one slot filled
        // out (odds are never touched, so really we are going to have at least 1/2 of all possible
        // slots filled already).
        let max_found =
            blobs.keys().max().expect("Didn't find a maximum slot number. No blobs on disk?");
        // Either the last even slot was filled or we found the largest odd slot so this gets the
        // maximum number of slots.
        let max_found = max_found + 1;
        let max_slots = max_found + (max_found % 2);
        debug_assert!(max_slots % 2 == 0);
        let half_slots = max_slots / 2;
        tracing::info!(
            "max_found = {}. assuming max_slots = {} (half_slots = {})",
            max_found,
            max_slots,
            half_slots
        );

        let mut slots = vec![Slot::Empty; max_slots as usize];
        for (k, v) in blobs.into_iter() {
            slots[k as usize] = v;
        }

        tracing::info!("generating load");
        loop {
            // Get a random, even numbered slot and do the "next thing" to it.
            //   1. if the slot is empty, create a blob and write random data to it
            //   2. if it's not empty
            //      - 50% chance we just open it and read the contents and close it again
            //      - 50% chance we delete it
            // Obviously this isn't a "realistic" workload - blobs in the wild are going to spend a
            // lot of time getting read before they are deleted - but we want things to change a
            // lot.
            let slot_num = rng.gen_range(0..half_slots as usize) * 2;
            let maybe_new_slot = match &slots[slot_num] {
                Slot::Empty => {
                    let path = write_blob(&mut rng, &root, slot_num as u64)?;
                    Some(Slot::Blob { path })
                }
                Slot::Blob { path } => {
                    if rng.gen_bool(1.0 / 2.0) {
                        let mut blob = File::open(&path)?;
                        let _ = blob.read_u64::<NativeEndian>()?;
                        None
                    } else {
                        std::fs::remove_file(&path)?;
                        Some(Slot::Empty)
                    }
                }
            };

            if let Some(new_slot) = maybe_new_slot {
                slots[slot_num] = new_slot;
            }
        }
    }

    async fn verify(
        &self,
        _device_label: String,
        device_path: Option<String>,
        _seed: u64,
    ) -> Result<()> {
        let block_device = device_path.unwrap();
        tracing::info!("provided block device: {}", block_device);
        let dev = blackout_target::find_dev(&block_device).await?;
        tracing::info!("using equivalent block device: {}", dev);
        let blobfs = Blobfs::new(&dev)?;

        tracing::info!("verifying disk with fsck");
        blobfs.fsck().await.context("fsck failed")?;

        tracing::info!("verification successful");
        Ok(())
    }
}

#[fuchsia::main]
async fn main() -> Result<()> {
    let server = TestServer::new(BlobfsCheckerboard)?;
    server.serve().await;

    Ok(())
}
