// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::server::{HangingGet, Subscriber},
    core::{
        convert::{TryFrom, TryInto},
        pin::Pin,
        task::{Context, Poll},
    },
    fidl::endpoints::{ClientEnd, RequestStream},
    fidl_fuchsia_bluetooth_hfp::{
        HeadsetGainMarker, HeadsetGainRequest, HeadsetGainRequestStream,
        HeadsetGainWatchMicrophoneGainResponder, HeadsetGainWatchSpeakerGainResponder,
    },
    fuchsia_zircon as zx,
    futures::{
        ready,
        stream::{FusedStream, Stream, StreamExt},
    },
    tracing::info,
};

use super::update::AgUpdate;
use crate::error::Error;

type NotifyFn<Resp> = Box<dyn Fn(&u8, Resp) -> bool>;
type GainHangingGet<Resp> = HangingGet<u8, Resp, NotifyFn<Resp>>;
type GainSubscriber<Resp> = Subscriber<u8, Resp, NotifyFn<Resp>>;

/// Valid Gain value for the Hands Free Speaker or Microphone.
/// Value is in the range 0-15, inclusive.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct Gain(u8);

impl From<Gain> for u8 {
    fn from(gain: Gain) -> Self {
        gain.0
    }
}

impl From<Gain> for i64 {
    fn from(gain: Gain) -> Self {
        gain.0 as i64
    }
}

impl TryFrom<u8> for Gain {
    type Error = Error;
    fn try_from(x: u8) -> Result<Self, Self::Error> {
        if x <= 15 {
            Ok(Gain(x))
        } else {
            Err(Error::OutOfRange)
        }
    }
}

impl TryFrom<i64> for Gain {
    type Error = Error;
    fn try_from(x: i64) -> Result<Self, Self::Error> {
        if 0 <= x && x <= 15 {
            Ok(Gain(x as u8))
        } else {
            Err(Error::OutOfRange)
        }
    }
}

/// Represents the component's representation of a peer's Gain values for both Speaker and
/// Microphone.
///
/// Gain reports from the peer can be reported to this component which will update the Call Manager
/// if the value has changed and the Call Manager is watching for updates.
///
/// Receives requests to set the gain from the call manager and produces them as stream output.
/// After the ClientEnd of the associated HeadsetGain FIDL protocol closes, the GainControl stream
/// is terminated.
pub(super) struct GainControl {
    client_end: Option<ClientEnd<HeadsetGainMarker>>,
    request_stream: HeadsetGainRequestStream,
    speaker_gain_hanging_get: GainHangingGet<HeadsetGainWatchSpeakerGainResponder>,
    speaker_gain_hanging_get_subscriber: GainSubscriber<HeadsetGainWatchSpeakerGainResponder>,
    microphone_gain_hanging_get: GainHangingGet<HeadsetGainWatchMicrophoneGainResponder>,
    microphone_gain_hanging_get_subscriber: GainSubscriber<HeadsetGainWatchMicrophoneGainResponder>,
    terminated: bool,
}

impl GainControl {
    pub fn new() -> Result<Self, Error> {
        let f: NotifyFn<HeadsetGainWatchSpeakerGainResponder> = Box::new(|s, r| {
            let _ = r.send(*s);
            true
        });
        let mut speaker_gain_hanging_get = HangingGet::new(0, f);
        let f: NotifyFn<HeadsetGainWatchMicrophoneGainResponder> = Box::new(|s, r| {
            let _ = r.send(*s);
            true
        });
        let mut microphone_gain_hanging_get = HangingGet::new(0, f);
        let (client_end, request_stream) = fidl::endpoints::create_request_stream()
            .map_err(|e| Error::system("Could not create gain control fidl endpoints", e))?;
        Ok(Self {
            client_end: Some(client_end),
            request_stream,
            speaker_gain_hanging_get_subscriber: speaker_gain_hanging_get.new_subscriber(),
            speaker_gain_hanging_get,
            microphone_gain_hanging_get_subscriber: microphone_gain_hanging_get.new_subscriber(),
            microphone_gain_hanging_get,
            terminated: false,
        })
    }

    /// Take the client end of the FIDL channel or else create a new instance to replace `self`.
    /// The new client end is returned if a new instance is created.
    pub fn get_client_end(&mut self) -> Result<ClientEnd<HeadsetGainMarker>, Error> {
        let client_end = match self.client_end.take() {
            Some(client_end) => client_end,
            None => {
                *self = GainControl::new()?;
                self.client_end
                    .take()
                    .expect("Client end must exist because GainControl was just created")
            }
        };
        Ok(client_end)
    }

