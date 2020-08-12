// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_avrcp as avrcp, fidl_fuchsia_media as media,
    fidl_fuchsia_settings as settings,
    fuchsia_async::{self as fasync, DurationExt, Timer},
    fuchsia_syslog::{self, fx_log_info, fx_log_warn, fx_vlog},
    fuchsia_zircon as zx,
    futures::{
        channel::oneshot::Sender,
        future::{Fuse, FusedFuture},
        pin_mut, select, Future, FutureExt, StreamExt,
    },
    std::fmt::Debug,
};

pub(crate) struct VolumeRelay {
    /// A sender that when sent will cause the relay task to stop. None if the task is not running.
    _stop: Option<Sender<()>>,
}

struct AvrcpVolume(u8);

impl AvrcpVolume {
    /// Convert from a settings volume between 0.0 and 1.0 to a volume that can be sent
    /// through AVRCP (0 to 127 as per the spec)
    fn from_media_volume(value: settings::AudioSettings) -> Result<Self, anyhow::Error> {
        let streams = value.streams.ok_or(format_err!("No streams in the AudioSettings"))?;

        // Find the media stream volume
        let volume =
            match streams.iter().find(|&s| s.stream == Some(media::AudioRenderUsage::Media)) {
                None => Err(format_err!("Couldn't find Media stream in settings")),
                Some(settings::AudioStreamSettings { user_volume: None, .. }) => {
                    Err(format_err!("Volume not included in Media stream settings"))
                }
                Some(settings::AudioStreamSettings { user_volume: Some(vol), .. }) => Ok(vol),
            };
        let level = match volume? {
            settings::Volume { muted: Some(true), .. } => 0.0,
            settings::Volume { level: None, .. } => 0.0,
            settings::Volume { level: Some(vol), .. } => *vol,
        };

        Ok(AvrcpVolume((level * 127.0) as u8))
    }

    /// Get an AudioSettings struct that can be sent to the Settings service to set the volume
    /// to the same level as this.  Converts from native AVRCP (0-127) to settings 0.0 - 1.0
    /// ranges.
    fn as_audio_settings(&self, stream: media::AudioRenderUsage) -> settings::AudioSettings {
        let settings = settings::AudioStreamSettings {
            stream: Some(stream),
            source: Some(settings::AudioStreamSettingSource::User),
            user_volume: Some(settings::Volume {
                level: Some(self.0 as f32 / 127.0),
                muted: Some(false),
            }),
        };
        settings::AudioSettings { streams: Some(vec![settings]), input: None }
    }
}

/// How long we will wait for the system to report a new volume after requesting a new volume.
/// This is chosen as 100 milliseconds as half of the required response time in the AVRCP Spec,
/// Section 6.2
const SETVOLUME_TIMEOUT: zx::Duration = zx::Duration::from_millis(100);

impl VolumeRelay {
    /// Start a relay between AVRCP and Settings.Audio.
    /// Media Volume is reported to AVRCP for Absolute Volume Controllers, and changes from AVRCP
    /// are propagated to the system Media volume.
    /// This starts the relay.  The relay can be stopped by dropping it.
    pub(crate) fn start() -> Result<Self, Error> {
        let avrcp_svc = fuchsia_component::client::connect_to_service::<avrcp::PeerManagerMarker>()
            .context("Failed to connect to Bluetooth AVRCP interface")?;
        let audio_settings_svc =
            fuchsia_component::client::connect_to_service::<settings::AudioMarker>()
                .context("Failed to connect to Audio settings interface")?;

        let (sender, receiver) = futures::channel::oneshot::channel();

        spawn_err("Volume", Self::volume_relay(avrcp_svc, audio_settings_svc, receiver.fuse()));

        Ok(Self { _stop: Some(sender) })
    }

