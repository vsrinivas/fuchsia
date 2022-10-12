// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! IMPORTANT !!!
// Most of the definitions in this file need to be kept in sync with:
// - //sdk/banjo/fuchsia.hardware.block/block.fidl;
// - //zircon/public/system/public/zircon/device/block.h.

use {
    anyhow::{ensure, Error},
    async_trait::async_trait,
    fidl_fuchsia_hardware_block as block,
    fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
    fuchsia_trace as trace,
    fuchsia_zircon::sys::zx_handle_t,
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{channel::oneshot, executor::block_on},
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        future::Future,
        hash::{Hash, Hasher},
        ops::DerefMut,
        pin::Pin,
        sync::{
            atomic::{AtomicU16, Ordering},
            Arc, Mutex,
        },
        task::{Context, Poll, Waker},
    },
};

pub use cache::Cache;

pub mod cache;

pub mod testing;

const BLOCK_VMOID_INVALID: u16 = 0;
const TEMP_VMO_SIZE: usize = 65536;

// See fuchsia.hardware.block/block.fidl
pub const BLOCKIO_READ: u32 = 1;
pub const BLOCKIO_WRITE: u32 = 2;
pub const BLOCKIO_FLUSH: u32 = 3;
pub const BLOCKIO_TRIM: u32 = 4;
pub const BLOCKIO_CLOSE_VMO: u32 = 5;

pub const BLOCK_GROUP_ITEM: u32 = 0x00000400;
pub const BLOCK_GROUP_LAST: u32 = 0x00000800;

pub const BLOCK_OP_MASK: u32 = 0x000000ff;

fn op_code_str(op_code: u32) -> &'static str {
    match op_code & BLOCK_OP_MASK {
        BLOCKIO_READ => "read",
        BLOCKIO_WRITE => "write",
        BLOCKIO_FLUSH => "flush",
        BLOCKIO_TRIM => "trim",
        BLOCKIO_CLOSE_VMO => "close_vmo",
        _ => "unknown",
    }
}

#[derive(Default, zerocopy::AsBytes, zerocopy::FromBytes)]
#[repr(C)]
pub struct BlockFifoRequest {
    pub op_code: u32,
    pub request_id: u32,
    pub group_id: u16,
    pub vmoid: u16,
    pub block_count: u32,
    pub vmo_block: u64,
    pub device_block: u64,
    pub trace_flow_id: u64,
}

#[derive(Default, zerocopy::AsBytes, zerocopy::FromBytes)]
#[repr(C)]
pub struct BlockFifoResponse {
    pub status: i32,
    pub request_id: u32,
    pub group_id: u16,
    pub reserved1: u16,
    pub count: u32,
    pub reserved2: u64,
    pub reserved3: u64,
    pub reserved4: u64,
}

// Generates a trace ID that will be unique across the system (as long as |request_id| isn't
// reused within this process).
fn generate_trace_flow_id(request_id: u32) -> u64 {
    lazy_static! {
        static ref SELF_HANDLE: zx_handle_t = fuchsia_runtime::process_self().raw_handle();
    };
    *SELF_HANDLE as u64 + (request_id as u64) << 32
}

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
struct FifoState {
    // The fifo.
    fifo: Option<fasync::Fifo<BlockFifoResponse, BlockFifoRequest>>,

    // The next request ID to be used.
    next_request_id: u32,

    // A queue of messages to be sent on the fifo.
    queue: std::collections::VecDeque<BlockFifoRequest>,

    // Map from request ID to RequestState.
    map: HashMap<u32, RequestState>,

    // The waker for the FifoPoller.
    poller_waker: Option<Waker>,
}

impl FifoState {
    fn terminate(&mut self) {
        self.fifo.take();
        for (_, request_state) in self.map.iter_mut() {
            request_state.result.get_or_insert(zx::Status::CANCELED);
            if let Some(waker) = request_state.waker.take() {
                waker.wake();
            }
        }
        if let Some(waker) = self.poller_waker.take() {
            waker.wake();
        }
    }