    /// Report the speaker gain value received from the Hands Free to the call manager.
    pub fn report_speaker_gain(&mut self, gain: Gain) {
        self.speaker_gain_hanging_get.new_publisher().update(move |s| {
            if *s == gain.0 {
                false
            } else {
                *s = gain.0;
                true
            }
        });
    }

    /// Report the microphone gain value received from the Hands Free to the call manager.
    pub fn report_microphone_gain(&mut self, gain: Gain) {
        self.microphone_gain_hanging_get.new_publisher().update(move |s| {
            if *s == gain.0 {
                false
            } else {
                *s = gain.0;
                true
            }
        });
    }

    /// Handle an incoming fidl request. Optionally returns a GainRequest if the fidl request must
    /// be handled outside of `GainControl`
    fn handle_gain_control_request(
        &mut self,
        request: HeadsetGainRequest,
    ) -> Result<Option<GainRequest>, Error> {
        let result = match request {
            HeadsetGainRequest::SetSpeakerGain { requested, .. } => {
                Some(GainRequest::Speaker(requested.try_into()?))
            }
            HeadsetGainRequest::SetMicrophoneGain { requested, .. } => {
                Some(GainRequest::Microphone(requested.try_into()?))
            }
            HeadsetGainRequest::WatchSpeakerGain { responder, .. } => {
                self.speaker_gain_hanging_get_subscriber.register(responder)?;
                None
            }
            HeadsetGainRequest::WatchMicrophoneGain { responder, .. } => {
                self.microphone_gain_hanging_get_subscriber.register(responder)?;
                None
            }
        };
        Ok(result)
    }
}

impl From<GainRequest> for AgUpdate {
    fn from(gain_request: GainRequest) -> Self {
        match gain_request {
            GainRequest::Speaker(gain) => AgUpdate::SpeakerVolumeControl(gain),
            GainRequest::Microphone(gain) => AgUpdate::MicrophoneVolumeControl(gain),
        }
    }
}

/// Item type returned by `<GainControl as Stream>::poll_next`.
#[derive(Debug)]
pub(crate) enum GainRequest {
    Speaker(Gain),
    Microphone(Gain),
}

// GainControl's Stream implementation wraps a GainControlRequestStream. It handles the requests
// coming off the request_stream by updating its internal state or by returning an Item, depending
// on what type of request is received from the request_stream. Without being polled as a stream,
// `GainControl` cannot handle GainControlRequests.
impl Stream for GainControl {
    type Item = GainRequest;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("Cannot poll a terminated stream");
        }

        // Keep polling the request stream in a loop until it produces a request that should be
        // returned from GainControl::poll_next or it produces Poll::Pending.
        loop {
            let result = ready!(self.request_stream.poll_next_unpin(cx));

            let result = match result {
                Some(Ok(request)) => match self.handle_gain_control_request(request) {
                    Ok(None) => continue,
                    Ok(Some(request)) => Some(request),
                    Err(e) => {
                        info!("Error from HeadsetGain client: {}. Dropping connection", e);
                        self.request_stream
                            .control_handle()
                            .shutdown_with_epitaph(zx::Status::INVALID_ARGS);
                        None
                    }
                },
                Some(Err(e)) => {
                    info!("Error from HeadsetGain client: {}. Dropping connection", e);
                    None
                }
                None => None,
            };
            if result.is_none() {
                self.terminated = true;
            }
            return Poll::Ready(result);
        }
    }
}

