// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::gpt,
    anyhow::{Context, Error},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_fshost::{BlockWatcherMarker, BlockWatcherProxy},
    fidl_fuchsia_hardware_block_partition::PartitionMarker,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::prelude::*,
    payload_streamer::PayloadStreamer,
    ramdevice_client::{RamdiskClient, RamdiskClientBuilder},
    std::fs::File,
};

/// Block size of the ramdisk.
const BLOCK_SIZE: u64 = 512;

/// Returns the size in bytes of the partition at `path`.
pub async fn get_partition_size(path: &str) -> Result<usize, Error> {
    let (status, size) =
        fuchsia_component::client::connect_to_protocol_at_path::<PartitionMarker>(path)
            .context("connecting to partition")?
            .get_info()
            .await
            .context("sending get info request")?;
    zx::Status::ok(status).context("getting partition info")?;
    let size = size.unwrap();

    Ok((size.block_size as usize) * (size.block_count as usize))
}

struct BlockWatcherPauser {
    connection: Option<BlockWatcherProxy>,
}

impl BlockWatcherPauser {
    async fn new() -> Result<Self, Error> {
        let connection = fuchsia_component::client::connect_to_protocol::<BlockWatcherMarker>()
            .context("connecting to block watcher")?;
        connection.pause().await.context("pausing the block watcher")?;

        Ok(Self { connection: Some(connection) })
    }

    async fn resume(&mut self) -> Result<(), Error> {
        if let Some(conn) = self.connection.take() {
            conn.resume().await.context("resuming the block watcher")?;
        }
        Ok(())
    }
}

impl Drop for BlockWatcherPauser {
    fn drop(&mut self) {
        if self.connection.is_some() {
            let mut executor =
                fuchsia_async::LocalExecutor::new().expect("failed to create executor");
            executor
                .run_singlethreaded(self.resume())
                .map_err(|e| fx_log_err!("failed to resume block watcher: {:?}", e))
                .ok();
        }
    }
}

pub struct FvmRamdisk {
    ramdisk: RamdiskClient,
    sparse_fvm_path: String,
    pauser: BlockWatcherPauser,
}

impl FvmRamdisk {
    pub async fn new(ramdisk_size: u64, sparse_fvm_path: String) -> Result<Self, Error> {
        Self::new_with_physmemfn(ramdisk_size, sparse_fvm_path, zx::system_get_physmem).await
    }

    async fn new_with_physmemfn<PhysmemFn>(
        ramdisk_size: u64,
        sparse_fvm_path: String,
        get_physmem_size: PhysmemFn,
    ) -> Result<Self, Error>
    where
        PhysmemFn: Fn() -> u64,
    {
        let physmem_size = get_physmem_size();
        if ramdisk_size >= physmem_size {
            return Err(anyhow::anyhow!(
                "Not enough available physical memory to create the ramdisk!"
            ));
        }
        let pauser = BlockWatcherPauser::new().await.context("pausing block watcher")?;
        let ramdisk = Self::create_ramdisk_with_fvm(ramdisk_size)
            .await
            .context("Creating ramdisk with partition table")?;

        let ret = FvmRamdisk { ramdisk, sparse_fvm_path, pauser };

        // Manually bind the GPT driver now that the disk contains a valid GPT, so that
        // the paver can pave to the disk.
        ret.rebind_gpt_driver().await.context("Rebinding GPT driver")?;
        Ok(ret)
    }

    /// Pave the FVM into this ramdisk, consuming this object
    /// and leaving the ramdisk to run once the application has quit.
    pub async fn pave_fvm(mut self) -> Result<(), Error> {
        fx_log_info!("connecting to the paver");
        let channel = self.ramdisk.open().context("Opening ramdisk")?;
        let paver =
            fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_paver::PaverMarker>()?;
        let (data_sink, remote) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_paver::DynamicDataSinkMarker>()?;
        paver.use_block_device(fidl::endpoints::ClientEnd::new(channel), remote)?;

        let size =
            get_partition_size(&self.sparse_fvm_path).await.context("getting partition size")?;
        let file = File::open(&self.sparse_fvm_path)?;
        let streamer = PayloadStreamer::new(Box::new(file), size);
        let (client, mut server) =
            fidl::endpoints::create_request_stream::<fidl_fuchsia_paver::PayloadStreamMarker>()?;

        fasync::Task::spawn(async move {
            while let Some(req) = server.try_next().await.expect("Failed to get request") {
                streamer.handle_request(req).await.expect("Failed to handle request!");
            }
        })
        .detach();
        data_sink.write_volumes(client).await.context("writing volumes")?;
        self.pauser.resume().await.context("resuming block watcher")?;
        // Now that the ramdisk has successfully been paved, and the block watcher resumed,
        // we need to force a rebind of everything so that the block watcher sees the final
        // state of the FVM partition. Without this, the block watcher ignores the FVM because
        // it was paused when it appeared.
        self.rebind_gpt_driver().await.context("rebinding GPT driver")?;
        // Forget the ramdisk, so that we don't run its destructor (which closes it).
        // This means that we don't have to keep running to keep the ramdisk around.
        std::mem::forget(self.ramdisk);
        Ok(())
    }

    /// Creates a ramdisk and writes a suitable partition table to it,
    /// and automatically rebinds the appropriate drivers so the GPT is parsed.
    async fn create_ramdisk_with_fvm(size: u64) -> Result<RamdiskClient, Error> {
        let blocks = size / BLOCK_SIZE;
        let ramdisk =
            RamdiskClientBuilder::new(BLOCK_SIZE, blocks).build().context("Building ramdisk")?;
        let channel = ramdisk.open().context("Opening ramdisk")?;

        let file: File = fdio::create_fd(channel.into()).context("Creating FD")?;
        gpt::write_ramdisk(Box::new(file)).context("writing GPT")?;

        Ok(ramdisk)
    }

    /// Explicitly binds the GPT driver to the given ramdisk.
    async fn rebind_gpt_driver(&self) -> Result<(), Error> {
        let channel = self.ramdisk.open()?;
        let controller = ControllerProxy::new(fidl::AsyncChannel::from_channel(channel)?);
        controller.rebind("gpt.so").await?.map_err(zx::Status::from_raw)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_not_enough_physmem() {
        assert!(FvmRamdisk::new_with_physmemfn(512, "".to_owned(), || 512).await.is_err());
        assert!(FvmRamdisk::new_with_physmemfn(512, "".to_owned(), || 6).await.is_err());
    }
}
