// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice session.

mod buffer;

use std::fmt::Debug;
use std::num::{NonZeroU16, NonZeroU64};
use std::pin::Pin;
use std::sync::{Arc, Weak};
use std::{convert::TryFrom as _, task::Waker};

use fidl_fuchsia_hardware_network as netdev;
use fidl_table_validation::ValidFidlTable;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{
    future::{poll_fn, Future},
    ready,
    task::{Context, Poll},
};
use parking_lot::Mutex;

use crate::error::{Error, Result};
pub use buffer::Buffer;
use buffer::{
    pool::Pool, AllocKind, DescId, Rx, Tx, NETWORK_DEVICE_DESCRIPTOR_LENGTH,
    NETWORK_DEVICE_DESCRIPTOR_VERSION,
};

/// A session between network device client and driver.
pub struct Session {
    inner: Weak<Inner>,
}

impl Debug for Session {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.inner.upgrade() {
            Some(inner) => {
                let Inner { name, pool: _, proxy: _, rx: _, tx: _, tx_pending: _ } = &*inner;
                f.debug_struct("Session").field("name", &name).finish_non_exhaustive()
            }
            None => f.write_str("Session(closed)"),
        }
    }
}

impl Session {
    /// Creates a new session with the given `name` and `config`.
    pub async fn new(
        device: &netdev::DeviceProxy,
        name: &str,
        config: Config,
    ) -> Result<(Self, Task)> {
        let inner = Inner::new(device, name, config).await?;
        Ok((Session { inner: Arc::downgrade(&inner) }, Task { inner }))
    }

    /// Sends a [`Buffer`] to the network device in this session.
    pub async fn send(&self, buffer: Buffer<Tx>) -> Result<()> {
        self.inner()?.send(buffer).await
    }

    /// Receives a [`Buffer`] from the network device in this session.
    pub async fn recv(&self) -> Result<Buffer<Rx>> {
        self.inner()?.recv().await
    }

    /// Allocates a [`Buffer`] that may later be queued to the network device.
    ///
    /// The returned buffer will have at least `num_bytes` as size.
    pub async fn alloc_tx_buffer(&self, num_bytes: usize) -> Result<Buffer<Tx>> {
        self.inner()?.pool.alloc_tx_buffer(num_bytes).await
    }

    /// Attaches [`Session`] to a port.
    pub async fn attach<IntoIter, Iter>(&self, Port(port): Port, rx_frames: IntoIter) -> Result<()>
    where
        IntoIter: IntoIterator<IntoIter = Iter>,
        Iter: Iterator<Item = netdev::FrameType> + ExactSizeIterator,
    {
        let mut iter = rx_frames.into_iter();
        let () = self
            .inner()?
            .proxy
            .attach(port, &mut iter)
            .await?
            .map_err(|raw| Error::Attach(port, zx::Status::from_raw(raw)))?;
        Ok(())
    }

    /// Detaches a port from the [`Session`].
    pub async fn detach(&self, Port(port): Port) -> Result<()> {
        let () = self
            .inner()?
            .proxy
            .detach(port)
            .await?
            .map_err(|raw| Error::Detach(port, zx::Status::from_raw(raw)))?;
        Ok(())
    }

    /// Retrieves [`Inner`] if the task is still alive.
    fn inner(&self) -> Result<Arc<Inner>> {
        self.inner.upgrade().ok_or(Error::NoProgress)
    }
}

struct Inner {
    pool: Arc<Pool>,
    proxy: netdev::SessionProxy,
    name: String,
    rx: fasync::Fifo<DescId<Rx>>,
    tx: fasync::Fifo<DescId<Tx>>,
    // Pending tx descriptors to be sent.
    tx_pending: Pending<Tx>,
}