impl FusedStream for GainControl {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, async_utils::PollExt, fidl_fuchsia_bluetooth_bredr as bredr,
        fuchsia_async as fasync, matches::assert_matches,
    };

    #[fuchsia::test]
    fn new_gain_control_succeeds() {
        let _exec = fasync::TestExecutor::new().unwrap();
        GainControl::new().expect("a success value");
    }

    #[fuchsia::test]
    fn get_client_end_leaves_field_empty() {
        let _exec = fasync::TestExecutor::new().unwrap();

        let mut control = GainControl::new().expect("a success value");
        assert!(control.client_end.is_some());

        control.get_client_end().expect("success in taking client end");
        assert!(control.client_end.is_none());

        // taking a second time creates a new gain control and still takes the client_end
        control.get_client_end().expect("success in taking client end");
        assert!(control.client_end.is_none());
    }

    #[fuchsia::test]
    fn stream_returns_pending_without_client_interaction() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let mut ctrl = GainControl::new().unwrap();
        let result = exec.run_until_stalled(&mut ctrl.next());
        assert!(result.is_pending());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn stream_is_terminated_when_client_end_is_closed() {
        let mut ctrl = GainControl::new().unwrap();
        let client_end = ctrl.get_client_end().unwrap();
        assert!(!ctrl.is_terminated());

        drop(client_end);
        let result = ctrl.next().await;
        assert!(result.is_none());
        assert!(ctrl.is_terminated());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn stream_error_returns_none_and_terminates() {
        let mut ctrl = GainControl::new().unwrap();
        let client_end = ctrl.get_client_end().unwrap();
        let wrong_client_end: ClientEnd<bredr::ProfileMarker> =
            ClientEnd::new(client_end.into_channel());
        let wrong_proxy = wrong_client_end.into_proxy().unwrap();

        let _resp = wrong_proxy.connect(
            &mut fidl_fuchsia_bluetooth::PeerId { value: 1 },
            &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters::EMPTY),
        );

        let result = ctrl.next().await;
        assert!(result.is_none());
        assert!(ctrl.is_terminated());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn stream_interaction_returns_valid_values() {
        let mut ctrl = GainControl::new().unwrap();
        let proxy = ctrl.get_client_end().unwrap().into_proxy().unwrap();

        proxy.set_speaker_gain(1).expect("success sending a set speaker gain request");
        proxy.set_microphone_gain(2).expect("success sending a set microphone gain request");

        let result = ctrl.next().await.expect("a valid value");
        assert_matches!(result, GainRequest::Speaker(Gain(1)));
        let result = ctrl.next().await.expect("a valid value");
        assert_matches!(result, GainRequest::Microphone(Gain(2)));

        assert!(!ctrl.is_terminated());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn invalid_gain_request_returns_none_and_terminates_with_epitaph() {
        let mut ctrl = GainControl::new().unwrap();
        let proxy = ctrl.get_client_end().unwrap().into_proxy().unwrap();

        proxy.set_speaker_gain(16).expect("success sending a set speaker gain request");

        assert!(ctrl.next().await.is_none());
        assert!(ctrl.is_terminated());

        let result = proxy.watch_speaker_gain().await;
        assert_matches!(
            result,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::INVALID_ARGS, .. })
        );
    }

    #[fuchsia::test]
    fn speaker_hanging_get_produces_values() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let mut ctrl = GainControl::new().unwrap();
        let proxy = ctrl.get_client_end().unwrap().into_proxy().unwrap();

        let mut speaker_gain_fut = proxy.watch_speaker_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());

        let result = exec.run_until_stalled(&mut speaker_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(0));

        let mut speaker_gain_fut = proxy.watch_speaker_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());
        assert!(exec.run_until_stalled(&mut speaker_gain_fut).is_pending());

        // report a gain value to respond to the hanging get
        ctrl.report_speaker_gain(Gain(1));

        let result = exec.run_until_stalled(&mut speaker_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(1));

        let mut speaker_gain_fut = proxy.watch_speaker_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());
        assert!(exec.run_until_stalled(&mut speaker_gain_fut).is_pending());

        // reporting an identical gain value will not complete the hanging get
        ctrl.report_speaker_gain(Gain(1));
        assert!(exec.run_until_stalled(&mut speaker_gain_fut).is_pending());

        // reporting a new value will finally complete the hanging get
        ctrl.report_speaker_gain(Gain(2));

        let result = exec.run_until_stalled(&mut speaker_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(2));
    }

    #[fuchsia::test]
    fn microphone_hanging_get_produces_values() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let mut ctrl = GainControl::new().unwrap();
        let proxy = ctrl.get_client_end().unwrap().into_proxy().unwrap();

        let mut microphone_gain_fut = proxy.watch_microphone_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());

        let result = exec.run_until_stalled(&mut microphone_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(0));

        let mut microphone_gain_fut = proxy.watch_microphone_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());
        assert!(exec.run_until_stalled(&mut microphone_gain_fut).is_pending());

        // report a gain value to respond to the hanging get
        ctrl.report_microphone_gain(Gain(1));

        let result = exec.run_until_stalled(&mut microphone_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(1));

        let mut microphone_gain_fut = proxy.watch_microphone_gain();
        let _ = exec.run_until_stalled(&mut ctrl.next());
        assert!(exec.run_until_stalled(&mut microphone_gain_fut).is_pending());

        // reporting an identical gain value will not complete the hanging get
        ctrl.report_microphone_gain(Gain(1));
        assert!(exec.run_until_stalled(&mut microphone_gain_fut).is_pending());

        // reporting a new value will finally complete the hanging get
        ctrl.report_microphone_gain(Gain(2));

        let result = exec.run_until_stalled(&mut microphone_gain_fut).expect("future to complete");
        assert_matches!(result, Ok(2));
    }
}
