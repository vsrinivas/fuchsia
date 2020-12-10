// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::flow_window::FlowWindow;
use crate::prelude::*;
use anyhow::{Context as _, Error};
use core::fmt::Debug;
use core::pin::Pin;
use fidl_fuchsia_lowpan_spinel::DeviceEvent as SpinelDeviceEvent;
use futures::prelude::*;
use futures::task::{Context, Poll};
use futures::{FutureExt, StreamExt};
use std::convert::TryInto;
use std::sync::Arc;

pub(super) const INBOUND_FRAME_WINDOW_SIZE: usize = 8;

/// A helpful adapter around [`fidl_fuchsia_lowpan_spinel::Error`]
/// that implements [`std::error::Error`] and works well with
/// [`anyhow::Error`].
#[derive(Debug, thiserror::Error)]
struct SpinelError(pub fidl_fuchsia_lowpan_spinel::Error);

impl std::fmt::Display for SpinelError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.0)
    }
}

/// Creates a [`SpinelDeviceSink`]/[`SpinelDeviceStream`] pair from
/// an instance of [`fidl_fuchsia_lowpan_spinel::DeviceProxyInterface`]
/// and a [`fidl_fuchsia_lowpan_spinel::DeviceEvent`] stream.
#[allow(dead_code)]
pub fn new_spinel_device_pair<DP, ES>(
    device_proxy: DP,
    event_stream: ES,
) -> (SpinelDeviceSink<DP>, SpinelDeviceStream<DP, ES>)
where
    DP: fidl_fuchsia_lowpan_spinel::DeviceProxyInterface + Clone,
    ES: futures::Stream<Item = Result<SpinelDeviceEvent, fidl::Error>> + Unpin + Send,
{
    let sink = SpinelDeviceSink { device_proxy, send_window: Default::default() };
    let stream = sink.wrap_event_stream(event_stream);
    (sink, stream)
}

/// A trait that provides the ability to sink Spinel frames,
/// open and close the interface, and obtain the max frame size.
///
/// This trait is currently only implemented by `SpinelDeviceSink`,
/// but has been separated out for ergonomic reasons.
#[async_trait::async_trait]
pub trait SpinelDeviceClient:
    for<'a> futures::sink::Sink<&'a [u8], Error = anyhow::Error> + Unpin + Send + Sync + Clone
{
    /// Maps directly to the `DeviceProxyInterface::open()` method
    /// with error consolidation.
    ///
    /// May also call `DeviceProxyInterface::ready_to_receive_frames()`.
    async fn open(&self) -> Result<(), Error>;

    /// Maps directly to the `DeviceProxyInterface::close()` method
    /// with error consolidation.
    async fn close(&self) -> Result<(), Error>;

    /// Maps directly to the `get_max_frame_size::close()` method,
    /// with error consolidation and type casting to `usize`.
    async fn get_max_frame_size(&self) -> Result<usize, Error>;
}

#[async_trait::async_trait]
impl<DP> SpinelDeviceClient for SpinelDeviceSink<DP>
where
    DP: fidl_fuchsia_lowpan_spinel::DeviceProxyInterface + Unpin + Clone,
{
    async fn open(&self) -> Result<(), Error> {
        self.send_window.reset();
        self.device_proxy
            .open()
            .await
            .context("device_proxy.open(): FIDL Error")?
            .map_err(SpinelError)
            .context("device_proxy.open(): Spinel Error")?;
        self.device_proxy
            .ready_to_receive_frames(INBOUND_FRAME_WINDOW_SIZE.try_into().unwrap())
            .context("device_proxy.ready_to_receive_frames(): FIDL Error")?;
        Ok(())
    }

    async fn close(&self) -> Result<(), Error> {
        // Defer to our struct method implementation.
        SpinelDeviceSink::close(&self).await
    }

    async fn get_max_frame_size(&self) -> Result<usize, Error> {
        Ok(self.device_proxy.get_max_frame_size().await? as usize)
    }
}

/// The `Sink` (outbound) side of a Spinel device. This also implements
/// the `SpinelDeviceClient` trait to provide open/close control over
/// the device.
#[derive(Clone)]
pub struct SpinelDeviceSink<DP> {
    device_proxy: DP,
    send_window: Arc<FlowWindow>,
}

/// Specialized methods only available when used directly
/// with [`fidl_fuchsia_lowpan_spinel::DeviceProxy`].
impl SpinelDeviceSink<fidl_fuchsia_lowpan_spinel::DeviceProxy> {
    /// Creates a new instance of `SpinelDeviceSink` from a
    /// [`fidl_fuchsia_lowpan_spinel::DeviceProxy`] instance.
    ///
    /// We limit the method `new` to only being used when
    /// we are using the `DeviceProxy` struct (as opposed to something
    /// else that implements `DeviceProxyInterface`). This is
    /// because we can then directly invoke `take_stream()` (defined below)
    /// to get the [`SpinelDeviceStream`] instance.
    pub fn new(device_proxy: fidl_fuchsia_lowpan_spinel::DeviceProxy) -> Self {
        SpinelDeviceSink { device_proxy, send_window: Default::default() }
    }