impl Inner {
    /// Creates a new session.
    async fn new(device: &netdev::DeviceProxy, name: &str, config: Config) -> Result<Arc<Self>> {
        let (pool, descriptors, data) = Pool::new(config)?;

        let session_info = {
            // The following two constants are not provided by user, panic
            // instead of returning an error.
            let descriptor_version = u8::try_from(NETWORK_DEVICE_DESCRIPTOR_VERSION)
                .expect("descriptor version not representable by u8");
            let descriptor_length =
                u8::try_from(NETWORK_DEVICE_DESCRIPTOR_LENGTH / std::mem::size_of::<u64>())
                    .expect("descriptor length in 64-bit words not representable by u8");
            netdev::SessionInfo {
                descriptors: Some(descriptors),
                data: Some(data),
                descriptor_version: Some(descriptor_version),
                descriptor_length: Some(descriptor_length),
                descriptor_count: Some(config.num_tx_buffers.get() + config.num_rx_buffers.get()),
                options: Some(config.options),
                ..netdev::SessionInfo::EMPTY
            }
        };

        let (client, netdev::Fifos { rx, tx }) = device
            .open_session(name, session_info)
            .await?
            .map_err(|raw| Error::Open(name.to_owned(), zx::Status::from_raw(raw)))?;
        let proxy = client.into_proxy()?;
        let rx =
            fasync::Fifo::from_fifo(rx).map_err(|status| Error::Fifo("create", "rx", status))?;
        let tx =
            fasync::Fifo::from_fifo(tx).map_err(|status| Error::Fifo("create", "tx", status))?;

        Ok(Arc::new(Self {
            pool,
            proxy,
            name: name.to_owned(),
            rx,
            tx,
            tx_pending: Pending::new(Vec::new()),
        }))
    }

    /// Polls to submit available rx descriptors from pool to driver.
    ///
    /// Returns the number of rx descriptors that are submitted.
    fn poll_submit_rx(&self, cx: &mut Context<'_>) -> Poll<Result<usize>> {
        self.pool.rx_pending.poll_submit(&self.rx, cx)
    }

    /// Polls completed rx descriptors from the driver.
    ///
    /// Returns the the head of a completed rx descriptor chain.
    fn poll_complete_rx(&self, cx: &mut Context<'_>) -> Poll<Result<DescId<Rx>>> {
        // TODO(https://fxbug.bug/78342): The fuchsia_async fifo wrapper API only
        // allows to read one at a time, consider batch reading to reduce the
        // number of syscalls if it becomes a performance issue in the future.
        match ready!(self.rx.try_read(cx)).map_err(|status| Error::Fifo("read", "rx", status))? {
            Some(desc) => Poll::Ready(Ok(desc)),
            None => Err(Error::PeerClosed("rx"))?,
        }
    }

    /// Polls to submit tx descriptors that are pending to the driver.
    ///
    /// Returns the number of tx descriptors that are successfully submitted.
    fn poll_submit_tx(&self, cx: &mut Context<'_>) -> Poll<Result<usize>> {
        self.tx_pending.poll_submit(&self.tx, cx)
    }

    /// Polls completed tx descriptors from the driver then puts them in pool.
    fn poll_complete_tx(&self, cx: &mut Context<'_>) -> Poll<Result<()>> {
        // TODO(https://fxbug.bug/78342): The fuchsia_async fifo wrapper API only
        // allows to read one at a time, consider batch reading to reduce the
        // number of syscalls if it becomes a performance issue in the future.
        match ready!(self.tx.try_read(cx)).map_err(|status| Error::Fifo("read", "tx", status))? {
            Some(head) => Poll::Ready(self.pool.tx_completed(head)),
            None => Err(Error::PeerClosed("tx"))?,
        }
    }

    /// Sends the [`Buffer`] to the driver.
    async fn send(&self, mut buffer: Buffer<Tx>) -> Result<()> {
        buffer.pad()?;
        buffer.commit();
        self.tx_pending.extend(std::iter::once(buffer.leak()));
        Ok(())
    }

    /// Receives a [`Buffer`] from the driver.
    ///
    /// Waits until there is completed rx buffers from the driver.
    async fn recv(&self) -> Result<Buffer<Rx>> {
        poll_fn(|cx| -> Poll<Result<Buffer<Rx>>> {
            let head = ready!(self.poll_complete_rx(cx))?;
            Poll::Ready(self.pool.rx_completed(head))
        })
        .await
    }
}

/// The backing task that drives the session.
///
/// A session can make no progress without this task. All ports will be detached
/// if the task is dropped.
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct Task {
    inner: Arc<Inner>,
}

impl Future for Task {
    type Output = Result<()>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let inner = &Pin::into_inner(self).inner;
        loop {
            let mut all_pending = true;
            // TODO(https://fxbug.dev/78342): poll once for all completed
            // descriptors if this becomes a performance bottleneck.
            while inner.poll_complete_tx(cx)?.is_ready() {
                all_pending = false;
            }
            if inner.poll_submit_rx(cx)?.is_ready() {
                all_pending = false;
            }
            if inner.poll_submit_tx(cx).is_ready() {
                all_pending = false;
            }
            if all_pending {
                return Poll::Pending;
            }
        }
    }
}

