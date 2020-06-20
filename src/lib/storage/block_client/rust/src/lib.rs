// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Error},
    fidl_fuchsia_hardware_block as block,
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::task::AtomicWaker,
    std::{
        collections::HashMap,
        convert::TryInto,
        future::Future,
        pin::Pin,
        sync::{Arc, Mutex},
        task::{Context, Poll, Waker},
    },
};

const BLOCK_VMOID_INVALID: u16 = 0;

const BLOCKIO_READ: u32 = 1;
const BLOCKIO_WRITE: u32 = 2;
const _BLOCKIO_FLUSH: u32 = 3;
const _BLOCKIO_TRIM: u32 = 4;
const BLOCKIO_CLOSE_VMO: u32 = 5;

#[repr(C)]
#[derive(Default)]
struct BlockFifoRequest {
    op_code: u32,
    request_id: u32,
    group_id: u16,
    vmoid: u16,
    block_count: u32,
    vmo_block: u64,
    device_block: u64,
}

#[repr(C)]
struct BlockFifoResponse {
    status: i32,
    request_id: u32,
    group_id: u16,
    reserved1: u16,
    count: u32,
    reserved2: u64,
    reserved3: u64,
}

unsafe impl fasync::FifoEntry for BlockFifoRequest {}
unsafe impl fasync::FifoEntry for BlockFifoResponse {}

/// Represents a slice of a buffer to be used for reading or writing.
pub trait BufferSlice<'a> {
    /// Returns the attached vmo-id.  For now, this method must always return something, but
    /// eventually, it will be possible for a temporary VMO to be used albeit with a performance
    /// penalty.
    fn get_vmo_id(&self) -> Option<&'a VmoId>;

    /// The offset, in bytes, for the slice.
    fn offset(&self) -> u64;

    /// The length, in bytes, for the slice.
    fn length(&self) -> u64;
}

/// Concrete buffer slice type for a VMO.
pub struct VmoIdBufferSlice<'a> {
    vmo_id: &'a VmoId,
    offset: u64,
    length: u64,
}

impl<'a> VmoIdBufferSlice<'a> {
    /// Returns a new VmoIdBufferSlice instance.
    fn new(vmo_id: &'a VmoId, offset: u64, length: u64) -> Self {
        Self { vmo_id, offset, length }
    }
}

impl<'a> BufferSlice<'a> for VmoIdBufferSlice<'a> {
    fn get_vmo_id(&self) -> Option<&'a VmoId> {
        Some(self.vmo_id)
    }
    fn offset(&self) -> u64 {
        self.offset
    }
    fn length(&self) -> u64 {
        self.length
    }
}

#[derive(Default)]
struct RequestState {
    result: Option<zx::Status>,
    waker: Option<Waker>,
}

#[derive(Default)]
struct Requests {
    // The next request ID to be used.
    next_request_id: u32,

    // Map from request ID to RequestState.
    map: HashMap<u32, RequestState>,

    // Set to true when terminated.
    terminate: bool,
}

struct FifoState {
    // The fifo.
    fifo: fasync::Fifo<BlockFifoResponse, BlockFifoRequest>,

    // Inflight requests, and related information.
    requests: Mutex<Requests>,

    // The waker for the FifoPoller to be used upon termination.
    poller_waker: AtomicWaker,
}

// A future used for fifo responses.
struct ResponseFuture {
    request_id: u32,
    fifo_state: Arc<FifoState>,
}

impl ResponseFuture {
    fn new(fifo_state: Arc<FifoState>) -> Self {
        let request_id;
        {
            let mut requests = fifo_state.requests.lock().unwrap();
            request_id = requests.next_request_id;
            requests.next_request_id = requests.next_request_id.overflowing_add(1).0;
            assert!(
                requests.map.insert(request_id, RequestState::default()).is_none(),
                "request id in use!"
            );
        }
        ResponseFuture { request_id, fifo_state }
    }
}

impl Future for ResponseFuture {
    type Output = Result<(), zx::Status>;

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut requests = self.fifo_state.requests.lock().unwrap();
        let terminate = requests.terminate;
        let request_state = requests.map.get_mut(&self.request_id).unwrap();
        if let Some(result) = request_state.result {
            Poll::Ready(result.into())
        } else if terminate {
            Poll::Ready(Err(zx::Status::CANCELED))
        } else {
            request_state.waker.replace(context.waker().clone());
            Poll::Pending
        }
    }
}

