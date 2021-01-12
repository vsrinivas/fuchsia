// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod fvm;
pub mod operator;

use {
    anyhow::Error,
    argh::FromArgs,
    fasync::{Task, TimeoutExt},
    fidl_fuchsia_hardware_block_partition::Guid,
    fuchsia_async as fasync,
    fuchsia_zircon::Vmo,
    futures::{future::join_all, SinkExt},
    fvm::Volume,
    log::{debug, info, LevelFilter},
    operator::VolumeOperator,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::{thread::sleep, time::Duration},
    stress_test_utils::{fvm::FvmInstance, random_seed, StdoutLogger},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of fvm and performs stressful operations on it
struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u128>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

    /// number of volumes in FVM.
    /// each volume operates on a different thread and will perform
    /// the required number of operations before exiting.
    /// defaults to 3 volumes.
    #[argh(option, short = 'n', default = "3")]
    num_volumes: u64,

    /// size of one block of the ramdisk (in bytes)
    #[argh(option, default = "512")]
    ramdisk_block_size: u64,

    /// number of blocks in the ramdisk
    /// defaults to 106MiB ramdisk
    #[argh(option, default = "217088")]
    ramdisk_block_count: u64,

    /// size of one slice in FVM (in bytes)
    #[argh(option, default = "32768")]
    fvm_slice_size: u64,

    /// limits the maximum slices in a single extend operation
    #[argh(option, default = "1024")]
    max_slices_in_extend: u64,

    /// controls the density of the partition.
    #[argh(option, default = "65536")]
    max_vslice_count: u64,

    /// controls how often volumes are force-disconnected,
    /// either by crashing FVM or by rebinding the driver.
    /// disabled if set to 0.
    #[argh(option, default = "0")]
    disconnect_secs: u64,

    /// when force-disconnection is enabled, this
    /// defines the probability with which a rebind
    /// happens instead of a crash.
    #[argh(option, default = "0.0")]
    rebind_probability: f64,
}

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

pub async fn run_test(
    mut rng: SmallRng,
    ramdisk_block_count: u64,
    fvm_slice_size: u64,
    ramdisk_block_size: u64,
    num_volumes: u64,
    max_slices_in_extend: u64,
    max_vslice_count: u64,
    disconnect_secs: u64,
    rebind_probability: f64,
    time_limit_secs: Option<u64>,
    num_operations: Option<u64>,
) {
    let vmo_size = ramdisk_block_count * ramdisk_block_size;

    // Create the VMO that the ramdisk is backed by
    let vmo = Vmo::create(vmo_size).unwrap();

    // Initialize the ramdisk and setup FVM.
    let mut instance = FvmInstance::new(true, &vmo, fvm_slice_size, ramdisk_block_size).await;

    let mut tasks = vec![];
    let mut senders = vec![];

    for i in 0..num_volumes {
        // Make a new RNG for this volume
        let volume_rng_seed: u128 = rng.gen();
        let volume_rng = SmallRng::from_seed(volume_rng_seed.to_le_bytes());

        // Create the new volume
        let volume_name = format!("testpart-{}", i);
        let instance_guid = instance.new_volume(&volume_name, TYPE_GUID).await;

        // Connect to the volume
        let (volume, sender) = Volume::new(instance_guid, fvm_slice_size).await;

        // Create the operator
        let operator =
            VolumeOperator::new(volume, volume_rng, max_slices_in_extend, max_vslice_count);

        // Start the operator
        let task = operator.run(num_operations.unwrap_or(u64::MAX));

        tasks.push(task);
        senders.push(sender);
    }

    // Send the initial block path to all operators
    for sender in senders.iter_mut() {
        let _ = sender.send(instance.block_path()).await;
    }

    // Create the disconnection task in a new thread
    if disconnect_secs > 0 {
        Task::blocking(async move {
            loop {
                sleep(Duration::from_secs(disconnect_secs));

                if rng.gen_bool(rebind_probability) {
                    debug!("Rebinding FVM driver");
                    instance.rebind_fvm_driver().await;
                } else {
                    // Crash the old instance and replace it with a new instance.
                    // This will cause the component tree to be taken down abruptly.
                    debug!("Killing component manager");
                    instance.kill_component_manager();
                    instance =
                        FvmInstance::new(false, &vmo, fvm_slice_size, ramdisk_block_size).await;
                }

                // Give the new block path to the operators.
                // Ignore the result because some operators may have completed.
                let path = instance.block_path();
                for sender in senders.iter_mut() {
                    let _ = sender.send(path.clone()).await;
                }
            }
        })
        .detach();
    }

    let operator_tasks = join_all(tasks);

    if let Some(time_limit_secs) = time_limit_secs {
        operator_tasks.on_timeout(Duration::from_secs(time_limit_secs), || vec![]).await;
    } else {
        operator_tasks.await;
    };
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let args: Args = argh::from_env();

    let filter = args.log_filter.unwrap_or(LevelFilter::Info);
    StdoutLogger::init(filter);

    // Initialize seed
    let seed = match args.seed {
        Some(seed) => seed,
        None => random_seed(),
    };
    let rng = SmallRng::from_seed(seed.to_le_bytes());

    info!("--------------------- stressor is starting -----------------------");
    info!("ENVIRONMENT = {:#?}", args);
    info!("SEED FOR THIS INVOCATION = {}", seed);
    info!("------------------------------------------------------------------");

    // Setup a panic handler that prints out details of this invocation
    let args_clone = args.clone();
    let seed = seed.clone();
    let default_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        eprintln!("");
        eprintln!("--------------------- stressor has crashed -----------------------");
        eprintln!("ENVIRONMENT = {:#?}", args_clone);
        eprintln!("SEED FOR THIS INVOCATION = {}", seed);
        eprintln!("------------------------------------------------------------------");
        eprintln!("");
        default_panic_hook(panic_info);
    }));

    run_test(
        rng,
        args.ramdisk_block_count,
        args.fvm_slice_size,
        args.ramdisk_block_size,
        args.num_volumes,
        args.max_slices_in_extend,
        args.max_vslice_count,
        args.disconnect_secs,
        args.rebind_probability,
        args.time_limit_secs,
        args.num_operations,
    )
    .await;

    info!("Stress test is exiting successfully!");

    Ok(())
}
