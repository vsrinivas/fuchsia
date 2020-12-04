// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod data;
pub mod io;

use {
    fidl_fuchsia_device::{ControllerMarker, ControllerProxy},
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_hardware_block_volume::{
        VolumeManagerMarker, VolumeManagerProxy, VolumeMarker, VolumeProxy,
    },
    fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    fuchsia_component::client::connect_to_service_at_path,
    fuchsia_zircon::{sys::zx_status_t, AsHandleRef, Rights, Status, Vmo},
    io::Directory,
    isolated_driver_manager::{bind_fvm, rebind_fvm},
    log::{info, set_logger, set_max_level, LevelFilter},
    ramdevice_client::{RamdiskClient, VmoRamdiskClientBuilder},
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
    std::{
        fmt::Debug,
        fs::OpenOptions,
        io::{stdout, Write},
        os::{raw::c_int, unix::io::AsRawFd},
        path::PathBuf,
        time::Duration,
    },
    test_utils_lib::{
        events::{Event, Started},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
    },
};

#[link(name = "fs-management")]
extern "C" {
    // This function initializes FVM on a fuchsia.hardware.block.Block device
    // with a given slice size.
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

async fn start_test() -> OpaqueTest {
    let test: OpaqueTest =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/isolated-devmgr#meta/isolated-devmgr.cm")
            .await
            .unwrap();

    // Wait for the root component to start
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut started_event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
    event_source.start_component_tree().await;
    EventMatcher::ok().moniker(".").expect_match::<Started>(&mut started_event_stream).await;

    test
}

fn create_ramdisk(test: &OpaqueTest, vmo: &Vmo, ramdisk_block_size: u64) -> RamdiskClient {
    // Wait until the ramctl driver is available
    let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
    let ramctl_path = dev_path.join("misc/ramctl");
    let ramctl_path = ramctl_path.to_str().unwrap();
    ramdevice_client::wait_for_device(ramctl_path, Duration::from_secs(20)).unwrap();

    let duplicated_handle = vmo.as_handle_ref().duplicate(Rights::SAME_RIGHTS).unwrap();
    let duplicated_vmo = Vmo::from(duplicated_handle);

    // Create the ramdisks
    let dev_root = OpenOptions::new().read(true).write(true).open(&dev_path).unwrap();
    VmoRamdiskClientBuilder::new(duplicated_vmo)
        .block_size(ramdisk_block_size)
        .dev_root(dev_root)
        .build()
        .unwrap()
}

fn init_fvm(ramdisk_path: &str, fvm_slice_size: u64) {
    // Create the FVM filesystem
    let ramdisk_file = OpenOptions::new().read(true).write(true).open(ramdisk_path).unwrap();
    let ramdisk_fd = ramdisk_file.as_raw_fd();
    let status = unsafe { fvm_init(ramdisk_fd, fvm_slice_size as usize) };
    Status::ok(status).unwrap();
}

async fn start_fvm_driver(ramdisk_path: &str) -> (ControllerProxy, VolumeManagerProxy) {
    let controller = connect_to_service_at_path::<ControllerMarker>(ramdisk_path).unwrap();
    bind_fvm(&controller).await.unwrap();

    // Wait until the FVM driver is available
    let fvm_path = PathBuf::from(ramdisk_path).join("fvm");
    let fvm_path = fvm_path.to_str().unwrap();
    ramdevice_client::wait_for_device(fvm_path, Duration::from_secs(20)).unwrap();

    // Connect to the Volume Manager
    let proxy = connect_to_service_at_path::<VolumeManagerMarker>(fvm_path).unwrap();
    (controller, proxy)
}

async fn does_guid_match(volume_proxy: &VolumeProxy, expected_instance_guid: &Guid) -> bool {
    // The GUIDs must match
    let (status, actual_guid_instance) = volume_proxy.get_instance_guid().await.unwrap();

    // The ramdisk is also a block device, but does not support the Volume protocol
    if let Err(Status::NOT_SUPPORTED) = Status::ok(status) {
        return false;
    }

    let actual_guid_instance = actual_guid_instance.unwrap();
    *actual_guid_instance == *expected_instance_guid
}

/// Creates a storage stress test instance.
/// This instance holds structs that control the v2 component tree,
/// the driver controller, the ramdisk and FVM volumes.
///
/// NOTE: The order of fields in this struct is important.
/// Destruction happens top-down. Test must be destroyed last.
pub struct TestInstance {
    volume_manager: VolumeManagerProxy,
    controller: ControllerProxy,
    ramdisk: RamdiskClient,
    test: OpaqueTest,
}

impl TestInstance {
    /// Create a test instance from the given VMO and initialize the ramdisk with FVM layout.
    pub async fn init(vmo: &Vmo, fvm_slice_size: u64, ramdisk_block_size: u64) -> Self {
        let test = start_test().await;
        let ramdisk = create_ramdisk(&test, vmo, ramdisk_block_size);

        let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        init_fvm(ramdisk_path, fvm_slice_size);
        let (controller, volume_manager) = start_fvm_driver(ramdisk_path).await;

        Self { test, controller, ramdisk, volume_manager }
    }

    /// Kill the test's component manager process.
    /// This should take down the entire test's component tree with it,
    /// including the driver manager and ramdisk + fvm drivers.
    pub fn kill_component_manager(mut self) {
        self.test.component_manager_app.kill().unwrap();

        // We do not want the ramdisk to be destroyed cleanly.
        // Forget the ramdisk struct.
        std::mem::forget(self.ramdisk);
    }

    /// Force rebind the FVM driver. This is similar to a device disconnect/reconnect.
    pub async fn rebind_fvm_driver(&mut self) {
        rebind_fvm(&self.controller).await.unwrap();
    }

    /// Create a test instance from the given VMO. Assumes that the ramdisk already has
    /// the FVM layout on it.
    pub async fn existing(vmo: &Vmo, ramdisk_block_size: u64) -> Self {
        let test = start_test().await;
        let ramdisk = create_ramdisk(&test, &vmo, ramdisk_block_size);

        let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        let (controller, volume_manager) = start_fvm_driver(ramdisk_path).await;

        Self { test, controller, ramdisk, volume_manager }
    }

    /// Get the full path to /dev/class/block from the devmgr running in this test
    pub fn block_path(&self) -> PathBuf {
        self.test.get_hub_v2_path().join("exec/expose/dev/class/block")
    }

    /// Create a new FVM volume with the given name and type GUID. This volume will consume
    /// exactly 1 slice. Returns the instance GUID used to uniquely identify this volume.
    pub async fn new_volume(&mut self, name: &str, mut type_guid: Guid) -> Guid {
        // Generate a random instance GUID
        let mut rng = SmallRng::from_entropy();
        let mut instance_guid = Guid { value: rng.gen() };

        // Create the new volume
        let status = self
            .volume_manager
            .allocate_partition(1, &mut type_guid, &mut instance_guid, name, 0)
            .await
            .unwrap();
        Status::ok(status).unwrap();

        instance_guid
    }

    /// Get the full path to a volume in this test that matches the given instance GUID.
    /// This function will wait until a matching volume is found.
    pub async fn get_volume_path(&self, instance_guid: &Guid) -> PathBuf {
        get_volume_path(self.block_path(), instance_guid).await
    }
}

// A simple logger that prints to stdout
pub struct StdoutLogger;

impl StdoutLogger {
    pub fn init(filter: LevelFilter) {
        set_logger(&StdoutLogger).expect("Failed to set StdoutLogger as global logger");
        set_max_level(filter);
    }
}

impl log::Log for StdoutLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.level() {
                log::Level::Info => {
                    println!("{}", record.args());
                }
                log::Level::Error => {
                    eprintln!("{}: {}", record.level(), record.args());
                }
                _ => {
                    println!("{}: {}", record.level(), record.args());
                }
            }
        }
    }

    fn flush(&self) {
        stdout().flush().unwrap();
    }
}

