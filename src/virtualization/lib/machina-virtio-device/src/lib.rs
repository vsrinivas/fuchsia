// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Write Machina virtio devices in Rust.
//!
//! This crate aims to simplify the writing of virtio devices as out of process FIDL services by
//! automating boiler plate and providing common wrappers for Machina/Fuchsia specifics.
//!
//! The primary helper is the [`Device`] object that wraps most of the common other helpers, and
//! this must be constructed via the [`DeviceBuilder`], although any of the individual helpers can
//! be used independently without [`Device`] if desired.

mod bell;
mod mem;
mod notify;

pub use bell::{BellError, GuestBellTrap};
pub use mem::{guest_mem_from_vmo, translate_queue, GuestMem};
pub use notify::NotifyEvent;

use {
    fidl_fuchsia_virtualization_hardware::{
        StartInfo, VirtioDeviceReadyResponder, VirtioDeviceRequest, VirtioDeviceRequestStream,
    },
    fuchsia_zircon::{self as zx},
    futures::{task::AtomicWaker, Stream, TryFutureExt, TryStreamExt},
    parking_lot::Mutex,
    std::{
        collections::{hash_map, HashMap},
        pin::Pin,
        task::{Context, Poll},
    },
    thiserror::Error,
    virtio_device::{
        mem::DriverMem,
        queue::{DescChain, DriverNotify, Queue},
        util::DescChainStream,
    },
};