    async fn volume_relay(
        mut avrcp: avrcp::PeerManagerProxy,
        audio: settings::AudioProxy,
        mut stop_signal: impl FusedFuture + Unpin,
    ) -> Result<(), Error> {
        let mut volume_requests =
            connect_avrcp_volume(&mut avrcp).await.context("connecting avrcp volume")?;

        let audio_proxy_clone = audio.clone();
        let mut audio_watch_stream =
            HangingGetStream::new(Box::new(move || Some(audio_proxy_clone.watch())));

        // Wait for the first update from the settings app.
        let mut current_volume = match audio_watch_stream.next().await {
            None => return Err(format_err!("Volume watch response stream ended")),
            Some(Err(e)) => return Err(format_err!("FIDL error polling audio watch: {:?}", e)),
            Some(Ok(settings)) => match AvrcpVolume::from_media_volume(settings) {
                Err(e) => return Err(format_err!("Can't get initial volume: {:?}", e)),
                Ok(vol) => vol.0,
            },
        };
        fx_vlog!(1, "Initial system media volume level is {:?} in AVRCP", current_volume);
        let mut staged_volume = Some(current_volume);

        let mut last_onchanged = None;
        // TODO(54002): Change this to be a single responder when AVRCP correctly manages the
        // lifetime of volume changed subscriptions.
        let mut hanging_onchanged = Vec::new();
        let mut hanging_setvolumes = Vec::new();

        let setvolume_timeout = Fuse::terminated();
        pin_mut!(setvolume_timeout);

        loop {
            let mut sys_volume_watch_fut = audio_watch_stream.next();
            let mut avrcp_request_fut = volume_requests.next();

            select! {
                _ = stop_signal => {
                    fx_vlog!(1, "AVRCP volume relay ending on stop signal");
                    break Ok(());
                },
                avrcp_request = avrcp_request_fut => {
                    let request = match avrcp_request {
                        None => return Err(format_err!("AVRCP Volume Handler Channel Closed")),
                        Some(Err(e)) => return Err(format_err!("Volume Handler Request Error: {:?}", e)),
                        Some(Ok(req)) => req,
                    };
                    match request {
                        avrcp::AbsoluteVolumeHandlerRequest::SetVolume { requested_volume, responder } => {
                            let settings = AvrcpVolume(requested_volume).as_audio_settings(media::AudioRenderUsage::Media);
                            fx_vlog!(1, "AVRCP Setting system volume to {} -> {:?}", requested_volume, settings);
                            if let Err(e) = audio.set(settings).await {
                                fx_log_warn!("Couldn't set media volume: {:?}", e);
                                let _ = responder.send(current_volume);
                                continue;
                            }
                            hanging_setvolumes.push(responder);
                            if setvolume_timeout.is_terminated() {
                                setvolume_timeout.set(Timer::new(SETVOLUME_TIMEOUT.after_now()).fuse());
                            }
                        },
                        avrcp::AbsoluteVolumeHandlerRequest::OnVolumeChanged { responder } => {
                            hanging_onchanged.push(responder);
                        },
                        avrcp::AbsoluteVolumeHandlerRequest::GetCurrentVolume { responder } => {
                            let _ = responder.send(current_volume);
                            continue;
                        }
                    }
                },
                _ = setvolume_timeout => {
                    for responder in hanging_setvolumes.drain(..) {
                        fx_vlog!(1, "Timed out - reporting result of SetVolume as {}", current_volume);
                        let _  = responder.send(current_volume);
                    }
                },
                watch_response = sys_volume_watch_fut => {
                    let settings = match watch_response {
                        None => return Err(format_err!("Volume watch response stream ended")),
                        Some(Err(e)) => return Err(format_err!("FIDL error from watch: {:?}", e)),
                        Some(Ok(settings)) => settings,
                    };

                    current_volume = match AvrcpVolume::from_media_volume(settings) {
                        Err(e) => {
                            fx_log_warn!("Volume Relay can't get volume: {:?}", e);
                            continue;
                        },
                        Ok(vol) => vol.0,
                    };

                    fx_vlog!(1, "System media volume level now at {:?} in AVRCP", current_volume);
                    if hanging_setvolumes.len() > 0 {
                        for responder in hanging_setvolumes.drain(..) {
                            fx_vlog!(1, "Reporting result of SetVolume as {}", current_volume);
                            let _  = responder.send(current_volume);
                        }
                        // When the change is the result of a setvolume command, the onchanged
                        // hanging is _not_ updated.
                        last_onchanged = Some(current_volume);
                        continue;
                    }
                    staged_volume = Some(current_volume);
                },
            }

            if !hanging_onchanged.is_empty() && staged_volume.is_some() {
                let next_volume = staged_volume.take().unwrap();
                if Some(next_volume) == last_onchanged {
                    fx_vlog!(1, "Not reporting unchanged volume {} to AVRCP", next_volume);
                    continue;
                }

                for responder in hanging_onchanged.drain(..) {
                    let _ = responder.send(next_volume);
                }
                fx_vlog!(1, "Reporting changed system volume {} to AVRCP", next_volume);
                last_onchanged = Some(next_volume);
            }
        }
    }
}

