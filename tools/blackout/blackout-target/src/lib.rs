// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for target side of filesystem integrity host-target interaction tests

#![deny(missing_docs)]

use {
    anyhow::Result,
    async_trait::async_trait,
    fidl_fuchsia_blackout_test::{ControllerRequest, ControllerRequestStream},
    fidl_fuchsia_device::ControllerMarker,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_fs,
    fuchsia_fs::directory::readdir,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    rand::{distributions, rngs::StdRng, Rng, SeedableRng},
};

pub mod static_tree;

/// The three steps the target-side of a blackout test needs to implement.
#[async_trait]
pub trait Test {
    /// Setup the test run on the given block_device.
    async fn setup(&self, block_device: String, seed: u64) -> Result<()>;
    /// Run the test body on the given block_device.
    async fn test(&self, block_device: String, seed: u64) -> Result<()>;
    /// Verify the consistency of the filesystem on the block_device.
    async fn verify(&self, block_device: String, seed: u64) -> Result<()>;
}

struct BlackoutController(ControllerRequestStream);

/// A test server, which serves the fuchsia.blackout.test.Controller protocol.
pub struct TestServer<'a, T> {
    fs: ServiceFs<ServiceObj<'a, BlackoutController>>,
    test: T,
}

impl<'a, T> TestServer<'a, T>
where
    T: Test + Copy,
{
    /// Create a new test server for this test.
    pub fn new(test: T) -> Result<TestServer<'a, T>> {
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service(BlackoutController);
        fs.take_and_serve_directory_handle()?;

        Ok(TestServer { fs, test })
    }

    /// Start serving the outgoing directory. Blocks until all connections are closed.
    pub async fn serve(self) {
        const MAX_CONCURRENT: usize = 10_000;
        let test = self.test;
        self.fs
            .for_each_concurrent(MAX_CONCURRENT, move |stream| {
                handle_request(test, stream).unwrap_or_else(|e| tracing::error!("{}", e))
            })
            .await;
    }
}

async fn handle_request<T: Test + Copy>(
    test: T,
    BlackoutController(mut stream): BlackoutController,
) -> Result<()> {
    while let Some(request) = stream.try_next().await? {
        handle_controller(test, request).await?;
    }

    Ok(())
}

async fn handle_controller<T: Test + Copy>(test: T, request: ControllerRequest) -> Result<()> {
    match request {
        ControllerRequest::Setup { responder, device_path, seed } => {
            let mut res = test.setup(device_path, seed).await.map_err(|e| {
                tracing::error!("{}", e);
                zx::Status::INTERNAL.into_raw()
            });
            responder.send(&mut res)?;
        }
        ControllerRequest::Test { device_path, seed, .. } => test.test(device_path, seed).await?,
        ControllerRequest::Verify { responder, device_path, seed } => {
            let mut res = test.verify(device_path, seed).await.map_err(|e| {
                // The test tries failing on purpose, so only print errors as warnings.
                tracing::warn!("{}", e);
                zx::Status::BAD_STATE.into_raw()
            });
            responder.send(&mut res)?;
        }
    }

    Ok(())
}

/// Generate a Vec<u8> of random bytes from a seed using a standard distribution.
pub fn generate_content(seed: u64) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(seed);

    let size = rng.gen_range(1..1 << 16);
    rng.sample_iter(&distributions::Standard).take(size).collect()
}

/// Find the device in /dev/class/block that represents a given topological path. Returns the full
/// path of the device in /dev/class/block.
pub async fn find_dev(dev: &str) -> Result<String> {
    let dev_class_block = fuchsia_fs::directory::open_in_namespace(
        "/dev/class/block",
        fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
    )?;
    for entry in readdir(&dev_class_block).await? {
        let path = format!("/dev/class/block/{}", entry.name);
        let proxy = connect_to_protocol_at_path::<ControllerMarker>(&path)?;
        let topo_path = proxy.get_topological_path().await?.map_err(|s| zx::Status::from_raw(s))?;
        println!("{} => {}", path, topo_path);
        if dev == topo_path {
            return Ok(path);
        }
    }
    Err(anyhow::anyhow!("Couldn't find {} in /dev/class/block", dev))
}