    /// Returns an instance of `SpinelDeviceStream` that is associated
    /// with this `SpinelDeviceSink`.
    ///
    /// Note that this method calls `DeviceProxy::take_event_stream()`,
    /// which will panic if called more than once. Thus, this method MUST
    /// also only be invoked once.
    ///
    /// The alternative to invoking this method is to call
    /// `SpinelDeviceSink::wrap_event_stream()`.
    pub fn take_stream(
        &self,
    ) -> SpinelDeviceStream<
        fidl_fuchsia_lowpan_spinel::DeviceProxy,
        fidl_fuchsia_lowpan_spinel::DeviceEventStream,
    > {
        self.wrap_event_stream(self.device_proxy.take_event_stream())
    }
}

impl<DP> Debug for SpinelDeviceSink<DP> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SpinelDeviceSink")
            .field("device_proxy", &())
            .field("send_window", &self.send_window)
            .finish()
    }
}

impl<DP> SpinelDeviceSink<DP>
where
    DP: fidl_fuchsia_lowpan_spinel::DeviceProxyInterface,
{
    /// Wraps the given FIDL `SpinelDeviceEvent` stream into
    /// a `SpinelDeviceStream`.
    ///
    /// This is called to get the stream after creating a [`SpinelDeviceSink`].
    /// If the sink is directly using a `DeviceProxy`, then the method
    /// `SpinelDeviceSink::take_stream` will likely be more convenient
    /// to use.
    pub fn wrap_event_stream<ES>(&self, event_stream: ES) -> SpinelDeviceStream<DP, ES>
    where
        ES: futures::Stream<Item = Result<SpinelDeviceEvent, fidl::Error>> + Unpin + Send,
        DP: Clone,
    {
        SpinelDeviceStream {
            device_proxy: self.device_proxy.clone(),
            event_stream,
            send_window: self.send_window.clone(),
        }
    }

    /// Maps directly to the `DeviceProxyInterface::close()` method,
    /// with additional error consolidation.
    pub fn close(&self) -> impl Future<Output = Result<(), Error>> {
        self.device_proxy.close().map(|r| match r {
            Ok(Ok(())) => Ok(()),
            Ok(Err(err)) => Err(Error::from(SpinelError(err))),
            Err(err) => Err(Error::from(err)),
        })
    }
}

impl<'b, DP> futures::sink::Sink<&'b [u8]> for SpinelDeviceSink<DP>
where
    DP: fidl_fuchsia_lowpan_spinel::DeviceProxyInterface,
{
    type Error = anyhow::Error;

    fn poll_ready(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        match self.send_window.poll_dec(cx, 1) {
            Poll::Ready(()) => Poll::Ready(Ok(())),
            Poll::Pending => {
                traceln!("SPINEL_WAITING_TO_SEND: {:?}", self.send_window);
                Poll::Pending
            }
        }
    }

    fn start_send(self: Pin<&mut Self>, frame: &'b [u8]) -> Result<(), Self::Error> {
        traceln!("SPINEL_SEND: {:x?}", frame);
        Ok(self.device_proxy.send_frame(frame)?)
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        // Nothing to flush.
        Poll::Ready(Ok(()))
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        self.poll_flush(cx)
    }
}

/// The `Stream` (inbound) side of a Spinel device.
pub struct SpinelDeviceStream<DP, ES> {
    device_proxy: DP,
    event_stream: ES,
    send_window: Arc<FlowWindow>,
}

impl<DP, ES> Debug for SpinelDeviceStream<DP, ES> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SpinelDeviceStream")
            .field("device_proxy", &())
            .field("send_window", &self.send_window)
            .field("event_stream", &())
            .finish()
    }
}

impl<DP, ES> futures::stream::Stream for SpinelDeviceStream<DP, ES>
where
    DP: fidl_fuchsia_lowpan_spinel::DeviceProxyInterface + Unpin,
    ES: futures::Stream<Item = Result<SpinelDeviceEvent, fidl::Error>> + Unpin + Send,
{
    type Item = Result<Vec<u8>, Error>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let self_mut = self.get_mut();
        loop {
            match self_mut.event_stream.poll_next_unpin(cx) {
                Poll::Ready(Some(Ok(SpinelDeviceEvent::OnReadyForSendFrames {
                    number_of_frames,
                }))) => {
                    traceln!("SPINEL_READY_FOR_SEND: {}", number_of_frames);
                    self_mut.send_window.inc(number_of_frames);
                    // Continue the loop. Since `poll_next(..) == Ready(..)`, no wakeup is scheduled
                }
                Poll::Ready(Some(Ok(SpinelDeviceEvent::OnReceiveFrame { data }))) => {
                    traceln!("SPINEL_RECV: {:x?}", data);
                    self_mut.device_proxy.ready_to_receive_frames(1)?;
                    return Poll::Ready(Some(Ok(data)));
                }
                Poll::Ready(Some(Ok(SpinelDeviceEvent::OnError { error, .. }))) => {
                    return Poll::Ready(Some(Err(error).map_err(SpinelError).map_err(Error::from)))
                }
                Poll::Ready(Some(Err(error))) => {
                    return Poll::Ready(Some(Err(error).map_err(Error::from)))
                }
                Poll::Ready(None) => return Poll::Ready(None),
                Poll::Pending => return Poll::Pending,
            }
        }
    }
}