impl Drop for ResponseFuture {
    fn drop(&mut self) {
        self.fifo_state.requests.lock().unwrap().map.remove(&self.request_id).unwrap();
    }
}

/// Wraps a vmo-id. Will panic if you forget to detach.
pub struct VmoId(u16);

impl VmoId {
    fn take(mut self) -> u16 {
        let id = self.0;
        self.0 = BLOCK_VMOID_INVALID;
        id
    }

    fn id(&self) -> u16 {
        self.0
    }
}

impl Drop for VmoId {
    fn drop(&mut self) {
        assert_eq!(self.0, BLOCK_VMOID_INVALID, "Did you forget to detach?");
    }
}

/// Represents a connection to a remote block device.
pub struct RemoteBlockDevice {
    device: Mutex<block::BlockSynchronousProxy>,
    block_size: u32,
    fifo_state: Arc<FifoState>,
}

impl RemoteBlockDevice {
    /// Returns a connection to a remote block device via the given channel.
    pub fn new(channel: zx::Channel) -> Result<Self, Error> {
        let mut block_device = block::BlockSynchronousProxy::new(channel);
        let (status, maybe_info) = block_device.get_info(zx::Time::INFINITE)?;
        let info = maybe_info.ok_or(zx::Status::from_raw(status))?;
        let (status, maybe_fifo) = block_device.get_fifo(zx::Time::INFINITE)?;
        let fifo = fasync::Fifo::from_fifo(maybe_fifo.ok_or(zx::Status::from_raw(status))?)?;
        let fifo_state = Arc::new(FifoState {
            fifo,
            requests: Default::default(),
            poller_waker: AtomicWaker::new(),
        });
        let device = Self {
            device: Mutex::new(block_device),
            block_size: info.block_size,
            fifo_state: fifo_state.clone(),
        };
        fasync::spawn(FifoPoller { fifo_state });
        Ok(device)
    }

    /// Wraps AttachVmo from fuchsia.hardware.block::Block.
    pub fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error> {
        let mut device = self.device.lock().unwrap();
        let (status, maybe_vmo_id) = device
            .attach_vmo(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, zx::Time::INFINITE)?;
        Ok(VmoId(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id))
    }

    async fn send(&self, mut request: BlockFifoRequest) -> Result<(), Error> {
        let reply = ResponseFuture::new(self.fifo_state.clone());
        request.request_id = reply.request_id;
        self.fifo_state.fifo.write_entries(std::slice::from_ref(&request)).await?;
        Ok(reply.await?)
    }

    /// Detaches the given vmo-id from the device.
    pub async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        self.send(BlockFifoRequest {
            op_code: BLOCKIO_CLOSE_VMO,
            vmoid: vmo_id.take(),
            ..Default::default()
        })
        .await
    }

    fn to_blocks(&self, bytes: u64) -> Result<u64, Error> {
        ensure!(bytes % self.block_size as u64 == 0, "bad alignment");
        Ok(bytes / self.block_size as u64)
    }

    /// Reads from the device at |device_offset| into the given buffer slice.
    pub async fn read<'a, B: BufferSlice<'a>>(
        &self,
        buffer_slice: B,
        device_offset: u64,
    ) -> Result<(), Error> {
        self.send(BlockFifoRequest {
            op_code: BLOCKIO_READ,
            vmoid: buffer_slice.get_vmo_id().unwrap().id(),
            block_count: self.to_blocks(buffer_slice.length())?.try_into()?,
            vmo_block: self.to_blocks(buffer_slice.offset())?,
            device_block: self.to_blocks(device_offset)?,
            ..Default::default()
        })
        .await
    }

    /// Writes the data in |buffer_slice| to the device.
    pub async fn write<'a, B: BufferSlice<'a>>(
        &self,
        buffer_slice: B,
        device_offset: u64,
    ) -> Result<(), Error> {
        self.send(BlockFifoRequest {
            op_code: BLOCKIO_WRITE,
            vmoid: buffer_slice.get_vmo_id().unwrap().id(),
            block_count: self.to_blocks(buffer_slice.length())?.try_into()?,
            vmo_block: self.to_blocks(buffer_slice.offset())?,
            device_block: self.to_blocks(device_offset)?,
            ..Default::default()
        })
        .await
    }
}

