// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod operator;

use {
    anyhow::Error,
    argh::FromArgs,
    fidl_fuchsia_hardware_block_partition::Guid,
    fs_management::Minfs,
    fuchsia_async as fasync,
    fuchsia_async::{Task, TimeoutExt},
    fuchsia_zircon::Vmo,
    log::debug,
    log::info,
    log::LevelFilter,
    operator::MinfsOperator,
    rand::{rngs::SmallRng, SeedableRng},
    std::thread::sleep,
    std::time::Duration,
    stress_test_utils::fvm::{get_volume_path, FvmInstance},
    stress_test_utils::{random_seed, StdoutLogger},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of minfs and performs stressful operations on it
struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u128>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

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

    /// controls how often minfs is disconnected by
    /// crashing the block device.
    /// disabled if set to 0.
    #[argh(option, default = "0")]
    disconnect_secs: u64,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,
}

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

// The path to the minfs filesystem in the test's namespace
const MINFS_MOUNT_PATH: &str = "/minfs";

pub async fn run_test(
    rng: SmallRng,
    ramdisk_block_count: u64,
    ramdisk_block_size: u64,
    fvm_slice_size: u64,
    num_operations: Option<u64>,
    disconnect_secs: u64,
    time_limit_secs: Option<u64>,
) {
    // Create the VMO that the ramdisk is backed by
    let vmo_size = ramdisk_block_count * ramdisk_block_size;
    let vmo = Vmo::create(vmo_size).unwrap();

    // Initialize the ramdisk and setup FVM.
    let mut instance = FvmInstance::new(true, &vmo, fvm_slice_size, ramdisk_block_size).await;

    // Create a minfs volume
    let volume_instance_guid = instance.new_volume("minfs", TYPE_GUID).await;

    // Find the path to the volume
    let block_path = instance.block_path();
    let mut volume_path = get_volume_path(block_path, &volume_instance_guid).await;

    // Initialize minfs for the first time
    let mut minfs = Minfs::new(volume_path.to_str().unwrap()).unwrap();
    minfs.format().unwrap();

    if disconnect_secs > 0 {
        Task::blocking(async move {
            // Crash the block device every |disconnect_secs|.
            loop {
                {
                    // Start up minfs
                    let mut minfs = Minfs::new(volume_path.to_str().unwrap()).unwrap();
                    minfs.fsck().unwrap();
                    minfs.mount(MINFS_MOUNT_PATH).unwrap();

                    // Wait for the required amount of time
                    sleep(Duration::from_secs(disconnect_secs));

                    // Crash the old instance and replace it with a new instance.
                    // This will cause the component tree to be taken down abruptly.
                    debug!("Killing component manager");
                    instance.kill_component_manager();

                    // Minfs may not neatly terminate. Force kill the process.
                    let result = minfs.kill();
                    debug!("Minfs kill result = {:?}", result);
                }

                // Start up a new instance
                instance = FvmInstance::new(false, &vmo, fvm_slice_size, ramdisk_block_size).await;
                let block_path = instance.block_path();
                volume_path = get_volume_path(block_path, &volume_instance_guid).await;
            }
        })
        .detach();
    } else {
        // Start up minfs
        minfs.fsck().unwrap();
        minfs.mount(MINFS_MOUNT_PATH).unwrap();
    }

    // Run the operator in a new thread
    let operator_task = Task::blocking(async move {
        let operator = MinfsOperator::new(rng).await;
        operator.do_random_operations(num_operations.unwrap_or(u64::MAX)).await;
    });

    if let Some(time_limit_secs) = time_limit_secs {
        operator_task.on_timeout(Duration::from_secs(time_limit_secs), || (())).await;
    } else {
        operator_task.await;
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
        args.ramdisk_block_size,
        args.fvm_slice_size,
        args.num_operations,
        args.disconnect_secs,
        args.time_limit_secs,
    )
    .await;

    info!("Stress test is exiting successfully!");

    Ok(())
}