#[derive(Error, Debug)]
pub enum DeviceError {
    #[error("Error with status {0}")]
    Status(#[from] zx::Status),
    #[error("FIDL error {0}")]
    Fidl(#[from] fidl::Error),
    #[error("Queue {0} was invalid for the requested operation")]
    InvalidQueue(u16),
    #[error("Provided configuration for queue {0} was invalid")]
    BadQueueConfig(u16),
    #[error("Received message {0:?} that was unexpected for the current operation")]
    UnexpectedMessage(VirtioDeviceRequest),
    #[error("Stream ended with messages required to complete current operation")]
    UnexpectedEndOfStream,
    #[error(transparent)]
    Bell(#[from] BellError),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

/// Helper to process device messages and build a [`Device`]
///
/// The [`DeviceBuilder`] provides a stateful interface and can assist with process the startup
/// messages for queue configuration that virtio devices need to undertake.
///
/// If a device needs full control of how messages are handled it can use the raw [`add_queue`] and
/// [`build`] methods to build a [`Device`], otherwise it is expected that
/// [`config_builder_from_stream`] will typically be sufficient and automates the message loop.
#[derive(Debug)]
pub struct DeviceBuilder<N> {
    notify: N,
    trap: Option<GuestBellTrap>,
    queues: HashMap<u16, QueueConfig>,
}

impl DeviceBuilder<()> {
    /// Construct a new [`DeviceBuilder`]
    ///
    /// This provides complete flexibility on processing the [`StartInfo`], but typically it is
    /// expected that [`from_start_info`] is the more useful way to get a partially initialized
    /// [`DeviceBuilder`].
    ///
    /// If a [`GuestBellTrap`] exists it can optionally be provided here. If not provided then the
    /// final [`Device`] will not have the trap, although users can always process the stream of
    /// trap notifications themselves.
    pub fn new(trap: Option<GuestBellTrap>) -> DeviceBuilder<()> {
        DeviceBuilder { notify: (), trap, queues: HashMap::new() }
    }
}

impl<N> DeviceBuilder<N> {
    /// Check if a trap has been configured.
    ///
    /// Since it is an error to [`set_trap`] if one already exists

    /// Change the notify to a different type.
    ///
    /// Passes the current notification object to the provided function, and stores any success
    /// result as the new notify.
    ///
    /// When building the final [`Device`] the internal notify will be given to each [`Queue`] that
    /// is constructed. Initially the notify type `N` will always be [`NotifyEvent`] and is created
    /// from the [zx::Event`] provided in the [`StartInfo`].
    ///
    /// The normal reason to want to change the notify type is to interpose a [`BufferedNotify`]
    /// (virtio_device::util::BufferedNotify).
    ///
    /// ```
    /// builder.map_notify(
    ///     |e| Result::<_, zx::Status>::Ok(virtio_device::util::BufferedNotify::new(e))
    /// )
    /// ```
    pub fn map_notify<N2>(
        self,
        map: impl FnOnce(N) -> Result<N2, DeviceError>,
    ) -> Result<DeviceBuilder<N2>, DeviceError> {
        let DeviceBuilder { notify, trap, queues } = self;
        let notify = map(notify)?;
        Ok(DeviceBuilder { notify, trap, queues })
    }

    /// Add the specified [`QueueConfig`] to the list of queues.
    ///
    /// This just records the [`QueueConfig`] and aside from ensuring `config.queue` is not a
    /// duplicate, no further validation is done. The [`Queue`] itself will get build in the
    /// [`build`] step after all queues have been specified.
    pub fn add_queue(mut self, config: QueueConfig) -> Result<Self, DeviceError> {
        let queue = config.queue;
        if self.queues.insert(queue, config).is_some() {
            return Err(DeviceError::InvalidQueue(queue));
        }
        Ok(self)
    }

    /// Query the queues configured so far.
    ///
    /// Returns an iterator of the queue numbers that have been configured so far in the builder.
    pub fn configured_queues(&self) -> impl Iterator<Item = u16> + '_ {
        self.queues.keys().cloned()
    }
}

impl<N: Clone> DeviceBuilder<N> {
    /// Build a [`Device`] from the current builder state.
    ///
    /// This builds all of the queues the were configured and constructs the final [`Device`]. The
    /// negotiated features and a [`DriverMem`] need to given here for queue building.
    pub fn build<'a, M: DriverMem>(
        self,
        negotiated_features: u32,
        mem: &'a M,
    ) -> Result<Device<'a, N>, DeviceError> {
        let DeviceBuilder { notify, trap, queues } = self;
        // Note: Queue does not currently support any features so they're not passed in, but
        // eventually it will.
        let queues = queues
            .into_iter()
            .map(|(queue_num, config)| {
                Queue::new(
                    translate_queue(
                        mem,
                        config.size,
                        config.desc as usize,
                        config.avail as usize,
                        config.used as usize,
                    )
                    .ok_or(DeviceError::BadQueueConfig(queue_num))?,
                    notify.clone(),
                )
                .map(|q| (queue_num, q))
                .ok_or(DeviceError::BadQueueConfig(queue_num))
            })
            .collect::<Result<_, DeviceError>>()?;
        let device = Device {
            notify,
            inner: Mutex::new(Inner { trap, wakers: HashMap::new() }),
            queues,
            features: negotiated_features,
        };
        return Ok(device);
    }
}

/// Construct a [`DeviceBuilder`] from the provided [`StartInfo`]
///
/// Consumes all the handles out of the [`StartInfo`] and will initialize the provided
/// [`GuestMem`] using [`GuestMem::provide_vmo`].
///
/// [`from_start_info`] is the preferred way to construct a [`DeviceBuilder`], this variant exists
/// should your device have unusual requirements on the lifetime of [`GuestMem`].
///
/// The state of [`mem`] is undefined if an `Error` is returned.
pub fn builder_from_start_info(
    info: StartInfo,
    mem: &mut GuestMem,
) -> Result<DeviceBuilder<NotifyEvent>, DeviceError> {
    let StartInfo { trap, guest, event, vmo } = info;
    mem.give_vmo(vmo)?;
    DeviceBuilder::new(
        guest
            .map(|guest| {
                GuestBellTrap::new(&guest, zx::GPAddr(trap.addr as usize), trap.size as usize)
            })
            .transpose()?,
    )
    .map_notify(|_| Ok(NotifyEvent::new(event)))
}

/// Construct a [`DeviceBuilder`] and [`GuestMem`] from the provided [`StartInfo`]
///
/// Consumes all the handles out of the [`StartInfo`] and will return a [`GuestMem`] as well as a
/// [`DeviceBuilder`] that has already been initialized with any traps and notifications sources
/// provided in the [`StartInfo`].
pub fn from_start_info(
    info: StartInfo,
) -> Result<(DeviceBuilder<NotifyEvent>, GuestMem), DeviceError> {
    let mut mem = GuestMem::new();
    let builder = builder_from_start_info(info, &mut mem)?;
    Ok((builder, mem))
}

/// Process a [`VirtioDeviceRequestStream`] to configure all the queues.
///
/// Runs a simple message loop to process all [`VirtioDeviceRequest::ConfigureQueue`] until a
/// [`VirtioDeviceRequest::Ready`] is received. If a device expects no other messages during
/// this configuration then this automates the building. Otherwise if other messages are
/// expected clients will need to run their own message loop and use [`add_queue`].
///
/// On success a [`Device`] and a [`VirtioDeviceReadyResponder`] will be returned. The
/// negotiated features can be [queried](Device::get_features) and then the [responder]
/// (VirtioDeviceReadyResponder) can be signaled once the device is satisfied and able to start.
///
/// A reference to a [`QueueCheck`] must be provided for the builder to know whether to accept
/// or reject any particular queue configuration request.
pub async fn config_builder_from_stream<'a, N: Clone, M: DriverMem, Q: QueueCheck + ?Sized>(
    mut builder: DeviceBuilder<N>,
    stream: &mut VirtioDeviceRequestStream,
    queues: &Q,
    mem: &'a M,
) -> Result<(Device<'a, N>, VirtioDeviceReadyResponder), DeviceError> {
    while let Some(msg) = stream.try_next().await? {
        match msg {
            VirtioDeviceRequest::ConfigureQueue { queue, size, desc, avail, used, responder } => {
                queues.check_queue(queue, builder.configured_queues()).map_err(Into::into)?;
                builder = builder.add_queue(QueueConfig { queue, size, desc, avail, used })?;
                responder.send()?;
            }
            VirtioDeviceRequest::Ready { negotiated_features, responder } => {
                return builder.build(negotiated_features, mem).map(|device| (device, responder));
            }
            x => {
                return Err(DeviceError::UnexpectedMessage(x));
            }
        };
    }
    Err(DeviceError::UnexpectedEndOfStream)
}