/// Session configuration.
#[derive(Debug, Clone, Copy)]
pub struct Config {
    /// Buffer stride on VMO, in bytes.
    buffer_stride: NonZeroU64,
    /// Number of rx descriptors to allocate.
    num_rx_buffers: NonZeroU16,
    /// Number of tx descriptors to allocate.
    num_tx_buffers: NonZeroU16,
    /// Session flags.
    options: netdev::SessionFlags,
    /// Buffer layout.
    buffer_layout: BufferLayout,
}

/// Describes the buffer layout that [`Pool`] needs to know.
#[derive(Debug, Clone, Copy)]
struct BufferLayout {
    /// Minimum tx buffer data length.
    min_tx_data: usize,
    /// Minimum tx buffer head length.
    min_tx_head: u16,
    /// Minimum tx buffer tail length.
    min_tx_tail: u16,
    /// The length of a buffer.
    length: usize,
}

/// Network device information with all required fields.
#[derive(Debug, Clone, ValidFidlTable)]
#[fidl_table_src(netdev::DeviceInfo)]
pub struct DeviceInfo {
    /// Minimum descriptor length, in 64-bit words.
    pub min_descriptor_length: u8,
    /// Accepted descriptor version.
    pub descriptor_version: u8,
    /// Maximum number of items in rx FIFO (per session).
    pub rx_depth: u16,
    /// Maximum number of items in tx FIFO (per session).
    pub tx_depth: u16,
    /// Alignment requirement for buffers in the data VMO.
    pub buffer_alignment: u32,
    /// Maximum supported length of buffers in the data VMO, in bytes.
    pub max_buffer_length: u32,
    /// The minimum rx buffer length required for device.
    pub min_rx_buffer_length: u32,
    /// The minimum tx buffer length required for the device.
    pub min_tx_buffer_length: u32,
    /// The number of bytes the device requests be free as `head` space in a tx buffer.
    pub min_tx_buffer_head: u16,
    /// The amount of bytes the device requests be free as `tail` space in a tx buffer.
    pub min_tx_buffer_tail: u16,
    /// Available rx acceleration flags for this device.
    pub rx_accel: Vec<netdev::RxAcceleration>,
    /// Available tx acceleration flags for this device.
    pub tx_accel: Vec<netdev::TxAcceleration>,
}

