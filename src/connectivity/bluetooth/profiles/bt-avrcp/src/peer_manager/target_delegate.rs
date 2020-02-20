// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_bluetooth_avrcp::{
    AbsoluteVolumeHandlerProxy, NotificationEvent, PlayStatus, TargetAvcError, TargetHandlerProxy,
    TargetPassthroughError,
};

/// Delegates commands received on any peer channels to the currently registered target handler and
/// absolute volume handler.
/// If no target handler or absolute volume handler is registered with the service, this delegate
/// will return appropriate stub responses.
/// If a target handler is changed or closed at any point, this delegate will handle the state
/// transitions for any outstanding and pending registered notifications.
#[derive(Debug)]
pub struct TargetDelegate {
    inner: Arc<Mutex<TargetDelegateInner>>,
}

#[derive(Debug)]
struct TargetDelegateInner {
    target_handler: Option<TargetHandlerProxy>,
    absolute_volume_handler: Option<AbsoluteVolumeHandlerProxy>,
}

impl TargetDelegate {
    pub fn new() -> TargetDelegate {
        TargetDelegate {
            inner: Arc::new(Mutex::new(TargetDelegateInner {
                target_handler: None,
                absolute_volume_handler: None,
            })),
        }
    }

    /// Sets the target delegate. Returns true if successful. Resets any pending registered
    /// notifications.
    /// If the target is already set to some value this method does not replace it and returns false
    pub fn set_target_handler(&self, target_handler: TargetHandlerProxy) -> Result<(), Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.target_handler.is_some() {
            return Err(Error::TargetBound);
        }

        let target_handler_event_stream = target_handler.take_event_stream();
        // We were able to set the target delegate so spawn a task to watch for it
        // to close.
        let inner_ref = self.inner.clone();
        fasync::spawn(async move {
            let _ = target_handler_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().target_handler = None;
        });