struct Inner {
    trap: Option<GuestBellTrap>,
    wakers: HashMap<u16, std::sync::Arc<AtomicWaker>>,
}

/// State for managing a virtio device and its queues using futures.
///
/// Provides a wrapper around one or more [`Queue`]s along with helpers to send and receive queue
/// notifications to the guest driver. These wrappers focus on presenting a asynchronous futures
/// interface on top of the underlying objects.
///
/// Primary this [provides](take_stream) a wrapper around a [`DescChainStream`] for processing
/// descriptor chains. The guest signals that there are descriptors available either using a
/// [`GuestBellTrap`] or via a [`VirtioDeviceRequest::NotifyQueue`] message. Connecting these two
/// sources of notifications to the underlying waker from the [`DescChainStream`] can be done in a
/// most easily using [`run_device_notify`]. It will run forever processing messages from a
/// [`VirtioDeviceRequestStream`], and from any [`GuestBellTrap`], performing [`notify_queue`] as
/// needed.
///
/// If the the device needs to run its own message loop on the stream, and therefore cannot give it
/// to [`run_device_notify`], it can also just [`take_bell_traps`] and use
/// [`GuestBellTrap::complete`] or [`GuestBellTrap::complete_or_pending`] to process them. In this
/// case the device message loop should use [`notify_queue`] for any
/// [`VirtioDeviceRequest::NotifyQueue`] it receives.
///
/// The [`DriverNotify`] object that was configured in the [`DeviceBuilder]` can be retrieved using
/// [`get_notify`]. The notify object might be needed by a device to:
/// - Signal a configuration change
/// - Flush pending queue notifications if something like  [`virtio_device::util::BufferedNotify`]
///   is being used.
pub struct Device<'a, N> {
    notify: N,
    queues: HashMap<u16, Queue<'a, N>>,
    inner: Mutex<Inner>,
    features: u32,
}

