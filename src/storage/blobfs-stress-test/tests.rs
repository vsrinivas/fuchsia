// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    blobfs_stress_test_lib::{state::BlobfsState, utils::init_blobfs},
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    log::info,
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
};

#[fasync::run_singlethreaded(test)]
async fn simple_test() {
    syslog::init_with_tags(&["blobfs_stress_test"]).unwrap();

    // TODO(xbhatnag): Find a way to seed the RNG
    let mut temp_rng = SmallRng::from_entropy();
    let seed: u128 = temp_rng.gen();
    let rng = SmallRng::from_seed(seed.to_le_bytes());

    info!("TEST SEED = {}", seed);

    let (_test, root_dir) = init_blobfs().await;

    let mut state = BlobfsState::new(root_dir, rng);

    // Do 10 random operations on the disk
    for _ in 0..10 {
        state.do_random_operation().await;
    }
}
