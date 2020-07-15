// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Error},
    fidl_fuchsia_hardware_block as block,
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{channel::oneshot, task::AtomicWaker},
    std::{
        collections::HashMap,
        convert::TryInto,
        future::Future,
        pin::Pin,
        sync::{Arc, Mutex},
        task::{Context, Poll, Waker},
    },
};

pub use cache::Cache;

pub mod cache;

const BLOCK_VMOID_INVALID: u16 = 0;
const TEMP_VMO_SIZE: usize = 65536;

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

pub enum BufferSlice<'a> {
    VmoId { vmo_id: &'a VmoId, offset: u64, length: u64 },
    Memory(&'a [u8]),
}

impl<'a> BufferSlice<'a> {
    pub fn new_with_vmo_id(vmo_id: &'a VmoId, offset: u64, length: u64) -> Self {
        BufferSlice::VmoId { vmo_id, offset, length }
    }
}

impl<'a> From<&'a [u8]> for BufferSlice<'a> {
    fn from(buf: &'a [u8]) -> Self {
        BufferSlice::Memory(buf)
    }
}

pub enum MutableBufferSlice<'a> {
    VmoId { vmo_id: &'a VmoId, offset: u64, length: u64 },
    Memory(&'a mut [u8]),
}

impl<'a> MutableBufferSlice<'a> {
    pub fn new_with_vmo_id(vmo_id: &'a VmoId, offset: u64, length: u64) -> Self {
        MutableBufferSlice::VmoId { vmo_id, offset, length }
    }
}

impl<'a> From<&'a mut [u8]> for MutableBufferSlice<'a> {
    fn from(buf: &'a mut [u8]) -> Self {
        MutableBufferSlice::Memory(buf)
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

    // A queue of messages to be sent on the fifo.
    queue: std::collections::VecDeque<BlockFifoRequest>,

    // Map from request ID to RequestState.
    map: HashMap<u32, RequestState>,

    // Set to true when terminated.
    terminate: bool,
}

impl Requests {
    fn terminate(&mut self) {
        self.terminate = true;
        for (_, request_state) in self.map.iter_mut() {
            if let Some(waker) = request_state.waker.take() {
                waker.wake();
            }
        }
    }
}

struct FifoState {
    // The fifo.
    fifo: fasync::Fifo<BlockFifoResponse, BlockFifoRequest>,

    // Inflight requests, and related information.
    requests: Mutex<Requests>,

    // The waker for the FifoPoller to be used upon termination.
    poller_waker: AtomicWaker,
}

impl FifoState {
    async fn send(self: &Arc<Self>, mut request: BlockFifoRequest) -> Result<(), Error> {
        let request_id;
        {
            let mut requests = self.requests.lock().unwrap();
            request_id = requests.next_request_id;
            requests.next_request_id = requests.next_request_id.overflowing_add(1).0;
            assert!(
                requests.map.insert(request_id, RequestState::default()).is_none(),
                "request id in use!"
            );
            request.request_id = request_id;
            requests.queue.push_back(request);
            self.poller_waker.wake();
        }
        let reply = ResponseFuture::new(self.clone(), request_id);
        Ok(reply.await?)
    }
}

// A future used for fifo responses.
struct ResponseFuture {
    request_id: u32,
    fifo_state: Arc<FifoState>,
}

impl ResponseFuture {
    fn new(fifo_state: Arc<FifoState>, request_id: u32) -> Self {
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
    fn take(&mut self) -> VmoId {
        let vmo_id = VmoId(self.0);
        self.0 = BLOCK_VMOID_INVALID;
        vmo_id
    }

    fn into_id(mut self) -> u16 {
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
    block_count: u64,
    fifo_state: Arc<FifoState>,
    temp_vmo: futures::lock::Mutex<zx::Vmo>,
    temp_vmo_id: VmoId,
}

impl RemoteBlockDevice {
    /// Returns a connection to a remote block device via the given channel.
    pub fn new(channel: zx::Channel) -> Result<Self, Error> {
        let device = Self::from_channel(channel)?;
        fasync::spawn(FifoPoller { fifo_state: device.fifo_state.clone() });
        Ok(device)
    }

    /// Returns a connection to a remote block device via the given channel, but spawns a separate
    /// thread for polling the fifo which makes it work in cases where no executor is configured for
    /// the calling thread.
    pub fn new_sync(channel: zx::Channel) -> Result<Self, Error> {
        // The fifo needs to be instantiated from the thread that has the executor as that's where
        // the fifo registers for notifications to be delivered.
        let (sender, receiver) = oneshot::channel::<Result<Self, Error>>();
        std::thread::spawn(move || {
            let mut executor = fasync::Executor::new().expect("failed to create executor");
            let maybe_device = RemoteBlockDevice::from_channel(channel);
            let fifo_state = maybe_device.as_ref().ok().map(|device| device.fifo_state.clone());
            let _ = sender.send(maybe_device);
            if let Some(fifo_state) = fifo_state {
                executor.run_singlethreaded(FifoPoller { fifo_state });
            }
        });
        futures::executor::block_on(receiver).unwrap()
    }

    fn from_channel(channel: zx::Channel) -> Result<Self, Error> {
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
        let temp_vmo = zx::Vmo::create(TEMP_VMO_SIZE as u64)?;
        let (status, maybe_vmo_id) = block_device
            .attach_vmo(temp_vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, zx::Time::INFINITE)?;
        let temp_vmo_id = VmoId(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id);
        let device = Self {
            device: Mutex::new(block_device),
            block_size: info.block_size,
            block_count: info.block_count,
            fifo_state,
            temp_vmo: futures::lock::Mutex::new(temp_vmo),
            temp_vmo_id,
        };
        Ok(device)
    }

    /// Wraps AttachVmo from fuchsia.hardware.block::Block.
    pub fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error> {
        let mut device = self.device.lock().unwrap();
        let (status, maybe_vmo_id) = device
            .attach_vmo(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, zx::Time::INFINITE)?;
        Ok(VmoId(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id))
    }

    /// Detaches the given vmo-id from the device.
    pub async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        self.fifo_state
            .send(BlockFifoRequest {
                op_code: BLOCKIO_CLOSE_VMO,
                vmoid: vmo_id.into_id(),
                ..Default::default()
            })
            .await
    }

    fn to_blocks(&self, bytes: u64) -> Result<u64, Error> {
        ensure!(bytes % self.block_size as u64 == 0, "bad alignment");
        Ok(bytes / self.block_size as u64)
    }

    /// Reads from the device at |device_offset| into the given buffer slice.
    pub async fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        match buffer_slice {
            MutableBufferSlice::VmoId { vmo_id, offset, length } => {
                self.fifo_state
                    .send(BlockFifoRequest {
                        op_code: BLOCKIO_READ,
                        vmoid: vmo_id.id(),
                        block_count: self.to_blocks(length)?.try_into()?,
                        vmo_block: self.to_blocks(offset)?,
                        device_block: self.to_blocks(device_offset)?,
                        ..Default::default()
                    })
                    .await?
            }
            MutableBufferSlice::Memory(mut slice) => {
                let temp_vmo = self.temp_vmo.lock().await;
                let mut device_block = self.to_blocks(device_offset)?;
                loop {
                    let to_do = std::cmp::min(TEMP_VMO_SIZE, slice.len());
                    let block_count = self.to_blocks(to_do as u64)? as u32;
                    self.fifo_state
                        .send(BlockFifoRequest {
                            op_code: BLOCKIO_READ,
                            vmoid: self.temp_vmo_id.id(),
                            block_count: block_count,
                            vmo_block: 0,
                            device_block: device_block,
                            ..Default::default()
                        })
                        .await?;
                    temp_vmo.read(&mut slice[..to_do], 0)?;
                    if to_do == slice.len() {
                        break;
                    }
                    device_block += block_count as u64;
                    slice = &mut slice[to_do..];
                }
            }
        }
        Ok(())
    }

    /// Writes the data in |buffer_slice| to the device.
    pub async fn write_at(
        &self,
        buffer_slice: BufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        match buffer_slice {
            BufferSlice::VmoId { vmo_id, offset, length } => {
                self.fifo_state
                    .send(BlockFifoRequest {
                        op_code: BLOCKIO_WRITE,
                        vmoid: vmo_id.id(),
                        block_count: self.to_blocks(length)?.try_into()?,
                        vmo_block: self.to_blocks(offset)?,
                        device_block: self.to_blocks(device_offset)?,
                        ..Default::default()
                    })
                    .await?;
            }
            BufferSlice::Memory(mut slice) => {
                let temp_vmo = self.temp_vmo.lock().await;
                let mut device_block = self.to_blocks(device_offset)?;
                loop {
                    let to_do = std::cmp::min(TEMP_VMO_SIZE, slice.len());
                    let block_count = self.to_blocks(to_do as u64)? as u32;
                    temp_vmo.write(&slice[..to_do], 0)?;
                    self.fifo_state
                        .send(BlockFifoRequest {
                            op_code: BLOCKIO_WRITE,
                            vmoid: self.temp_vmo_id.id(),
                            block_count: block_count,
                            vmo_block: 0,
                            device_block: device_block,
                            ..Default::default()
                        })
                        .await?;
                    if to_do == slice.len() {
                        break;
                    }
                    device_block += block_count as u64;
                    slice = &slice[to_do..];
                }
            }
        }
        Ok(())
    }

    pub fn block_size(&self) -> u32 {
        self.block_size
    }

    pub fn block_count(&self) -> u64 {
        self.block_count
    }
}

impl Drop for RemoteBlockDevice {
    fn drop(&mut self) {
        // It's OK to leak the VMO id because the server will dump all VMOs when the fifo is torn
        // down.
        self.temp_vmo_id.take().into_id();
        self.fifo_state.requests.lock().unwrap().terminate();
        self.fifo_state.poller_waker.wake();
    }
}

// FifoPoller is a future responsible for sending and receiving from the fifo.
struct FifoPoller {
    fifo_state: Arc<FifoState>,
}

impl Future for FifoPoller {
    type Output = ();

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut requests = self.fifo_state.requests.lock().unwrap();
        if requests.terminate {
            return Poll::Ready(());
        }

        // Send requests.
        loop {
            let slice = requests.queue.as_slices().0;
            if slice.is_empty() {
                break;
            }
            match self.fifo_state.fifo.write(context, slice) {
                Poll::Ready(Ok(sent)) => {
                    requests.queue.drain(0..sent);
                }
                Poll::Ready(Err(_)) => {
                    requests.terminate();
                    return Poll::Ready(());
                }
                Poll::Pending => {
                    break;
                }
            }
        }

        // Receive responses.
        while let Poll::Ready(result) = self.fifo_state.fifo.read(context) {
            match result {
                Ok(Some(response)) => {
                    let request_id = response.request_id;
                    // If the request isn't in the map, assume that it's a cancelled read.
                    if let Some(request_state) = requests.map.get_mut(&request_id) {
                        request_state.result.replace(zx::Status::from_raw(response.status));
                        if let Some(waker) = request_state.waker.take() {
                            waker.wake();
                        }
                    }
                }
                _ => {
                    requests.terminate();
                    return Poll::Ready(());
                }
            }
        }

        self.fifo_state.poller_waker.register(context.waker());
        Poll::Pending
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{BufferSlice, MutableBufferSlice, RemoteBlockDevice},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{
            future::TryFutureExt,
            join,
            stream::{futures_unordered::FuturesUnordered, StreamExt},
        },
        ramdevice_client::RamdiskClient,
    };

    const RAMDISK_BLOCK_SIZE: u64 = 1024;
    const RAMDISK_BLOCK_COUNT: u64 = 1024;

    pub fn make_ramdisk() -> (RamdiskClient, RemoteBlockDevice) {
        isolated_driver_manager::launch_isolated_driver_manager()
            .expect("launch_isolated_driver_manager failed");
        ramdevice_client::wait_for_device("/dev/misc/ramctl", std::time::Duration::from_secs(10))
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(RAMDISK_BLOCK_SIZE, RAMDISK_BLOCK_COUNT)
            .expect("RamdiskClient::create failed");
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
            .write_at(BufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0)
            .await
            .expect("write_at failed");
        remote_block_device
            .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 1024, 2048), 0)
            .await
            .expect("read_at failed");
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
            .write_at(BufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 1)
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
                    .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0)
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
            reads.push(
                remote_block_device
                    .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0),
            );
        }
        let _ = futures::join!(futures::future::join_all(reads), async {
            ramdisk.destroy().expect("ramdisk.destroy failed")
        });
        remote_block_device.detach_vmo(vmo_id).await.expect_err("ramdisk should be destroyed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cancelled_reads() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).expect("attach_vmo failed");
        {
            let mut reads = FuturesUnordered::new();
            for _ in 0..1024 {
                reads.push(
                    remote_block_device
                        .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0),
                );
            }
            // Read the first 500 results and then dump the rest.
            for _ in 0..500 {
                reads.next().await;
            }
        }
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parallel_large_read_and_write_with_memory_succeds() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let remote_block_device_ref = &remote_block_device;
        let test_one = |offset, len, fill| async move {
            let buf = vec![fill; len];
            remote_block_device_ref
                .write_at(buf[..].into(), offset)
                .await
                .expect("write_at failed");
            // Read back an extra block either side.
            let mut read_buf = vec![0u8; len + 2 * RAMDISK_BLOCK_SIZE as usize];
            remote_block_device_ref
                .read_at(read_buf.as_mut_slice().into(), offset - RAMDISK_BLOCK_SIZE)
                .await
                .expect("read_at failed");
            assert_eq!(
                &read_buf[0..RAMDISK_BLOCK_SIZE as usize],
                &[0; RAMDISK_BLOCK_SIZE as usize][..]
            );
            assert_eq!(
                &read_buf[RAMDISK_BLOCK_SIZE as usize..RAMDISK_BLOCK_SIZE as usize + len],
                &buf[..]
            );
            assert_eq!(
                &read_buf[RAMDISK_BLOCK_SIZE as usize + len..],
                &[0; RAMDISK_BLOCK_SIZE as usize][..]
            );
        };
        const WRITE_LEN: usize = super::TEMP_VMO_SIZE * 3 + RAMDISK_BLOCK_SIZE as usize;
        join!(
            test_one(RAMDISK_BLOCK_SIZE, WRITE_LEN, 0xa3u8),
            test_one(2 * RAMDISK_BLOCK_SIZE + WRITE_LEN as u64, WRITE_LEN, 0x7fu8)
        );
    }
}