impl<'a, N> Device<'a, N> {
    /// Take a [`Stream`] that yields [`DescChain`] for the requested queue
    ///
    /// This returns an error if the specified queue either was not configured in the
    /// [`DeviceBuilder`], or has already been taken and not returned. The
    /// [`WrappedDescChainStream`] that this returns will automatically return itself when dropped.
    ///
    /// Note that the [`Stream`] needs to have its waker signalled to work correctly, see [struct]
    /// (Device) level comment for details.
    pub fn take_stream<'b>(
        &'b self,
        idx: u16,
    ) -> Result<WrappedDescChainStream<'a, 'b, N>, DeviceError> {
        let mut inner = self.inner.lock();
        let entry = if let hash_map::Entry::Vacant(entry) = inner.wakers.entry(idx) {
            entry
        } else {
            return Err(DeviceError::InvalidQueue(idx));
        };
        let queue = self.queues.get(&idx).ok_or(DeviceError::InvalidQueue(idx))?;
        let desc_stream = DescChainStream::new(queue);
        entry.insert(desc_stream.waker());
        Ok(WrappedDescChainStream(idx, desc_stream, self))
    }

    /// Retrieve underlying driver notification object
    pub fn get_notify(&self) -> &N {
        &self.notify
    }

    /// Query the configured queues.
    ///
    /// Returns an iterator of the queue numbers that were configured.
    pub fn configured_queues<'b>(&'b self) -> impl Iterator<Item = u16> + 'b
    where
        'b: 'a,
    {
        self.queues.keys().cloned()
    }

    /// Notify a queue in response to a notification from the driver.
    ///
    /// This signals the waker for the given queue and is required to have the streams returned from
    /// [`take_stream`] yield items.
    ///
    /// See [struct](Device) level documentation for more details.
    pub fn notify_queue(&self, idx: u16) -> Result<(), DeviceError> {
        self.inner.lock().wakers.get(&idx).ok_or(DeviceError::InvalidQueue(idx))?.wake();
        Ok(())
    }

    /// Take any [`GuestBellTraps`] that might have been configured.
    ///
    /// If bell traps were provided in the [`DeviceBuilder`] this returns them. This completely
    /// removes them from the [`Device`] and the caller is now responsible for them and forwarding
    /// any notifications from the driver to [`notify_queue`].
    ///
    /// Internally [`run_device_notify`] uses this to get the bell traps and so once you call it
    /// this will always return a `None`. Similarly if you call this [`run_device_notify`] will
    /// not be able to process bell traps, since you are responsible for them.
    ///
    /// The normal reason to use this is if you need to run your own message loop and cannot use
    /// [`run_device_notify`], in which case you almost always want to
    /// ```
    /// GuestBellTrap::complete_or_pending(device.take_bell_traps(), &device)
    /// ```
    pub fn take_bell_traps(&self) -> Option<GuestBellTrap> {
        self.inner.lock().trap.take()
    }

    /// Return the negotiated features from [`DeviceBuilder::give_ready`]
    pub fn get_features(&self) -> u32 {
        self.features
    }

    /// Run any notifications from the driver till completion.
    ///
    /// Consumes both a [`VirtioDeviceStream`] as well as any [bell traps](take_bell_traps) to
    /// receive any notifications from the driver, for the device, and calls [`notify_queue`] with
    /// them. Will never yield a success and only ever yields an error should either source of
    /// notifications close unexpectedly, or indicate an invalidate queue.
    ///
    /// This method is ideal if you do not need to process device specific messages from the FIDL
    /// channel.
    pub async fn run_device_notify(
        &self,
        stream: VirtioDeviceRequestStream,
    ) -> Result<(), DeviceError> {
        // Each of our notification sources, stream and bell, should not end, as this indicates some
        // underlying connection issue. As such we transform each future to become an error should
        // it ever yield a success value.
        let notify = self.run_device_notify_stream(stream).and_then(|()| {
            futures::future::ready(Result::<(), _>::Err(DeviceError::UnexpectedEndOfStream))
        });
        let bell =
            GuestBellTrap::complete_or_pending(self.take_bell_traps(), self).and_then(|()| {
                futures::future::ready(Result::<(), _>::Err(DeviceError::UnexpectedEndOfStream))
            });
        // Although join tries to run both futures to completion, since we already wrapped each
        // future to yield an error when it completes, this will effectively run both futures till
        // either completes or yields an error, which is the behavior we want.
        futures::future::try_join(notify, bell).await.map(|((), ())| ())
    }

    /// Process all queue notifications on a [`VirtioDeviceRequestStream`]
    ///
    /// Unlike [`run_device_notify`] this does not [`take_bell_trap`] and process those messages.
    /// This will also yield an `Ok(())` should the stream end, leaving the caller to determine if
    /// that is an error condition or not.
    pub async fn run_device_notify_stream(
        &self,
        stream: VirtioDeviceRequestStream,
    ) -> Result<(), DeviceError> {
        stream
            .err_into()
            .try_for_each(|msg| {
                futures::future::ready(match msg {
                    VirtioDeviceRequest::NotifyQueue { queue, .. } => self.notify_queue(queue),
                    msg => Err(DeviceError::UnexpectedMessage(msg)),
                })
            })
            .await
    }
}

/// Raw queue configuration from a [`VirtioDeviceRequest::ConfigureQueue`]
///
/// This is just a [`VirtioDeviceRequest::ConfigureQueue`] without the responder to allow for
/// more easily passing around queue configuration.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct QueueConfig {
    pub queue: u16,
    pub size: u16,
    pub desc: u64,
    pub avail: u64,
    pub used: u64,
}