        inner_guard.target_handler = Some(target_handler);
        Ok(())
    }

    pub fn set_absolute_volume_handler(
        &self,
        absolute_volume_handler: AbsoluteVolumeHandlerProxy,
    ) -> Result<(), Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.absolute_volume_handler.is_some() {
            return Err(Error::TargetBound);
        }

        let volume_event_stream = absolute_volume_handler.take_event_stream();
        // We were able to set the target delegate so spawn a task to watch for it
        // to close.
        let inner_ref = self.inner.clone();
        fasync::spawn(async move {
            let _ = volume_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().absolute_volume_handler = None;
        });

        inner_guard.absolute_volume_handler = Some(absolute_volume_handler);
        Ok(())
    }

    /// Send a passthrough panel command
    pub async fn send_passthrough_command(
        &self,
        command: AvcPanelCommand,
        pressed: bool,
    ) -> Result<(), TargetPassthroughError> {
        let cmd_fut = {
            let inner_guard = self.inner.lock();
            match &inner_guard.target_handler {
                Some(target_handler) => target_handler.send_command(command, pressed),
                // We have don't have a target handler, reject the passthrough command
                // Ideally we would return NotImplemented but we don't know if the remote will cache
                // that state.
                None => return Err(TargetPassthroughError::CommandRejected),
            }
        };

        // if we have a FIDL error, reject the passthrough command
        cmd_fut.await.map_err(|_| TargetPassthroughError::CommandRejected)?
    }

    /// Get the support events from the target handler or a default set of events supported if no
    /// target handler is set.
    /// This function must return a result and not an error as this call might happen before we have
    /// a target handler set.
    pub async fn get_supported_events(&self) -> Vec<NotificationEvent> {
        // Spec requires that we reply with at least two notification types.
        const DEFAULT_EVENTS: &'static [NotificationEvent] =
            &[NotificationEvent::VolumeChanged, NotificationEvent::AddressedPlayerChanged];

        let cmd_fut = {
            let inner_guard = self.inner.lock();
            match &inner_guard.target_handler {
                Some(target_handler) => target_handler.get_events_supported(),
                // we have don't have a target handler, return default set of events.
                None => return DEFAULT_EVENTS.to_vec(),
            }
        };

        if let Ok(Ok(value)) = cmd_fut.await {
            value
        } else {
            // we swallow both FIDL errors and errors from the target handler and return a default
            // set of notifications we support.
            // TODO: audit this behavior is ideal
            DEFAULT_EVENTS.to_vec()
        }
    }

    pub async fn send_get_play_status_command(&self) -> Result<PlayStatus, TargetAvcError> {
        let send_command_fut = {
            let inner_guard = self.inner.lock();
            match &inner_guard.target_handler {
                Some(target_handler) => target_handler.get_play_status(),
                // we have don't have a target handler, return no players available
                None => return Err(TargetAvcError::RejectedNoAvailablePlayers),
            }
        };

        // if we have a FIDL error, return no players available
        send_command_fut.await.map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{TargetHandlerMarker, TargetHandlerRequest};
    use matches::assert_matches;
    use std::task::Poll;

    // This also gets tested at a service level. Test that we get a TargetBound error on double set.
    // Test that we can set again after the target handler has closed.
    #[test]
    fn set_target_test() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let (target_proxy_1, target_stream_1) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        let (target_proxy_2, target_stream_2) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");

        let target_delegate = TargetDelegate::new();
        assert_matches!(target_delegate.set_target_handler(target_proxy_1), Ok(()));
        assert_matches!(
            target_delegate.set_target_handler(target_proxy_2),
            Err(Error::TargetBound)
        );
        drop(target_stream_1);
        drop(target_stream_2);

        // pump the new task that got spawned. it should unset the target handler now that the stream
        // is closed. using a no-op future just spin the other global executor tasks.
        let no_op = async {};
        pin_utils::pin_mut!(no_op);
        assert!(exec.run_until_stalled(&mut no_op).is_ready());

        let (target_proxy_3, target_stream_3) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy_3), Ok(()));
        drop(target_stream_3);
    }

    // test we get the default response before a target handler is set and test we get the response
    // from a target handler that we expected.
    #[test]
    fn target_handler_response() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");

        let target_delegate = TargetDelegate::new();
        {
            // try without a target handler
            let get_supported_events_fut = target_delegate.get_supported_events();
            pin_utils::pin_mut!(get_supported_events_fut);
            match exec.run_until_stalled(&mut get_supported_events_fut) {
                Poll::Ready(events) => {
                    assert_eq!(
                        events,
                        vec![
                            NotificationEvent::VolumeChanged,
                            NotificationEvent::AddressedPlayerChanged
                        ]
                    );
                }
                _ => assert!(false, "wrong default value"),
            };
        }

        {
            // try with a target handler
            let (target_proxy, mut target_stream) =
                create_proxy_and_stream::<TargetHandlerMarker>()
                    .expect("Error creating TargetHandler endpoint");
            assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

            let get_supported_events_fut = target_delegate.get_supported_events();
            pin_utils::pin_mut!(get_supported_events_fut);
            assert!(exec.run_until_stalled(&mut get_supported_events_fut).is_pending());

            let select_next_some_fut = target_stream.select_next_some();
            pin_utils::pin_mut!(select_next_some_fut);
            match exec.run_until_stalled(&mut select_next_some_fut) {
                Poll::Ready(Ok(TargetHandlerRequest::GetEventsSupported { responder })) => {
                    assert!(responder
                        .send(&mut Ok(vec![
                            NotificationEvent::VolumeChanged,
                            NotificationEvent::TrackPosChanged,
                        ]))
                        .is_ok());
                }
                _ => assert!(false, "unexpected stream state"),
            };

            match exec.run_until_stalled(&mut get_supported_events_fut) {
                Poll::Ready(events) => {
                    assert_eq!(
                        events,
                        vec![NotificationEvent::VolumeChanged, NotificationEvent::TrackPosChanged]
                    );
                }
                _ => assert!(false, "unexpected state"),
            }
        }
    }
}