impl Drop for RemoteBlockDevice {
    fn drop(&mut self) {
        self.fifo_state.requests.lock().unwrap().terminate = true;
        self.fifo_state.poller_waker.wake();
    }
}

// FifoPoller is a future responsible for polling for response from the fifo.
struct FifoPoller {
    fifo_state: Arc<FifoState>,
}

impl Future for FifoPoller {
    type Output = ();

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut requests = self.fifo_state.requests.lock().unwrap();
        if !requests.terminate {
            self.fifo_state.poller_waker.register(context.waker());
            while let Ok(Some(response)) = futures::ready!(self.fifo_state.fifo.read(context)) {
                let request_id = response.request_id;
                // If the request isn't in the map, assume that it's a cancelled read.
                if let Some(request_state) = requests.map.get_mut(&request_id) {
                    request_state.result.replace(zx::Status::from_raw(response.status));
                    if let Some(waker) = request_state.waker.take() {
                        waker.wake();
                    }
                }
            }
        }
        requests.terminate = true;
        for (_, request_state) in requests.map.iter_mut() {
            if let Some(waker) = request_state.waker.take() {
                waker.wake();
            }
        }
        return Poll::Ready(());
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{RemoteBlockDevice, VmoIdBufferSlice},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{
            future::TryFutureExt,
            stream::{futures_unordered::FuturesUnordered, StreamExt},
        },
        ramdevice_client::RamdiskClient,
    };

    fn make_ramdisk() -> (RamdiskClient, RemoteBlockDevice) {
        isolated_driver_manager::launch_isolated_driver_manager()
            .expect("launch_isolated_driver_manager failed");
        ramdevice_client::wait_for_device("/dev/misc/ramctl", std::time::Duration::from_secs(10))
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(1024, 1024).expect("RamdiskClient::create failed");
        let remote_block_device =
            RemoteBlockDevice::new(ramdisk.open().expect("ramdisk.open failed"))
                .expect("RemoteBlockDevice::new failed");
        assert_eq!(remote_block_device.block_size, 1024);
        (ramdisk, remote_block_device)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_against_ram_disk() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        vmo.write(b"hello", 5).expect("vmo.write failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        remote_block_device
            .write(VmoIdBufferSlice::new(&vmo_id, 0, 1024), 0)
            .await
            .expect("write failed");
        remote_block_device
            .read(VmoIdBufferSlice::new(&vmo_id, 1024, 2048), 0)
            .await
            .expect("read failed");
        let mut buf: [u8; 5] = Default::default();
        vmo.read(&mut buf, 1029).expect("vmo.read failed");
        assert_eq!(&buf, b"hello");
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_alignment() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        remote_block_device
            .write(VmoIdBufferSlice::new(&vmo_id, 0, 1024), 1)
            .await
            .expect_err("expected failure due to bad alignment");
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parallel_io() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        let mut reads = Vec::new();
        for _ in 0..1024 {
            reads.push(
                remote_block_device
                    .read(VmoIdBufferSlice::new(&vmo_id, 0, 1024), 0)
                    .inspect_err(|e| panic!("read should have succeeded: {}", e)),
            );
        }
        futures::future::join_all(reads).await;
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_closed_device() {
        let (ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        let mut reads = Vec::new();
        for _ in 0..1024 {
            reads.push(remote_block_device.read(VmoIdBufferSlice::new(&vmo_id, 0, 1024), 0));
        }
        let _ = futures::join!(futures::future::join_all(reads), async {
            ramdisk.destroy().expect("ramdisk.destroy failed")
        });
        remote_block_device.detach_vmo(vmo_id).await.expect_err("ramdisk should be destroyed");
    }

    // TODO(fxb/54467) Flaking test
    #[ignore]
    #[fasync::run_singlethreaded(test)]
    async fn test_cancelled_reads() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        {
            let mut reads = FuturesUnordered::new();
            for _ in 0..1024 {
                reads.push(remote_block_device.read(VmoIdBufferSlice::new(&vmo_id, 0, 1024), 0));
            }
            // Read the first 500 results and then dump the rest.
            for _ in 0..500 {
                reads.next().await;
            }
        }
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }
}