/// Wrapper around a [`DescChainStream`]
///
/// Yields [`DescChain`] from a [`Stream`] much like [`DescChainStream`], but will de-register
/// itself from its associated [`Device`] on drop.
pub struct WrappedDescChainStream<'a, 'b, N>(u16, DescChainStream<'a, 'b, N>, &'b Device<'a, N>);

impl<'a, 'b, N: DriverNotify> Stream for WrappedDescChainStream<'a, 'b, N> {
    type Item = DescChain<'a, 'b, N>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Pin::new(&mut self.1).poll_next(cx)
    }
}

impl<'a, 'b, N> Drop for WrappedDescChainStream<'a, 'b, N> {
    fn drop(&mut self) {
        self.2.inner.lock().wakers.remove(&self.0).unwrap();
    }
}

/// Describes the queues that are expected and valid for a device
///
/// Provides a way for the automated device building in [`config_builder_from_stream`] to check
/// if a provided queue is valid prior to acknowledging the message and continuing. A device
/// can always opt out of using the [`QueueCheck`] if it does not easily map to their initialization
/// process, however they must then run their own message loop and use [`DeviceBuilder::add_queue`]
/// directly.
///
/// Some devices may only support a fixed set of queues, or they may support a variable number that
/// the guest can configure. This provides flexibility on supporting both. For convenience of the
/// common case of a fixed set of queues, [`QueueCheck`] is implemented for `[u16]` where
/// elements in the slice represent valid queues. This allows for doing:
/// ```
/// config_builder_from_stream(device_builder, stream, &[0,1,2][..], guest_mem).await?;
/// ```
/// This will cause the [`DeviceBuilder`] to allow only those queues to be configured.
/// After `config_builder_from_stream` returns, either the [`Device::configured_queues`], or using
/// [`Device::take_stream`] can be used to validate that all the expected queues were configured,
/// prior to sending on the [`VirtioDeviceReadyResponder`].
/// Note that building the [`Device`] itself does not communicate back to the VMM, and so there is
/// no information leakage by building the [`Device`] prior to being given the opportunity to
/// checking the queues, provided this is done before sending on the ready responder.
pub trait QueueCheck {
    type Error: Into<DeviceError>;
    /// Check a queue that is being added.
    ///
    /// If this returns `Ok(())` then the device acknowledges this is a valid queue, otherwise it
    /// can return an `Err`. An iterator over any queues that have already been added is also
    /// provided, although a guest can configure queues in any order.
    fn check_queue(
        &self,
        queue: u16,
        existing: impl Iterator<Item = u16>,
    ) -> Result<(), Self::Error>;
}

impl QueueCheck for [u16] {
    type Error = DeviceError;
    fn check_queue(
        &self,
        queue: u16,
        _existing: impl Iterator<Item = u16>,
    ) -> Result<(), Self::Error> {
        // Search the slice and ensure this is a queue that was requested.
        self.iter().find(|&&x| x == queue).map(|_| ()).ok_or(DeviceError::InvalidQueue(queue))
    }
}