    // Returns true if polling should be terminated.
    fn poll_send_requests(&mut self, context: &mut Context<'_>) -> bool {
        let fifo = if let Some(fifo) = self.fifo.as_ref() {
            fifo
        } else {
            return true;
        };

        loop {
            let slice = self.queue.as_slices().0;
            if slice.is_empty() {
                return false;
            }
            match fifo.write(context, slice) {
                Poll::Ready(Ok(sent)) => {
                    self.queue.drain(0..sent);
                }
                Poll::Ready(Err(_)) => {
                    self.terminate();
                    return true;
                }
                Poll::Pending => {
                    return false;
                }
            }
        }
    }
}

type FifoStateRef = Arc<Mutex<FifoState>>;

// A future used for fifo responses.
struct ResponseFuture {
    request_id: u32,
    fifo_state: FifoStateRef,
}

impl ResponseFuture {
    fn new(fifo_state: FifoStateRef, request_id: u32) -> Self {
        ResponseFuture { request_id, fifo_state }
    }
}

impl Future for ResponseFuture {
    type Output = Result<(), zx::Status>;

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut state = self.fifo_state.lock().unwrap();
        let request_state = state.map.get_mut(&self.request_id).unwrap();
        if let Some(result) = request_state.result {
            Poll::Ready(result.into())
        } else {
            request_state.waker.replace(context.waker().clone());
            Poll::Pending
        }
    }
}

impl Drop for ResponseFuture {
    fn drop(&mut self) {
        self.fifo_state.lock().unwrap().map.remove(&self.request_id).unwrap();
    }
}

/// Wraps a vmo-id. Will panic if you forget to detach.
#[derive(Debug)]
pub struct VmoId(AtomicU16);

impl VmoId {
    /// VmoIds will normally be vended by attach_vmo, but this might be used in some tests
    pub fn new(id: u16) -> Self {
        Self(AtomicU16::new(id))
    }

    /// Invalidates self and returns a new VmoId with the same underlying ID.
    pub fn take(&self) -> Self {
        Self(AtomicU16::new(self.0.swap(BLOCK_VMOID_INVALID, Ordering::Relaxed)))
    }

    pub fn is_valid(&self) -> bool {
        self.id() != BLOCK_VMOID_INVALID
    }

    /// Takes the ID.  The caller assumes responsibility for detaching.
    pub fn into_id(self) -> u16 {
        self.0.swap(BLOCK_VMOID_INVALID, Ordering::Relaxed)
    }

    pub fn id(&self) -> u16 {
        self.0.load(Ordering::Relaxed)
    }
}

impl PartialEq for VmoId {
    fn eq(&self, other: &Self) -> bool {
        self.id() == other.id()
    }
}

impl Eq for VmoId {}

impl Drop for VmoId {
    fn drop(&mut self) {
        assert_eq!(
            self.0.load(Ordering::Relaxed),
            BLOCK_VMOID_INVALID,
            "Did you forget to detach?"
        );
    }
}

impl Hash for VmoId {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.id().hash(state);
    }
}

/// Represents a client connection to a block device. This is a simplified version of the block.fidl
/// interface.
/// Most users will use the RemoteBlockClient instantiation of this trait.
#[async_trait]
pub trait BlockClient: Send + Sync {
    /// Wraps AttachVmo from fuchsia.hardware.block::Block.
    async fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error>;

    /// Detaches the given vmo-id from the device.
    async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error>;

    /// Reads from the device at |device_offset| into the given buffer slice.
    async fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error>;

    /// Writes the data in |buffer_slice| to the device.
    async fn write_at(
        &self,
        buffer_slice: BufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error>;

    /// Sends a flush request to the underlying block device.
    async fn flush(&self) -> Result<(), Error>;

    /// Closes the fifo.
    async fn close(&self) -> Result<(), Error>;

    /// Returns the block size of the device.
    fn block_size(&self) -> u32;

    /// Returns the size, in blocks, of the device.
    fn block_count(&self) -> u64;

    /// Returns true if the remote fifo is still connected.
    fn is_connected(&self) -> bool;
}

struct Common {
    block_size: u32,
    block_count: u64,
    fifo_state: FifoStateRef,
    temp_vmo: futures::lock::Mutex<zx::Vmo>,
    temp_vmo_id: VmoId,
}

impl Common {
    fn to_blocks(&self, bytes: u64) -> Result<u64, Error> {
        ensure!(bytes % self.block_size as u64 == 0, "bad alignment");
        Ok(bytes / self.block_size as u64)
    }

