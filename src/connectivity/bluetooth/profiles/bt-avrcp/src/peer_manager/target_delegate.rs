// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl::encoding::Decodable;
use fidl_fuchsia_bluetooth_avrcp::{
    self as fidl_avrcp, AbsoluteVolumeHandlerProxy, MediaAttributes, Notification,
    NotificationEvent, PlayStatus, TargetAvcError, TargetHandlerProxy, TargetPassthroughError,
};

use futures::TryFutureExt;

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

    // Retrieves a clone of the current volume handler, if there is one, otherwise returns None.
    fn absolute_volume_handler(&self) -> Option<AbsoluteVolumeHandlerProxy> {
        let guard = self.inner.lock();
        guard.absolute_volume_handler.clone()
    }

    // Retrieves a clone of the current target handler, if there is one, otherwise returns None.
    fn target_handler(&self) -> Option<TargetHandlerProxy> {
        let guard = self.inner.lock();
        guard.target_handler.clone()
    }

    /// Sets the target delegate. Resets any pending registered notifications.
    /// If the delegate is already set, reutrns an Error.
    pub fn set_target_handler(&self, target_handler: TargetHandlerProxy) -> Result<(), Error> {
        let mut inner_guard = self.inner.lock();
        if inner_guard.target_handler.is_some() {
            return Err(Error::TargetBound);
        }

        let target_handler_event_stream = target_handler.take_event_stream();
        // We were able to set the target delegate so spawn a task to watch for it
        // to close.
        let inner_ref = self.inner.clone();
        fasync::Task::spawn(async move {
            let _ = target_handler_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().target_handler = None;
        })
        .detach();

        inner_guard.target_handler = Some(target_handler);
        Ok(())
    }

    /// Sets the absolute volume handler delegate. Returns an error if one is currently active.
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
        fasync::Task::spawn(async move {
            let _ = volume_event_stream.map(|_| ()).collect::<()>().await;
            inner_ref.lock().absolute_volume_handler = None;
        })
        .detach();

        inner_guard.absolute_volume_handler = Some(absolute_volume_handler);
        Ok(())
    }

    /// Send a passthrough panel command
    pub async fn send_passthrough_command(
        &self,
        command: AvcPanelCommand,
        pressed: bool,
    ) -> Result<(), TargetPassthroughError> {
        let target_handler =
            self.target_handler().ok_or(TargetPassthroughError::CommandRejected)?;

        // if we have a FIDL error, reject the passthrough command
        target_handler
            .send_command(command, pressed)
            .await
            .map_err(|_| TargetPassthroughError::CommandRejected)?
    }

    /// Get the supported events from the target handler or a default set of events if no
    /// target handler is set.
    /// This function always returns a result and not an error as this call may happen before a target handler is set.
    pub async fn get_supported_events(&self) -> Vec<NotificationEvent> {
        // Spec requires that we reply with at least two notification types.
        // Reply we support volume change always and if there is no absolute volume handler, we
        // respond with an error of no players available.
        // Also respond we have address player change so we can synthesize that event.
        let mut default_events =
            vec![NotificationEvent::VolumeChanged, NotificationEvent::AddressedPlayerChanged];

        let cmd_fut = match self.target_handler() {
            None => return default_events,
            Some(x) => x.get_events_supported(),
        };

        if let Ok(Ok(mut events)) = cmd_fut.await {
            // We always also support volume change and addressed player changed.
            events.append(&mut default_events);
            events.sort_unstable();
            events.dedup();
            events
        } else {
            // Ignore FIDL errors and errors from the target handler and return the default set of notifications.
            default_events
        }
    }

    pub async fn send_get_play_status_command(&self) -> Result<PlayStatus, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;

        // if we have a FIDL error, return no players available
        target_handler
            .get_play_status()
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    /// Send a set absolute volume command to the absolute volume handler.
    pub async fn send_set_absolute_volume_command(&self, volume: u8) -> Result<u8, TargetAvcError> {
        let abs_vol_handler =
            self.absolute_volume_handler().ok_or(TargetAvcError::RejectedInvalidParameter)?;
        abs_vol_handler
            .set_volume(volume)
            .map_err(|_| TargetAvcError::RejectedInvalidParameter)
            .await
    }

    /// Get current value of the notification
    pub async fn send_get_notification(
        &self,
        event: NotificationEvent,
    ) -> Result<Notification, TargetAvcError> {
        if event == NotificationEvent::VolumeChanged {
            let abs_vol_handler =
                self.absolute_volume_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
            // if we have a FIDL error return no players
            let volume = abs_vol_handler
                .get_current_volume()
                .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)
                .await?;

            return Ok(Notification { volume: Some(volume), ..Notification::new_empty() });
        }
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        // if we have a FIDL error, return no players available
        target_handler
            .get_notification(event)
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    /// Watch for the change of the notification value
    // TODO(fxbug.dev/54002): Instead of cloning the AbsoluteVolumeHandlerProxy and then
    // sending a new `on_volume_changed()` hanging-get request, AVRCP should
    // monitor any outstanding request, and subscribe each peer to the result
    // of the single outstanding request.
    pub async fn send_watch_notification(
        &self,
        event: NotificationEvent,
        current_value: Notification,
        pos_change_interval: u32,
    ) -> Result<Notification, TargetAvcError> {
        if event == NotificationEvent::VolumeChanged {
            let abs_vol_handler = self
                .absolute_volume_handler()
                .ok_or(TargetAvcError::RejectedAddressedPlayerChanged)?;
            let volume = abs_vol_handler
                .on_volume_changed()
                .map_err(|_| TargetAvcError::RejectedAddressedPlayerChanged)
                .await?;

            return Ok(Notification { volume: Some(volume), ..Notification::new_empty() });
        }
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedAddressedPlayerChanged)?;
        // if we have a FIDL error, send that the players changed
        target_handler
            .watch_notification(event, current_value, pos_change_interval)
            .await
            .map_err(|_| TargetAvcError::RejectedAddressedPlayerChanged)?
    }

    pub async fn send_get_media_attributes_command(
        &self,
    ) -> Result<MediaAttributes, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        target_handler
            .get_media_attributes()
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    pub async fn send_list_player_application_setting_attributes_command(
        &self,
    ) -> Result<Vec<fidl_avrcp::PlayerApplicationSettingAttributeId>, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        target_handler
            .list_player_application_setting_attributes()
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    pub async fn send_get_player_application_settings_command(
        &self,
        attributes: Vec<fidl_avrcp::PlayerApplicationSettingAttributeId>,
    ) -> Result<fidl_avrcp::PlayerApplicationSettings, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        let send_command_fut =
            target_handler.get_player_application_settings(&mut attributes.into_iter());
        send_command_fut.await.map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    pub async fn send_set_player_application_settings_command(
        &self,
        requested_settings: fidl_avrcp::PlayerApplicationSettings,
    ) -> Result<fidl_avrcp::PlayerApplicationSettings, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        target_handler
            .set_player_application_settings(requested_settings)
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    pub async fn send_get_media_player_items_command(
        &self,
    ) -> Result<Vec<fidl_avrcp::MediaPlayerItem>, TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        target_handler
            .get_media_player_items()
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }

    pub async fn send_set_addressed_player_command(
        &self,
        mut player_id: fidl_avrcp::AddressedPlayerId,
    ) -> Result<(), TargetAvcError> {
        let target_handler =
            self.target_handler().ok_or(TargetAvcError::RejectedNoAvailablePlayers)?;
        target_handler
            .set_addressed_player(&mut player_id)
            .await
            .map_err(|_| TargetAvcError::RejectedNoAvailablePlayers)?
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::encoding::Decodable as FidlDecodable;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{
        Equalizer, PlayerApplicationSettings, TargetHandlerMarker, TargetHandlerRequest,
    };
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

    #[test]
    // Test getting correct response from a get_media_attributes command.
    fn test_get_media_attributes() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let target_delegate = TargetDelegate::new();

        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

        let get_media_attr_fut = target_delegate.send_get_media_attributes_command();
        pin_utils::pin_mut!(get_media_attr_fut);
        assert!(exec.run_until_stalled(&mut get_media_attr_fut).is_pending());

        let select_next_some_fut = target_stream.select_next_some();
        pin_utils::pin_mut!(select_next_some_fut);
        match exec.run_until_stalled(&mut select_next_some_fut) {
            Poll::Ready(Ok(TargetHandlerRequest::GetMediaAttributes { responder })) => {
                assert!(responder.send(&mut Ok(MediaAttributes::new_empty())).is_ok());
            }
            _ => assert!(false, "unexpected stream state"),
        };

        match exec.run_until_stalled(&mut get_media_attr_fut) {
            Poll::Ready(attributes) => {
                assert_eq!(attributes, Ok(MediaAttributes::new_empty()));
            }
            _ => assert!(false, "unexpected state"),
        }
    }

    #[test]
    // Test getting correct response from a list_player_application_settings command.
    fn test_list_player_application_settings() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let target_delegate = TargetDelegate::new();

        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

        let list_pas_fut =
            target_delegate.send_list_player_application_setting_attributes_command();
        pin_utils::pin_mut!(list_pas_fut);
        assert!(exec.run_until_stalled(&mut list_pas_fut).is_pending());

        let select_next_some_fut = target_stream.select_next_some();
        pin_utils::pin_mut!(select_next_some_fut);
        match exec.run_until_stalled(&mut select_next_some_fut) {
            Poll::Ready(Ok(TargetHandlerRequest::ListPlayerApplicationSettingAttributes {
                responder,
            })) => {
                assert!(responder.send(&mut Ok(vec![])).is_ok());
            }
            _ => assert!(false, "unexpected stream state"),
        };

        match exec.run_until_stalled(&mut list_pas_fut) {
            Poll::Ready(attributes) => {
                assert_eq!(attributes, Ok(vec![]));
            }
            _ => assert!(false, "unexpected state"),
        }
    }

    #[test]
    // Test getting correct response from a get_player_application_settings command.
    fn test_get_player_application_settings() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let target_delegate = TargetDelegate::new();

        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

        let attributes = vec![fidl_avrcp::PlayerApplicationSettingAttributeId::ShuffleMode];
        let get_pas_fut = target_delegate.send_get_player_application_settings_command(attributes);
        pin_utils::pin_mut!(get_pas_fut);
        assert!(exec.run_until_stalled(&mut get_pas_fut).is_pending());

        let select_next_some_fut = target_stream.select_next_some();
        pin_utils::pin_mut!(select_next_some_fut);
        match exec.run_until_stalled(&mut select_next_some_fut) {
            Poll::Ready(Ok(TargetHandlerRequest::GetPlayerApplicationSettings {
                responder,
                ..
            })) => {
                assert!(responder
                    .send(&mut Ok(fidl_avrcp::PlayerApplicationSettings {
                        shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                        ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                    }))
                    .is_ok());
            }
            _ => assert!(false, "unexpected stream state"),
        };

        match exec.run_until_stalled(&mut get_pas_fut) {
            Poll::Ready(attributes) => {
                assert_eq!(
                    attributes,
                    Ok(fidl_avrcp::PlayerApplicationSettings {
                        shuffle_mode: Some(fidl_avrcp::ShuffleMode::Off),
                        ..fidl_avrcp::PlayerApplicationSettings::new_empty()
                    })
                );
            }
            _ => assert!(false, "unexpected state"),
        }
    }

    #[test]
    // Test getting correct response from a get_player_application_settings command.
    fn test_set_player_application_settings() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let target_delegate = TargetDelegate::new();

        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

        // Current media doesn't support Equalizer.
        let attributes = PlayerApplicationSettings {
            equalizer: Some(Equalizer::Off),
            ..PlayerApplicationSettings::new_empty()
        };
        let set_pas_fut = target_delegate.send_set_player_application_settings_command(attributes);
        pin_utils::pin_mut!(set_pas_fut);
        assert!(exec.run_until_stalled(&mut set_pas_fut).is_pending());

        let select_next_some_fut = target_stream.select_next_some();
        pin_utils::pin_mut!(select_next_some_fut);
        match exec.run_until_stalled(&mut select_next_some_fut) {
            Poll::Ready(Ok(TargetHandlerRequest::SetPlayerApplicationSettings {
                responder,
                ..
            })) => {
                assert!(responder
                    .send(&mut Ok(fidl_avrcp::PlayerApplicationSettings::new_empty()))
                    .is_ok());
            }
            _ => assert!(false, "unexpected stream state"),
        };

        // We expect the returned `set_settings` to be empty, since we requested an
        // unsupported application setting.
        match exec.run_until_stalled(&mut set_pas_fut) {
            Poll::Ready(attr) => {
                assert_eq!(attr, Ok(fidl_avrcp::PlayerApplicationSettings::new_empty()));
            }
            _ => assert!(false, "unexpected state"),
        }
    }

    #[test]
    // Test getting correct response from a get_media_player_items request.
    fn test_get_media_player_items() {
        let mut exec = fasync::Executor::new().expect("executor::new failed");
        let target_delegate = TargetDelegate::new();

        let (target_proxy, mut target_stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Error creating TargetHandler endpoint");
        assert_matches!(target_delegate.set_target_handler(target_proxy), Ok(()));

        let get_media_fut = target_delegate.send_get_media_player_items_command();
        pin_utils::pin_mut!(get_media_fut);
        assert!(exec.run_until_stalled(&mut get_media_fut).is_pending());

        let select_next_some_fut = target_stream.select_next_some();
        pin_utils::pin_mut!(select_next_some_fut);
        match exec.run_until_stalled(&mut select_next_some_fut) {
            Poll::Ready(Ok(TargetHandlerRequest::GetMediaPlayerItems { responder, .. })) => {
                assert!(responder
                    .send(&mut Ok(vec![fidl_avrcp::MediaPlayerItem {
                        player_id: Some(1),
                        ..fidl_avrcp::MediaPlayerItem::new_empty()
                    }]))
                    .is_ok());
            }
            _ => assert!(false, "unexpected stream state"),
        };

        match exec.run_until_stalled(&mut get_media_fut) {
            Poll::Ready(items) => {
                assert!(items.is_ok());
                let items = items.expect("Just checked");
                assert_eq!(items.len(), 1);
                assert_eq!(items[0].player_id, Some(1));
            }
            _ => assert!(false, "unexpected state"),
        }
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
                    assert!(events.contains(&NotificationEvent::VolumeChanged));
                    assert!(events.contains(&NotificationEvent::AddressedPlayerChanged));
                    assert!(events.contains(&NotificationEvent::TrackPosChanged));
                }
                _ => assert!(false, "unexpected state"),
            }
        }
    }
}