fn spawn_err<F, E>(label: &'static str, future: F)
where
    F: Future<Output = Result<(), E>> + Send + 'static,
    E: Debug,
{
    fasync::Task::spawn(async move {
        if let Some(e) = future.await.err() {
            fx_log_info!("{} Completed with Error: {:?}", label, e);
        }
    })
    .detach();
}

async fn connect_avrcp_volume(
    avrcp: &mut avrcp::PeerManagerProxy,
) -> Result<avrcp::AbsoluteVolumeHandlerRequestStream, Error> {
    let (client, request_stream) = endpoints::create_request_stream()?;

    if let Err(e) = avrcp.set_absolute_volume_handler(client).await? {
        fx_log_info!("failed to set absolute volume handler");
        return Err(format_err!("Failed setting absolute volume handler: {}", e));
    }

    Ok(request_stream)
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::encoding::Decodable;
    use fidl::endpoints;
    use fuchsia_zircon::DurationNum;
    use futures::{channel::oneshot::Sender, task::Poll, Future};
    use std::pin::Pin;

    const INITIAL_MEDIA_VOLUME: f32 = 0.8;
    const INITIAL_AVRCP_VOLUME: u8 = 101;

    const NEW_MEDIA_VOLUME: f32 = 0.9;
    const NEW_AVRCP_VOLUME: u8 = 114;

    fn setup_avrcp_proxy(
    ) -> Result<(avrcp::PeerManagerProxy, avrcp::PeerManagerRequestStream), fidl::Error> {
        endpoints::create_proxy_and_stream::<avrcp::PeerManagerMarker>()
    }

    fn setup_settings_proxy(
    ) -> Result<(settings::AudioProxy, settings::AudioRequestStream), fidl::Error> {
        endpoints::create_proxy_and_stream::<settings::AudioMarker>()
    }

    /// Builds all of the Proxies and request streams involved with setting up a Volume Relay
    /// test.
    fn setup_volume_relay() -> Result<
        (
            settings::AudioRequestStream,
            avrcp::PeerManagerRequestStream,
            Sender<()>,
            impl Future<Output = Result<(), Error>>,
        ),
        fidl::Error,
    > {
        let (settings_proxy, settings_requests) = setup_settings_proxy()?;
        let (avrcp_proxy, avrcp_requests) = setup_avrcp_proxy()?;

        let (stop_sender, receiver) = futures::channel::oneshot::channel();

        let relay_fut = VolumeRelay::volume_relay(avrcp_proxy, settings_proxy, receiver.fuse());
        Ok((settings_requests, avrcp_requests, stop_sender, relay_fut))
    }

    /// Expects a Watch() call to the `audio_request_stream`.  Returns the handler to respond to
    /// the watch call, or panics if that doesn't happen.
    fn expect_audio_watch(
        exec: &mut fasync::Executor,
        audio_request_stream: &mut settings::AudioRequestStream,
    ) -> settings::AudioWatchResponder {
        let watch_request_fut = audio_request_stream.select_next_some();
        pin_mut!(watch_request_fut);

        match exec.run_until_stalled(&mut watch_request_fut) {
            Poll::Ready(Ok(settings::AudioRequest::Watch { responder })) => responder,
            x => panic!("Expected an Audio Watch Request, got {:?}", x),
        }
    }

    fn respond_to_audio_watch(responder: settings::AudioWatchResponder, level: f32) {
        responder
            .send(settings::AudioSettings {
                streams: Some(vec![settings::AudioStreamSettings {
                    stream: Some(media::AudioRenderUsage::Media),
                    user_volume: Some(settings::Volume {
                        level: Some(level),
                        ..settings::Volume::new_empty()
                    }),
                    ..settings::AudioStreamSettings::new_empty()
                }]),
                ..settings::AudioSettings::new_empty()
            })
            .expect("watch responder to send");
    }

    /// Confirms the setup of the Volume relay, which includes the registration of a volume
    /// handler proxy with the AVRCP client, and the initial volume setting request to the Media
    /// system.
    fn finish_relay_setup<T: Future>(
        mut relay_fut: &mut Pin<&mut T>,
        mut exec: &mut fasync::Executor,
        mut avrcp_request_stream: avrcp::PeerManagerRequestStream,
        audio_request_stream: &mut settings::AudioRequestStream,
    ) -> (avrcp::AbsoluteVolumeHandlerProxy, settings::AudioWatchResponder)
    where
        <T as Future>::Output: Debug,
    {
        // Expect registration of a AbsoluteVolumeHandler
        let request_fut = avrcp_request_stream.select_next_some();
        pin_mut!(request_fut);

        let handler = match exec.run_until_stalled(&mut request_fut) {
            Poll::Ready(Ok(avrcp::PeerManagerRequest::SetAbsoluteVolumeHandler {
                handler,
                responder,
            })) => {
                responder.send(&mut Ok(())).expect("response to handler set");
                handler
            }
            x => panic!("Expected SetAbsoluteVolumeHandler, got: {:?}", x),
        };

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let audio_watch_responder = expect_audio_watch(&mut exec, audio_request_stream);
        respond_to_audio_watch(audio_watch_responder, INITIAL_MEDIA_VOLUME);

        match exec.run_until_stalled(&mut relay_fut) {
            Poll::Pending => {}
            x => panic!("Expected relay to be pending, got {:?}", x),
        };
        let audio_watch_responder = expect_audio_watch(&mut exec, audio_request_stream);

        (handler.into_proxy().expect("absolute volume handler proxy"), audio_watch_responder)
    }

    /// Test that the relay sets up the connection to AVRCP and Sessions and stops on the stop
    /// signal.
    #[test]
    fn test_relay_setup() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");
        let (mut settings_requests, avrcp_requests, stop_sender, relay_fut) = setup_volume_relay()?;

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (volume_client, _watch_responder) =
            finish_relay_setup(&mut relay_fut, &mut exec, avrcp_requests, &mut settings_requests);

        // Sending a stop should drop all the things and the future should complete.
        stop_sender.send(()).expect("should be able to send a stop");

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_ready());

        match exec.run_until_stalled(&mut settings_requests.next()) {
            Poll::Ready(None) => {}
            x => panic!("Expected settings to be dropped, but got {:?}", x),
        };

        let mut current_volume_fut = volume_client.get_current_volume();
        match exec.run_until_stalled(&mut current_volume_fut) {
            Poll::Ready(Err(_e)) => {}
            x => panic!("Expected volume to be disconnected, but got {:?} from watch_info", x),
        };
        Ok(())
    }

    /// Test that the relay calls the set volume command correctly and responds within an
    /// appropriate amount of time.
    #[test]
    fn test_set_volume_command() -> Result<(), Error> {
        let mut exec = fasync::Executor::new_with_fake_time().expect("executor needed");
        let (mut settings_requests, avrcp_requests, _stop_sender, relay_fut) =
            setup_volume_relay()?;

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (volume_client, watch_responder) =
            finish_relay_setup(&mut relay_fut, &mut exec, avrcp_requests, &mut settings_requests);

        // The volume set here does not need to match below.
        let volume_set_fut = volume_client.set_volume(0);
        pin_mut!(volume_set_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        match exec.run_until_stalled(&mut volume_set_fut) {
            Poll::Pending => {}
            x => panic!("Expected request to be unfinished, but got {:?}", x),
        };

        let request_fut = settings_requests.select_next_some();
        pin_mut!(request_fut);

        match exec.run_until_stalled(&mut request_fut) {
            Poll::Ready(Ok(settings::AudioRequest::Set { settings, responder })) => {
                assert_eq!(1, settings.streams.expect("a stream was set").len());
                let _ = responder.send(&mut Ok(()))?;
            }
            x => panic!("Expected Ready audio set request and got: {:?}", x),
        };

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // When a new volume happens as a result, it's returned.
        respond_to_audio_watch(watch_responder, NEW_MEDIA_VOLUME);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        match exec.run_until_stalled(&mut volume_set_fut) {
            Poll::Ready(Ok(vol)) => assert_eq!(vol, NEW_AVRCP_VOLUME),
            x => panic!("Expected set_volume to be responded to but got: {:?}", x),
        };

        let _watch_responder = expect_audio_watch(&mut exec, &mut settings_requests);

        // We get another command, but this time, it didn't produce a new volume result (because it
        // didn't change the volume)

        let volume_set_fut = volume_client.set_volume(0);
        pin_mut!(volume_set_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        match exec.run_until_stalled(&mut volume_set_fut) {
            Poll::Pending => {}
            x => panic!("Expected request to be unfinished, but got {:?}", x),
        };

        let request_fut = settings_requests.select_next_some();
        pin_mut!(request_fut);

        match exec.run_until_stalled(&mut request_fut) {
            Poll::Ready(Ok(settings::AudioRequest::Set { responder, .. })) => {
                let _ = responder.send(&mut Ok(()))?;
            }
            x => panic!("Expected Ready audio set request and got: {:?}", x),
        };

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // The maximum time we will wait for a new volume is 100 milliseconds.
        exec.set_fake_time(101.millis().after_now());
        exec.wake_expired_timers();

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Because no change was sent from Media, the last value from media is sent
        match exec.run_until_stalled(&mut volume_set_fut) {
            Poll::Ready(Ok(vol)) => assert_eq!(vol, NEW_AVRCP_VOLUME),
            x => panic!("Expected set_volume to be responded to but got: {:?}", x),
        };

        Ok(())
    }

    /// Test that the relay returns the current volume when requested, and completes an
    /// on_volume_changed request when the volume changes locally.
    #[test]
    fn test_volume_changes() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");
        let (mut settings_requests, avrcp_requests, _stop_sender, relay_fut) =
            setup_volume_relay()?;

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        let (volume_client, watch_responder) =
            finish_relay_setup(&mut relay_fut, &mut exec, avrcp_requests, &mut settings_requests);

        let volume_get_fut = volume_client.get_current_volume();
        pin_mut!(volume_get_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Volume get should return immediately with the initial volume (0.8 -> 100)
        match exec.run_until_stalled(&mut volume_get_fut) {
            Poll::Ready(Ok(vol)) => {
                assert_eq!(INITIAL_AVRCP_VOLUME, vol);
            }
            x => panic!("Expected get_current_volume to be finished, but got {:?}", x),
        };

        let volume_hanging_fut = volume_client.on_volume_changed();
        pin_mut!(volume_hanging_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // The OnVolumeChanged request should return immediately the first time.
        match exec.run_until_stalled(&mut volume_hanging_fut) {
            Poll::Ready(Ok(vol)) => {
                assert_eq!(INITIAL_AVRCP_VOLUME, vol);
            }
            x => {
                panic!("Expected on_volume_changed to be finished the first time, but got {:?}", x)
            }
        };

        let volume_hanging_fut = volume_client.on_volume_changed();
        pin_mut!(volume_hanging_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // The next OnVolumeChanged request shouldn't resolve because the volume hasn't changed.
        match exec.run_until_stalled(&mut volume_hanging_fut) {
            Poll::Pending => {}
            x => {
                panic!("Expected on_volume_changed to be hanging the second time, but got {:?}", x)
            }
        };

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // When a new volume happens as a result, it's returned.
        respond_to_audio_watch(watch_responder, NEW_MEDIA_VOLUME);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        match exec.run_until_stalled(&mut volume_hanging_fut) {
            Poll::Ready(Ok(vol)) => assert_eq!(vol, NEW_AVRCP_VOLUME),
            x => panic!(
                "Expected on_volume_changed to be responded to after change but got: {:?}",
                x
            ),
        };

        let _watch_responder = expect_audio_watch(&mut exec, &mut settings_requests);

        Ok(())
    }

    /// Tests the behavior of the VolumeRelay when multiple requests for OnVolumeChanged
    /// updates are requested.
    // TODO(54002): This test should be updated to reflect the fact that the channel gets closed
    // when OnVolumeChanged is called twice without a response.
    #[test]
    fn test_volume_changes_multiple_requests() -> Result<(), Error> {
        let mut exec = fasync::Executor::new().expect("executor needed");
        let (mut settings_requests, avrcp_requests, _stop_sender, relay_fut) =
            setup_volume_relay()?;

        pin_mut!(relay_fut);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Setup the relay and make two copies of the `volume_client`.
        let (volume_client, watch_responder) =
            finish_relay_setup(&mut relay_fut, &mut exec, avrcp_requests, &mut settings_requests);

        let volume_hanging_fut1 = volume_client.on_volume_changed();
        pin_mut!(volume_hanging_fut1);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // The OnVolumeChanged request should return immediately the first time.
        match exec.run_until_stalled(&mut volume_hanging_fut1) {
            Poll::Ready(Ok(vol)) => {
                assert_eq!(INITIAL_AVRCP_VOLUME, vol);
            }
            x => {
                panic!("Expected on_volume_changed to be finished the first time, but got {:?}", x)
            }
        };

        // Make another OnVolumeChanged request.
        let volume_hanging_fut2 = volume_client.on_volume_changed();
        pin_mut!(volume_hanging_fut2);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // The next OnVolumeChanged request shouldn't resolve because the volume hasn't changed.
        match exec.run_until_stalled(&mut volume_hanging_fut2) {
            Poll::Pending => {}
            x => {
                panic!("Expected on_volume_changed to be hanging the second time, but got {:?}", x)
            }
        };

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Another request for volume updates.
        let volume_hanging_fut3 = volume_client.on_volume_changed();
        pin_mut!(volume_hanging_fut3);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Respond with a new volume.
        respond_to_audio_watch(watch_responder, NEW_MEDIA_VOLUME);

        let res = exec.run_until_stalled(&mut relay_fut);
        assert!(res.is_pending());

        // Both volume update futures should receive the updated avrcp volume.
        match exec.run_until_stalled(&mut volume_hanging_fut2) {
            Poll::Ready(Ok(vol)) => assert_eq!(vol, NEW_AVRCP_VOLUME),
            x => panic!(
                "Expected on_volume_changed to be responded to after change but got: {:?}",
                x
            ),
        };
        match exec.run_until_stalled(&mut volume_hanging_fut3) {
            Poll::Ready(Ok(vol)) => assert_eq!(vol, NEW_AVRCP_VOLUME),
            x => panic!(
                "Expected on_volume_changed to be responded to after change but got: {:?}",
                x
            ),
        };

        let _watch_responder = expect_audio_watch(&mut exec, &mut settings_requests);

        Ok(())
    }
}