impl DeviceInfo {
    /// Create a new session config from the device information.
    ///
    /// This method also does the boundary checks so that data_length/offset fields read
    /// from descriptors are safe to convert to [`usize`].
    pub fn make_config(
        &self,
        buffer_length: usize,
        options: netdev::SessionFlags,
    ) -> Result<Config> {
        let DeviceInfo {
            min_descriptor_length,
            descriptor_version,
            rx_depth,
            tx_depth,
            buffer_alignment,
            max_buffer_length,
            min_rx_buffer_length: _,
            min_tx_buffer_length,
            min_tx_buffer_head,
            min_tx_buffer_tail,
            rx_accel: _,
            tx_accel: _,
        } = self;
        if NETWORK_DEVICE_DESCRIPTOR_VERSION != u32::from(*descriptor_version) {
            return Err(Error::Config(format!(
                "descriptor version mismatch {} != {}",
                NETWORK_DEVICE_DESCRIPTOR_VERSION, descriptor_version
            )));
        }
        if NETWORK_DEVICE_DESCRIPTOR_LENGTH < usize::from(*min_descriptor_length) {
            return Err(Error::Config(format!(
                "descriptor length too small {} < {}",
                NETWORK_DEVICE_DESCRIPTOR_LENGTH, min_descriptor_length
            )));
        }

        let num_rx_buffers =
            NonZeroU16::new(*rx_depth).ok_or_else(|| Error::Config("no RX buffers".to_owned()))?;
        let num_tx_buffers =
            NonZeroU16::new(*tx_depth).ok_or_else(|| Error::Config("no TX buffers".to_owned()))?;

        let larger_than_max_buffer_length = |l: usize| {
            usize::try_from(*max_buffer_length).map_or(
                // This is the case where max_buffer_length can't fix in a
                // usize, but l is already a usize so max_buffer_length > l,
                // i.e. larger_than_max_buffer_length = false.
                false,
                // If converted successfully, we can just do the comparison.
                |max| l > max,
            )
        };

        // This makes sure usize::try_from(data_length).unwrap() is always safe.
        if larger_than_max_buffer_length(buffer_length) {
            return Err(Error::Config(format!(
                "buffer length too big: {} > {}",
                buffer_length, max_buffer_length
            )));
        }

        let buffer_alignment = usize::try_from(*buffer_alignment).map_err(
            |std::num::TryFromIntError { .. }| {
                Error::Config(format!(
                    "buffer_alignment not representable within usize: {}",
                    buffer_alignment,
                ))
            },
        )?;

        let buffer_stride =
            (buffer_length + buffer_alignment - 1) / buffer_alignment * buffer_alignment;

        // This makes sure usize::try_from(offset).unwrap() is always safe.
        if larger_than_max_buffer_length(buffer_stride) {
            return Err(Error::Config(format!(
                "cannot align {} to {} under {}",
                buffer_stride, buffer_alignment, max_buffer_length,
            )));
        }

        if buffer_stride < buffer_length {
            return Err(Error::Config(format!(
                "buffer stride too small {} < {}",
                buffer_stride, buffer_length
            )));
        }

        if buffer_length < usize::from(*min_tx_buffer_head) + usize::from(*min_tx_buffer_tail) {
            return Err(Error::Config(format!(
                "buffer length {} does not meet minimum tx buffer head/tail requirement {}/{}",
                buffer_length, min_tx_buffer_head, min_tx_buffer_tail,
            )));
        }

        let num_buffers =
            rx_depth.checked_add(*tx_depth).filter(|num| *num != u16::MAX).ok_or_else(|| {
                Error::Config(format!(
                    "too many buffers requested: {} + {} > u16::MAX",
                    rx_depth, tx_depth
                ))
            })?;

        let buffer_stride =
            u64::try_from(buffer_stride).map_err(|std::num::TryFromIntError { .. }| {
                Error::Config(format!("buffer_stride too big: {} > u64::MAX", buffer_stride))
            })?;

        // This is following the practice of rust stdlib to ensure allocation
        // size never reaches isize::MAX.
        // https://doc.rust-lang.org/std/primitive.pointer.html#method.add-1.
        match buffer_stride.checked_mul(num_buffers.into()).map(isize::try_from) {
            None | Some(Err(std::num::TryFromIntError { .. })) => {
                return Err(Error::Config(format!(
                    "too much memory required for the buffers: {} * {} > isize::MAX",
                    buffer_stride, num_buffers
                )))
            }
            Some(Ok(_total)) => (),
        };

        let buffer_stride = NonZeroU64::new(buffer_stride)
            .ok_or_else(|| Error::Config("buffer_stride is zero".to_owned()))?;

        // min_tx_buffer_length <= buffer_length <= usize::MAX
        let min_tx_data = usize::try_from(*min_tx_buffer_length).unwrap();
        Ok(Config {
            buffer_stride,
            num_rx_buffers,
            num_tx_buffers,
            options,
            buffer_layout: BufferLayout {
                length: buffer_length,
                min_tx_head: *min_tx_buffer_head,
                min_tx_tail: *min_tx_buffer_tail,
                min_tx_data,
            },
        })
    }

    /// Create a config for a primary session.
    pub fn primary_config(&self, buffer_length: usize) -> Result<Config> {
        self.make_config(buffer_length, netdev::SessionFlags::Primary)
    }
}

/// A port of the device.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Port(u8);

impl From<u8> for Port {
    fn from(p: u8) -> Self {
        Port(p)
    }
}

/// Pending descriptors to be sent to driver.
struct Pending<K: AllocKind> {
    inner: Mutex<(Vec<DescId<K>>, Option<Waker>)>,
}

impl<K: AllocKind> Pending<K> {
    fn new(descs: Vec<DescId<K>>) -> Self {
        Self { inner: Mutex::new((descs, None)) }
    }

    /// Extends the pending descriptors buffer.
    fn extend(&self, descs: impl IntoIterator<Item = DescId<K>>) {
        let mut guard = self.inner.lock();
        let (storage, waker) = &mut *guard;
        storage.extend(descs);
        if let Some(waker) = waker.take() {
            waker.wake();
        }
    }

    /// Submits the pending buffer to the driver through [`zx::Fifo`].
    ///
    /// It will return [`Poll::Pending`] if any of the following happens:
    ///   - There are no descriptors pending.
    ///   - The fifo is not ready for write.
    fn poll_submit(
        &self,
        fifo: &fasync::Fifo<DescId<K>>,
        cx: &mut Context<'_>,
    ) -> Poll<Result<usize>> {
        let mut guard = self.inner.lock();
        let (storage, waker) = &mut *guard;
        if storage.is_empty() {
            *waker = Some(cx.waker().clone());
            return Poll::Pending;
        }
        let submitted = ready!(fifo.try_write(cx, &storage[..]))
            .map_err(|status| Error::Fifo("write", K::REFL.as_str(), status))?;
        let _drained = storage.drain(0..submitted);
        Poll::Ready(Ok(submitted))
    }
}