#[cfg(test)]
#[allow(unused_mut)]
pub(crate) mod tests {
    use super::*;
    use crate::spinel::mock::*;
    use matches::assert_matches;

    #[test]
    fn test_spinel_device_client_step_by_step() {
        const MAX_FRAME_SIZE: u32 = 2000;
        let noop_waker = futures::task::noop_waker();
        let mut noop_context = futures::task::Context::from_waker(&noop_waker);
        let (
            mut device_sink,
            mut device_stream,
            mut device_event_sender,
            mut device_request_receiver,
        ) = new_mock_spinel_pair(MAX_FRAME_SIZE);

        assert_matches!(device_sink.open().now_or_never(), Some(Ok(())));

        assert_eq!(device_request_receiver.next().now_or_never(), Some(Some(DeviceRequest::Open)));
        assert_eq!(
            device_request_receiver.next().now_or_never(),
            Some(Some(DeviceRequest::ReadyToReceiveFrames(INBOUND_FRAME_WINDOW_SIZE as u32)))
        );

        assert_matches!(device_sink.get_max_frame_size().now_or_never(), Some(Ok(2000)));
        assert_eq!(
            device_request_receiver.next().now_or_never(),
            Some(Some(DeviceRequest::GetMaxFrameSize))
        );

        let frame: Vec<u8> = vec![0x80, 0x01];

        let mut send_future = device_sink.send(frame.as_slice());

        // Must be pending because we haven't gotten the OnReadyForSendFrames yet.
        assert!(send_future.poll_unpin(&mut noop_context).is_pending());

        // This receive future is what will process the OnReadyForSendFrames
        let mut receive_future = device_stream.try_next();
        assert!(receive_future.poll_unpin(&mut noop_context).is_pending());

        // Go ahead and have it be fed in.
        assert_matches!(
            device_event_sender
                .start_send(Ok(SpinelDeviceEvent::OnReadyForSendFrames { number_of_frames: 2 })),
            Ok(_)
        );

        // The receive future needs to run to handle the event
        assert!(receive_future.poll_unpin(&mut noop_context).is_pending());

        // Make sure the send completed successfully.
        assert_matches!(send_future.now_or_never(), Some(Ok(_)));

        // Check that it made the call on the FIDL interface
        assert_eq!(
            device_request_receiver.next().now_or_never(),
            Some(Some(DeviceRequest::SendFrame(frame))),
        );

        let frame: Vec<u8> = vec![0x81, 0x00];

        // Go ahead and make sure the next send doesn't block
        assert_matches!(device_sink.send(frame.as_slice()).now_or_never(), Some(Ok(_)));

        // Check that it made the call on the FIDL interface
        assert_eq!(
            device_request_receiver.next().now_or_never(),
            Some(Some(DeviceRequest::SendFrame(frame)))
        );

        let frame: Vec<u8> = vec![0x82, 0x02, 0x00];

        // But this next call to send must block:
        assert!(device_sink.send(frame.as_slice()).now_or_never().is_none());

        // ...and no frames should have been sent:
        assert_eq!(device_request_receiver.next().now_or_never(), None);

        // Go ahead and send us a frame.
        assert_matches!(
            device_event_sender
                .start_send(Ok(SpinelDeviceEvent::OnReceiveFrame { data: vec![0x81, 00] })),
            Ok(_)
        );

        // Make sure we got the frame
        assert_matches!(receive_future.now_or_never(), Some(Ok(_)));

        // Current implementation acks every frame
        assert!(device_stream.next().now_or_never().is_none());
        assert_eq!(
            device_request_receiver.next().now_or_never(),
            Some(Some(DeviceRequest::ReadyToReceiveFrames(1)))
        );

        // Go ahead and close our device.
        assert_matches!(device_sink.close().now_or_never(), Some(Ok(_)));
        assert_eq!(device_request_receiver.next().now_or_never(), Some(Some(DeviceRequest::Close)));

        assert_matches!(device_sink.close().now_or_never(), Some(Ok(())));
    }
}