    // Sends the request and waits for the response.
    async fn send(&self, mut request: BlockFifoRequest) -> Result<(), Error> {
        let _guard = trace::async_enter!(
            trace::Id::new(),
            "storage",
            "BlockOp",
            "op" => op_code_str(request.op_code)
        );
        let (request_id, trace_flow_id) = {
            let mut state = self.fifo_state.lock().unwrap();
            if state.fifo.is_none() {
                // Fifo has been closed.
                return Err(zx::Status::CANCELED.into());
            }
            trace::duration!("storage", "BlockOp::start");
            let request_id = state.next_request_id;
            let trace_flow_id = generate_trace_flow_id(request_id);
            state.next_request_id = state.next_request_id.overflowing_add(1).0;
            assert!(
                state.map.insert(request_id, RequestState::default()).is_none(),
                "request id in use!"
            );
            request.request_id = request_id;
            request.trace_flow_id = generate_trace_flow_id(request_id);
            trace::flow_begin!("storage", "BlockOp", trace_flow_id.into());
            state.queue.push_back(request);
            if let Some(waker) = state.poller_waker.clone() {
                state.poll_send_requests(&mut Context::from_waker(&waker));
            }
            (request_id, trace_flow_id)
        };
        ResponseFuture::new(self.fifo_state.clone(), request_id).await?;
        trace::duration!("storage", "BlockOp::end");
        trace::flow_end!("storage", "BlockOp", trace_flow_id.into());
        Ok(())
    }
}

impl Common {
    fn new(
        fifo: fasync::Fifo<BlockFifoResponse, BlockFifoRequest>,
        info: &block::BlockInfo,
        temp_vmo: zx::Vmo,
        temp_vmo_id: VmoId,
    ) -> Self {
        let fifo_state = Arc::new(Mutex::new(FifoState { fifo: Some(fifo), ..Default::default() }));
        fasync::Task::spawn(FifoPoller { fifo_state: fifo_state.clone() }).detach();
        Self {
            block_size: info.block_size,
            block_count: info.block_count,
            fifo_state,
            temp_vmo: futures::lock::Mutex::new(temp_vmo),
            temp_vmo_id,
        }
    }

    async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        self.send(BlockFifoRequest {
            op_code: BLOCKIO_CLOSE_VMO,
            vmoid: vmo_id.into_id(),
            ..Default::default()
        })
        .await
    }