/// This trait helps configure the stress test environment by setting up logging,
/// crash handling and random number generation.
pub trait Environment: 'static + Clone + Debug + Send + Sync {
    /// Initialize a logger, if necessary.
    fn init_logger(&self);

    /// The seed to be used for this stress test environment.
    /// If not set, a random seed will be used.
    fn seed(&self) -> Option<u128>;

    /// Returns the RNG to be used for this test.
    fn setup_env(&self) -> SmallRng {
        self.init_logger();

        // Initialize seed
        let seed = match self.seed() {
            Some(seed) => seed,
            None => random_seed(),
        };
        let rng = SmallRng::from_seed(seed.to_le_bytes());

        info!("--------------------- stressor is starting -----------------------");
        info!("ENVIRONMENT = {:#?}", self);
        info!("SEED FOR THIS INVOCATION = {}", seed);
        info!("------------------------------------------------------------------");

        // Setup a panic handler that prints out details of this invocation
        let self_clone = self.clone();
        let seed = seed.clone();
        let default_panic_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |panic_info| {
            eprintln!("");
            eprintln!("--------------------- stressor has crashed -----------------------");
            eprintln!("ENVIRONMENT = {:#?}", self_clone);
            eprintln!("SEED FOR THIS INVOCATION = {}", seed);
            eprintln!("------------------------------------------------------------------");
            eprintln!("");
            default_panic_hook(panic_info);
        }));

        rng
    }
}

/// Use entropy to generate a random seed
fn random_seed() -> u128 {
    let mut temp_rng = SmallRng::from_entropy();
    temp_rng.gen()
}

/// Gets the full path to a volume matching the given instance GUID at the given
/// /dev/class/block path. This function will wait until a matching volume is found.
pub async fn get_volume_path(block_path: PathBuf, instance_guid: &Guid) -> PathBuf {
    let dir = Directory::from_namespace(block_path.clone(), OPEN_RIGHT_READABLE).unwrap();
    loop {
        // TODO(xbhatnag): Find a better way to wait for the volume to appear
        for entry in dir.entries().await.unwrap() {
            let volume_path = block_path.join(entry);
            let volume_path_str = volume_path.to_str().unwrap();

            // Connect to the Volume FIDL protocol
            let volume_proxy = connect_to_service_at_path::<VolumeMarker>(volume_path_str).unwrap();
            if does_guid_match(&volume_proxy, &instance_guid).await {
                return volume_path;
            }
        }
    }
}