impl<T: std::ops::RangeBounds<u16>> QueueCheck for T {
    type Error = DeviceError;
    fn check_queue(
        &self,
        queue: u16,
        _existing: impl Iterator<Item = u16>,
    ) -> Result<(), Self::Error> {
        match self.contains(&queue) {
            true => Ok(()),
            false => Err(DeviceError::InvalidQueue(queue)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_virtualization_hardware::VirtioDeviceMarker;
    use fuchsia_async::{self as fasync};
    use matches::assert_matches;
    use virtio_device::util::NotificationCounter;

    // Make a QueueConfig for a given queue size offset in guest memory. Also returns the offset at
    // which the queue ends.
    fn make_queue_config(queue: u16, size: u16, offset: u64) -> (QueueConfig, u64) {
        let round_up = |val| (val + (16 - (val % 16)));
        let desc = round_up(offset);
        let desc_len = std::mem::size_of::<u16>() as u64 * size as u64;
        let avail = round_up(desc + desc_len);
        let avail_len = virtio_device::ring::Driver::avail_len_for_queue_size(size) as u64;
        let used = round_up(avail + avail_len);
        let used_len = virtio_device::ring::Device::used_len_for_queue_size(size) as u64;
        let end = round_up(used + used_len);
        (QueueConfig { queue, size, desc, avail, used }, end)
    }

    fn guest_mem(size: u64) -> GuestMem {
        let vmo = zx::Vmo::create(size).unwrap();
        guest_mem_from_vmo(vmo).unwrap()
    }

    #[test]
    fn queue_check() {
        let queues = &[1, 3][..];
        assert_matches!(queues.check_queue(1, [].iter().cloned()), Ok(()));
        assert_matches!(queues.check_queue(3, [].iter().cloned()), Ok(()));
        assert_matches!(
            queues.check_queue(0, [].iter().cloned()),
            Err(DeviceError::InvalidQueue(0))
        );

        let queues = 1..=2;
        assert_matches!(queues.check_queue(1, [].iter().cloned()), Ok(()));
        assert_matches!(queues.check_queue(2, [].iter().cloned()), Ok(()));
        assert_matches!(
            queues.check_queue(3, [].iter().cloned()),
            Err(DeviceError::InvalidQueue(3))
        );
    }

    #[test]
    fn invalid_queue() {
        // try and add two different queues with the same number
        let (queue1, queue2_offset) = make_queue_config(0, 4, 64);
        let (queue2, _) = make_queue_config(0, 8, queue2_offset);
        assert_matches!(
            DeviceBuilder::new(None).add_queue(queue1).unwrap().add_queue(queue2),
            Err(DeviceError::InvalidQueue(0))
        );
    }

    #[test]
    fn invalid_queue_config() {
        let (queue1, queue1_end) = make_queue_config(0, 4, 64);
        let mem_size = queue1_end
            + (zx::system_get_page_size() as u64
                - (queue1_end % zx::system_get_page_size() as u64));
        let (queue2, _) = make_queue_config(1, 6, mem_size);
        let builder = DeviceBuilder::new(None)
            .add_queue(queue1)
            .unwrap()
            .add_queue(queue2)
            .unwrap()
            .map_notify(|_| Ok(NotificationCounter::new()))
            .unwrap();

        let mem = guest_mem(mem_size);

        assert_matches!(builder.build(0, &mem).err(), Some(DeviceError::BadQueueConfig(1)));
    }

    #[fasync::run_until_stalled(test)]
    async fn builder_from_stream() -> Result<(), anyhow::Error> {
        let (queue1, queue1_end) = make_queue_config(0, 4, 64);
        let (queue2, queue2_end) = make_queue_config(1, 8, queue1_end);
        let mem = guest_mem(queue2_end);

        let builder =
            DeviceBuilder::new(None).map_notify(|_| Ok(NotificationCounter::new())).unwrap();

        let (vmm_side, device_side) = fidl::endpoints::create_endpoints::<VirtioDeviceMarker>()?;
        let vmm_side = vmm_side.into_proxy()?;
        let mut device_side = device_side.into_stream()?;

        let device_fut = config_builder_from_stream(builder, &mut device_side, &(0..=1), &mem);

        let config_fut = async {
            vmm_side
                .configure_queue(queue1.queue, queue1.size, queue1.desc, queue1.avail, queue1.used)
                .await
                .unwrap();
            // send a notify instead of a ready to cause an error.
            vmm_side.notify_queue(0).unwrap();
        };

        assert_matches!(
            futures::join!(device_fut, config_fut).0.err(),
            Some(DeviceError::UnexpectedMessage(_))
        );

        // Now build again with a ready that should succeed.

        let builder =
            DeviceBuilder::new(None).map_notify(|_| Ok(NotificationCounter::new())).unwrap();

        let (vmm_side, device_side) = fidl::endpoints::create_endpoints::<VirtioDeviceMarker>()?;
        let vmm_side = vmm_side.into_proxy()?;
        let mut device_side = device_side.into_stream()?;

        let device_fut = config_builder_from_stream(builder, &mut device_side, &(0..=1), &mem)
            .map_ok(|(device, responder)| {
                responder.send().unwrap();
                device
            });

        let config_fut = async {
            vmm_side
                .configure_queue(queue1.queue, queue1.size, queue1.desc, queue1.avail, queue1.used)
                .await
                .unwrap();
            vmm_side
                .configure_queue(queue2.queue, queue2.size, queue2.desc, queue2.avail, queue2.used)
                .await
                .unwrap();
            vmm_side.ready(3).await.unwrap();
        };

        let device = futures::join!(device_fut, config_fut).0.unwrap();

        // Check the features and queues are in the device
        assert_eq!(device.get_features(), 3);
        assert!(device.take_stream(0).is_ok());
        assert!(device.take_stream(1).is_ok());

        Ok(())
    }
}