    async fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        match buffer_slice {
            MutableBufferSlice::VmoId { vmo_id, offset, length } => {
                self.send(BlockFifoRequest {
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
                    self.send(BlockFifoRequest {
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

    async fn write_at(
        &self,
        buffer_slice: BufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        match buffer_slice {
            BufferSlice::VmoId { vmo_id, offset, length } => {
                self.send(BlockFifoRequest {
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
                    self.send(BlockFifoRequest {
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

    async fn flush(&self) -> Result<(), Error> {
        self.send(BlockFifoRequest {
            op_code: BLOCKIO_FLUSH,
            vmoid: BLOCK_VMOID_INVALID,
            ..Default::default()
        })
        .await
    }

    fn block_size(&self) -> u32 {
        self.block_size
    }

    fn block_count(&self) -> u64 {
        self.block_count
    }

    fn is_connected(&self) -> bool {
        self.fifo_state.lock().unwrap().fifo.is_some()
    }
}

impl Drop for Common {
    fn drop(&mut self) {
        // It's OK to leak the VMO id because the server will dump all VMOs when the fifo is torn
        // down.
        self.temp_vmo_id.take().into_id();
        self.fifo_state.lock().unwrap().terminate();
    }
}

/// RemoteBlockClient is a BlockClient that communicates with a real block device over FIDL.
pub struct RemoteBlockClient {
    remote: block::BlockProxy,
    common: Common,
}

impl RemoteBlockClient {
    /// Returns a connection to a remote block device via the given channel.
    pub async fn new(channel: zx::Channel) -> Result<Self, Error> {
        let remote = block::BlockProxy::new(fasync::Channel::from_channel(channel)?);
        let (status, maybe_info) = remote.get_info().await?;
        let info = maybe_info.ok_or(zx::Status::from_raw(status))?;
        let (status, maybe_fifo) = remote.get_fifo().await?;
        let fifo = fasync::Fifo::from_fifo(maybe_fifo.ok_or(zx::Status::from_raw(status))?)?;
        let temp_vmo = zx::Vmo::create(TEMP_VMO_SIZE as u64)?;
        let (status, maybe_vmo_id) =
            remote.attach_vmo(temp_vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?).await?;
        let vmo_id = VmoId::new(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id);
        Ok(RemoteBlockClient { remote, common: Common::new(fifo, &info, temp_vmo, vmo_id) })
    }
}

#[async_trait]
impl BlockClient for RemoteBlockClient {
    async fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error> {
        let (status, maybe_vmo_id) =
            self.remote.attach_vmo(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?).await?;
        Ok(VmoId::new(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id))
    }

    async fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        self.common.detach_vmo(vmo_id).await
    }

    async fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        self.common.read_at(buffer_slice, device_offset).await
    }

    async fn write_at(
        &self,
        buffer_slice: BufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        self.common.write_at(buffer_slice, device_offset).await
    }

    async fn flush(&self) -> Result<(), Error> {
        self.common.flush().await
    }

    async fn close(&self) -> Result<(), Error> {
        // It's OK to leak the VMO id because the server will dump all VMOs when the fifo is torn
        // down.
        self.common.temp_vmo_id.take().into_id();
        zx::Status::ok(self.remote.close_fifo().await?)?;
        Ok(())
    }

    fn block_size(&self) -> u32 {
        self.common.block_size()
    }

    fn block_count(&self) -> u64 {
        self.common.block_count()
    }

    fn is_connected(&self) -> bool {
        self.common.is_connected()
    }
}

pub struct RemoteBlockClientSync {
    remote: block::BlockSynchronousProxy,
    common: Common,
}

impl RemoteBlockClientSync {
    /// Returns a connection to a remote block device via the given channel, but spawns a separate
    /// thread for polling the fifo which makes it work in cases where no executor is configured for
    /// the calling thread.
    pub fn new(channel: zx::Channel) -> Result<Self, Error> {
        let remote = block::BlockSynchronousProxy::new(channel);
        let (status, maybe_info) = remote.get_info(zx::Time::INFINITE)?;
        let info = maybe_info.ok_or(zx::Status::from_raw(status))?;
        let (status, maybe_fifo) = remote.get_fifo(zx::Time::INFINITE)?;
        let fifo = maybe_fifo.ok_or(zx::Status::from_raw(status))?;
        let temp_vmo = zx::Vmo::create(TEMP_VMO_SIZE as u64)?;
        let (status, maybe_vmo_id) = remote
            .attach_vmo(temp_vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, zx::Time::INFINITE)?;
        let vmo_id = VmoId::new(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id);

        // The fifo needs to be instantiated from the thread that has the executor as that's where
        // the fifo registers for notifications to be delivered.
        let (sender, receiver) = oneshot::channel::<Result<Self, Error>>();
        std::thread::spawn(move || {
            let mut executor = fasync::LocalExecutor::new().expect("failed to create executor");
            match fasync::Fifo::from_fifo(fifo) {
                Ok(fifo) => {
                    let common = Common::new(fifo, &info, temp_vmo, vmo_id);
                    let fifo_state = common.fifo_state.clone();
                    let _ = sender.send(Ok(RemoteBlockClientSync { remote, common }));
                    executor.run_singlethreaded(FifoPoller { fifo_state });
                }
                Err(e) => {
                    let _ = sender.send(Err(e.into()));
                }
            }
        });
        block_on(receiver)?
    }

    pub fn attach_vmo(&self, vmo: &zx::Vmo) -> Result<VmoId, Error> {
        let (status, maybe_vmo_id) = self
            .remote
            .attach_vmo(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?, zx::Time::INFINITE)?;
        Ok(VmoId::new(maybe_vmo_id.ok_or(zx::Status::from_raw(status))?.id))
    }

    pub fn detach_vmo(&self, vmo_id: VmoId) -> Result<(), Error> {
        block_on(self.common.detach_vmo(vmo_id))
    }

    pub fn read_at(
        &self,
        buffer_slice: MutableBufferSlice<'_>,
        device_offset: u64,
    ) -> Result<(), Error> {
        block_on(self.common.read_at(buffer_slice, device_offset))
    }

    pub fn write_at(&self, buffer_slice: BufferSlice<'_>, device_offset: u64) -> Result<(), Error> {
        block_on(self.common.write_at(buffer_slice, device_offset))
    }

    pub fn flush(&self) -> Result<(), Error> {
        block_on(self.common.flush())
    }

    pub fn close(&self) -> Result<(), Error> {
        // It's OK to leak the VMO id because the server will dump all VMOs when the fifo is torn
        // down.
        self.common.temp_vmo_id.take().into_id();
        zx::Status::ok(self.remote.close_fifo(zx::Time::INFINITE)?)?;
        Ok(())
    }

    pub fn block_size(&self) -> u32 {
        self.common.block_size()
    }

    pub fn block_count(&self) -> u64 {
        self.common.block_count()
    }

    pub fn is_connected(&self) -> bool {
        self.common.is_connected()
    }
}

impl Drop for RemoteBlockClientSync {
    fn drop(&mut self) {
        // Ignore errors here as there is not much we can do about it.
        let _ = self.close();
    }
}

// FifoPoller is a future responsible for sending and receiving from the fifo.
struct FifoPoller {
    fifo_state: FifoStateRef,
}

impl Future for FifoPoller {
    type Output = ();

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut state_lock = self.fifo_state.lock().unwrap();
        let state = state_lock.deref_mut(); // So that we can split the borrow.

        // Send requests.
        if state.poll_send_requests(context) {
            return Poll::Ready(());
        }

        // Receive responses.
        let fifo = state.fifo.as_ref().unwrap(); // Safe because poll_send_requests checks.
        while let Poll::Ready(result) = fifo.read_one(context) {
            match result {
                Ok(response) => {
                    let request_id = response.request_id;
                    // If the request isn't in the map, assume that it's a cancelled read.
                    if let Some(request_state) = state.map.get_mut(&request_id) {
                        request_state.result.replace(zx::Status::from_raw(response.status));
                        if let Some(waker) = request_state.waker.take() {
                            waker.wake();
                        }
                    }
                }
                Err(_) => {
                    state.terminate();
                    return Poll::Ready(());
                }
            }
        }

        state.poller_waker = Some(context.waker().clone());
        Poll::Pending
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            BlockClient, BlockFifoRequest, BlockFifoResponse, BufferSlice, MutableBufferSlice,
            RemoteBlockClient, RemoteBlockClientSync,
        },
        fidl_fuchsia_hardware_block::{self as block, BlockRequest},
        fuchsia_async::{self as fasync, FifoReadable, FifoWritable},
        fuchsia_zircon as zx,
        futures::{
            future::{AbortHandle, Abortable, TryFutureExt},
            join,
            stream::{futures_unordered::FuturesUnordered, StreamExt},
        },
        ramdevice_client::RamdiskClient,
    };

    const RAMDISK_BLOCK_SIZE: u64 = 1024;
    const RAMDISK_BLOCK_COUNT: u64 = 1024;

    pub async fn make_ramdisk() -> (RamdiskClient, RemoteBlockClient) {
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(10),
        )
        .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(RAMDISK_BLOCK_SIZE, RAMDISK_BLOCK_COUNT)
            .expect("RamdiskClient::create failed");
        let remote_block_device =
            RemoteBlockClient::new(ramdisk.open().expect("ramdisk.open failed"))
                .await
                .expect("new failed");
        assert_eq!(remote_block_device.block_size(), 1024);
        (ramdisk, remote_block_device)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_against_ram_disk() {
        let (_ramdisk, remote_block_device) = make_ramdisk().await;

        let stats_before =
            remote_block_device.remote.get_stats(false).await.expect("get_stats failed");
        assert_eq!(stats_before.0, zx::Status::OK.into_raw());
        let stats_before = stats_before.1.expect("Processing get_stats result failed");

        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        vmo.write(b"hello", 5).expect("vmo.write failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
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

        // check that the stats are what we expect them to be
        let stats_after =
            remote_block_device.remote.get_stats(false).await.expect("get_stats failed");
        assert_eq!(stats_after.0, zx::Status::OK.into_raw());
        let stats_after = stats_after.1.expect("Processing get_stats result failed");
        // write stats
        assert_eq!(
            stats_before.write.success.total_calls + 1,
            stats_after.write.success.total_calls
        );
        assert_eq!(
            stats_before.write.success.bytes_transferred + 1024,
            stats_after.write.success.bytes_transferred
        );
        assert_eq!(stats_before.write.failure.total_calls, stats_after.write.failure.total_calls);
        // read stats
        assert_eq!(stats_before.read.success.total_calls + 1, stats_after.read.success.total_calls);
        assert_eq!(
            stats_before.read.success.bytes_transferred + 2048,
            stats_after.read.success.bytes_transferred
        );
        assert_eq!(stats_before.read.failure.total_calls, stats_after.read.failure.total_calls);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_against_ram_disk_with_flush() {
        let (_ramdisk, remote_block_device) = make_ramdisk().await;

        let stats_before =
            remote_block_device.remote.get_stats(false).await.expect("get_stats failed");
        assert_eq!(stats_before.0, zx::Status::OK.into_raw());
        let stats_before = stats_before.1.expect("Processing get_stats result failed");

        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        vmo.write(b"hello", 5).expect("vmo.write failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
        remote_block_device
            .write_at(BufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0)
            .await
            .expect("write_at failed");
        remote_block_device.flush().await.expect("flush failed");
        remote_block_device
            .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 1024, 2048), 0)
            .await
            .expect("read_at failed");
        let mut buf: [u8; 5] = Default::default();
        vmo.read(&mut buf, 1029).expect("vmo.read failed");
        assert_eq!(&buf, b"hello");
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");

        // check that the stats are what we expect them to be
        let stats_after =
            remote_block_device.remote.get_stats(false).await.expect("get_stats failed");
        assert_eq!(stats_after.0, zx::Status::OK.into_raw());
        let stats_after = stats_after.1.expect("Processing get_stats result failed");
        // write stats
        assert_eq!(
            stats_before.write.success.total_calls + 1,
            stats_after.write.success.total_calls
        );
        assert_eq!(
            stats_before.write.success.bytes_transferred + 1024,
            stats_after.write.success.bytes_transferred
        );
        assert_eq!(stats_before.write.failure.total_calls, stats_after.write.failure.total_calls);
        // flush stats
        assert_eq!(
            stats_before.flush.success.total_calls + 1,
            stats_after.flush.success.total_calls
        );
        assert_eq!(stats_before.flush.failure.total_calls, stats_after.flush.failure.total_calls);
        // read stats
        assert_eq!(stats_before.read.success.total_calls + 1, stats_after.read.success.total_calls);
        assert_eq!(
            stats_before.read.success.bytes_transferred + 2048,
            stats_after.read.success.bytes_transferred
        );
        assert_eq!(stats_before.read.failure.total_calls, stats_after.read.failure.total_calls);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_alignment() {
        let (_ramdisk, remote_block_device) = make_ramdisk().await;
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
        remote_block_device
            .write_at(BufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 1)
            .await
            .expect_err("expected failure due to bad alignment");
        remote_block_device.detach_vmo(vmo_id).await.expect("detach_vmo failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parallel_io() {
        let (_ramdisk, remote_block_device) = make_ramdisk().await;
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
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
        let (ramdisk, remote_block_device) = make_ramdisk().await;
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
        let mut reads = Vec::new();
        for _ in 0..1024 {
            reads.push(
                remote_block_device
                    .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0),
            );
        }
        assert!(remote_block_device.is_connected());
        let _ = futures::join!(futures::future::join_all(reads), async {
            ramdisk.destroy().expect("ramdisk.destroy failed")
        });
        // Destroying the ramdisk is asynchronous. Keep issuing reads until they start failing.
        while remote_block_device
            .read_at(MutableBufferSlice::new_with_vmo_id(&vmo_id, 0, 1024), 0)
            .await
            .is_ok()
        {}

        // Sometimes the FIFO will start rejecting requests before FIFO is actually closed, so we
        // get false-positives from is_connected.
        while remote_block_device.is_connected() {
            // Sleep for a bit to minimise lock contention.
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(500))).await;
        }

        // But once is_connected goes negative, it should stay negative.
        assert_eq!(remote_block_device.is_connected(), false);
        let _ = remote_block_device.detach_vmo(vmo_id).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_cancelled_reads() {
        let (_ramdisk, remote_block_device) = make_ramdisk().await;
        let vmo = zx::Vmo::create(131072).expect("Vmo::create failed");
        let vmo_id = remote_block_device.attach_vmo(&vmo).await.expect("attach_vmo failed");
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
        let (_ramdisk, remote_block_device) = make_ramdisk().await;
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

    // Implements dummy server which can be used by test cases to verify whether
    // channel messages and fifo operations are being received - by using set_channel_handler or
    // set_fifo_hander respectively
    struct FakeBlockServer<'a> {
        server_channel: Option<zx::Channel>,
        channel_handler: Box<dyn Fn(&BlockRequest) -> bool + 'a>,
        fifo_handler: Box<dyn Fn(BlockFifoRequest) -> BlockFifoResponse + 'a>,
    }

    impl<'a> FakeBlockServer<'a> {
        // Creates a new FakeBlockServer given a channel to listen on.
        //
        // 'channel_handler' and 'fifo_handler' closures allow for customizing the way how the server
        // handles requests received from channel or the fifo respectfully.
        //
        // 'channel_handler' receives a message before it is handled by the default implementation
        // and can return 'true' to indicate all processing is done and no further processing of
        // that message is required
        //
        // 'fifo_handler' takes as input a BlockFifoRequest and produces a response which the
        // FakeBlockServer will send over the fifo.
        fn new(
            server_channel: zx::Channel,
            channel_handler: impl Fn(&BlockRequest) -> bool + 'a,
            fifo_handler: impl Fn(BlockFifoRequest) -> BlockFifoResponse + 'a,
        ) -> FakeBlockServer<'a> {
            FakeBlockServer {
                server_channel: Some(server_channel),
                channel_handler: Box::new(channel_handler),
                fifo_handler: Box::new(fifo_handler),
            }
        }

        // Runs the server
        async fn run(&mut self) {
            let server = fidl::endpoints::ServerEnd::<block::BlockMarker>::new(
                self.server_channel.take().unwrap(),
            );

            // Set up a mock server.
            let (server_fifo, client_fifo) =
                zx::Fifo::create(16, std::mem::size_of::<BlockFifoRequest>())
                    .expect("Fifo::create failed");
            let maybe_server_fifo = std::sync::Mutex::new(Some(client_fifo));

            let (fifo_future_abort, fifo_future_abort_registration) = AbortHandle::new_pair();
            let fifo_future = Abortable::new(
                async {
                    let fifo = fasync::Fifo::from_fifo(server_fifo).expect("from_fifo failed");
                    loop {
                        let request = match fifo.read_entry().await {
                            Ok(r) => r,
                            Err(zx::Status::PEER_CLOSED) => break,
                            Err(e) => panic!("read_entry failed {:?}", e),
                        };

                        let response = self.fifo_handler.as_ref()(request);
                        fifo.write_entries(std::slice::from_ref(&response))
                            .await
                            .expect("write_entries failed");
                    }
                },
                fifo_future_abort_registration,
            );

            let channel_future = async {
                server
                    .into_stream()
                    .expect("into_stream failed")
                    .for_each(|request| async {
                        let request = request.expect("unexpected fidl error");

                        // Give a chance for the test to register and potentially handle the event
                        if self.channel_handler.as_ref()(&request) {
                            return;
                        }

                        match request {
                            BlockRequest::GetInfo { responder } => {
                                let mut block_info = block::BlockInfo {
                                    block_count: 1024,
                                    block_size: 512,
                                    max_transfer_size: 1024 * 1024,
                                    flags: 0,
                                    reserved: 0,
                                };
                                responder
                                    .send(zx::sys::ZX_OK, Some(&mut block_info))
                                    .expect("send failed");
                            }
                            BlockRequest::GetFifo { responder } => {
                                responder
                                    .send(zx::sys::ZX_OK, maybe_server_fifo.lock().unwrap().take())
                                    .expect("send failed");
                            }
                            BlockRequest::AttachVmo { vmo: _, responder } => {
                                let mut vmo_id = block::VmoId { id: 1 };
                                responder
                                    .send(zx::sys::ZX_OK, Some(&mut vmo_id))
                                    .expect("send failed");
                            }
                            BlockRequest::CloseFifo { responder } => {
                                fifo_future_abort.abort();
                                responder.send(zx::sys::ZX_OK).expect("send failed");
                            }
                            _ => panic!("Unexpected message"),
                        }
                    })
                    .await;
            };

            let _result = join!(fifo_future, channel_future);
            //_result can be Err(Aborted) since FifoClose calls .abort but that's expected
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_fifo_close_is_called() {
        let close_called = std::sync::Mutex::new(false);
        let (client, server) = zx::Channel::create().expect("Channel::create failed");

        std::thread::spawn(move || {
            let _remote_block_device =
                RemoteBlockClientSync::new(client).expect("RemoteBlockClientSync::new failed");
            // The drop here should cause CloseFifo to be sent.
        });

        let channel_handler = |request: &BlockRequest| -> bool {
            if let BlockRequest::CloseFifo { .. } = request {
                *close_called.lock().unwrap() = true;
            }
            false
        };
        FakeBlockServer::new(server, channel_handler, |_| unreachable!()).run().await;

        // After the server has finished running, we can check to see that close was called.
        assert!(*close_called.lock().unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_block_flush_is_called() {
        let (client, server) = zx::Channel::create().expect("Channel::create failed");

        futures::join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client).await.expect("new failed");

                remote_block_device.flush().await.expect("flush failed");
            },
            async {
                let flush_called = std::sync::Mutex::new(false);
                let fifo_handler = |request: BlockFifoRequest| -> BlockFifoResponse {
                    *flush_called.lock().unwrap() = true;
                    assert_eq!(request.op_code, super::BLOCKIO_FLUSH);
                    BlockFifoResponse {
                        status: zx::Status::OK.into_raw(),
                        request_id: request.request_id,
                        ..Default::default()
                    }
                };
                FakeBlockServer::new(server, |_| false, fifo_handler).run().await;
                // After the server has finished running, we can check to see that close was called.
                assert!(*flush_called.lock().unwrap());
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_trace_flow_ids_set() {
        let (client, server) = zx::Channel::create().expect("Channel::create failed");

        futures::join!(
            async {
                let remote_block_device = RemoteBlockClient::new(client).await.expect("new failed");
                remote_block_device.flush().await.expect("flush failed");
            },
            async {
                let flow_id: std::sync::Mutex<Option<u64>> = std::sync::Mutex::new(None);
                let fifo_handler = |request: BlockFifoRequest| -> BlockFifoResponse {
                    if request.trace_flow_id > 0 {
                        *flow_id.lock().unwrap() = Some(request.trace_flow_id);
                    }
                    BlockFifoResponse {
                        status: zx::Status::OK.into_raw(),
                        request_id: request.request_id,
                        ..Default::default()
                    }
                };
                FakeBlockServer::new(server, |_| false, fifo_handler).run().await;
                // After the server has finished running, verify the trace flow ID was set to some value.
                assert!(flow_id.lock().unwrap().is_some());
            }
        );
    }
}
