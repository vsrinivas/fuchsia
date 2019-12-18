// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp},
    fuchsia_syslog::fx_log_warn,
};

pub mod bounded_queue;

use crate::media::media_types::Notification;

/// An upper bound for the maximum number of responders that can be stored for an event ID.
/// This number was chosen because there can be at most 7 connected devices, and
/// each device can subscribe to the maximum number of notifications, which is
/// currently 7.
pub(crate) const MAX_NOTIFICATION_EVENT_QUEUE_SIZE: usize = 64;

/// The data stored for an outstanding notification.
#[derive(Debug)]
pub(crate) struct NotificationData {
    /// The current value of the notification when the client subscribed.
    current_value: Notification,
    /// The position change interval of the notification, for `TRACK_POS_CHANGED`.
    pos_change_interval: u32,
    /// The FIDL responder to send the reply when the notification value changes.
    responder: Option<fidl_avrcp::TargetHandlerWatchNotificationResponder>,
}

impl NotificationData {
    pub fn new(
        current_value: Notification,
        pos_change_interval: u32,
        responder: fidl_avrcp::TargetHandlerWatchNotificationResponder,
    ) -> Self {
        Self { current_value: current_value, pos_change_interval, responder: Some(responder) }
    }

    fn send(
        &mut self,
        value: Result<Notification, fidl_avrcp::TargetAvcError>,
    ) -> Result<(), fidl::Error> {
        if let Some(responder) = self.responder.take() {
            responder.send(&mut value.map(|v| v.into()))
        } else {
            Err(fidl::Error::NotNullable)
        }
    }

    /// Send the `updated_val` over the responder.
    /// If the updated_value exists, and has changed, send over the responder.
    /// If the updated_value exists, but hasn't changed, return Self.
    /// If the updated_value is an error, send the error over the responder.
    pub fn update_responder(
        mut self,
        event_id: &fidl_avrcp::NotificationEvent,
        updated_val: Result<Notification, fidl_avrcp::TargetAvcError>,
    ) -> Result<Option<Self>, Error> {
        let response = if let Ok(val) = updated_val {
            // If an invalid `event_id` is provided, send RejectedInvalidParameter as per AVRCP 1.6.
            match self.notification_value_changed(event_id, &val) {
                Ok(true) => Ok(val),
                Ok(false) => return Ok(Some(self)),
                Err(_) => Err(fidl_avrcp::TargetAvcError::RejectedInvalidParameter),
            }
        } else {
            updated_val
        };

        self.send(response).map(|_| None).map_err(|_| format_err!("Responder send error"))
    }

    /// Compares the initial value, `self.current_value` to the given new
    /// notification value, `current` for an event specified by `event_id`.
    /// Returns true if the value for `event_id` has changed.
    /// If an unsupported `event_id` is provided, an error will be returned.
    fn notification_value_changed(
        &self,
        event_id: &fidl_avrcp::NotificationEvent,
        new_value: &Notification,
    ) -> Result<bool, Error> {
        match event_id {
            fidl_avrcp::NotificationEvent::PlaybackStatusChanged => {
                Ok(self.current_value.status != new_value.status)
            }
            fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged => {
                Ok(self.current_value.application_settings != new_value.application_settings)
            }
            fidl_avrcp::NotificationEvent::TrackChanged => {
                Ok(self.current_value.track_id != new_value.track_id)
            }
            fidl_avrcp::NotificationEvent::TrackPosChanged => {
                Ok(self.current_value.pos != new_value.pos)
            }
            _ => {
                fx_log_warn!(
                    "Received notification request for unsupported notification event_id {:?}",
                    event_id
                );
                Err(format_err!("Invalid event_id provided"))
            }
        }
    }
}

/// `NotificationData` is no longer in use. Send the current_value
/// back over the responder before dropping.
impl Drop for NotificationData {
    fn drop(&mut self) {
        let curr_value = self.current_value.clone();
        if let Err(e) = self.send(Ok(curr_value)) {
            fx_log_warn!("Error in dropping the responder: {}", e);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::media::media_types::ValidPlayerApplicationSettings;

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{
        self as fidl_avrcp, TargetHandlerMarker, TargetHandlerRequest, TargetHandlerRequestStream,
        TargetHandlerWatchNotificationResponder,
    };
    use fuchsia_async as fasync;
    use futures::stream::TryStreamExt;

    async fn handle_watch_notifications<F>(mut stream: TargetHandlerRequestStream, test_fn: F)
    where
        F: Fn(TargetHandlerWatchNotificationResponder) -> (),
    {
        while let Some(request) = stream.try_next().await.expect("Should work") {
            if let TargetHandlerRequest::WatchNotification { responder, .. } = request {
                // Exercise the test when we get the request.
                test_fn(responder);
                return;
            }
        }
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests the comparison of `Notification` values works as intended.
    async fn test_notification_value_changed() {
        let (proxy, stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        // Set up the listener that runs the test when the request has been received.
        fasync::spawn(handle_watch_notifications(stream, |responder| {
            let mut prev_value: Notification = Default::default();
            prev_value.status = Some(fidl_avrcp::PlaybackStatus::Stopped);
            prev_value.track_id = Some(999);
            prev_value.pos = Some(12345);
            prev_value.application_settings = Some(ValidPlayerApplicationSettings::new(
                None,
                Some(fidl_avrcp::RepeatStatusMode::GroupRepeat),
                None,
                Some(fidl_avrcp::ScanMode::Off),
            ));
            let data = NotificationData::new(prev_value, 0, responder);

            let mut curr_value: Notification = Default::default();
            curr_value.status = Some(fidl_avrcp::PlaybackStatus::Playing);
            curr_value.track_id = Some(800);
            curr_value.pos = Some(12345);
            curr_value.application_settings = Some(ValidPlayerApplicationSettings::new(
                None,
                Some(fidl_avrcp::RepeatStatusMode::GroupRepeat),
                Some(fidl_avrcp::ShuffleMode::Off),
                Some(fidl_avrcp::ScanMode::Off),
            ));

            let res1 = data.notification_value_changed(
                &fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
                &curr_value,
            );
            assert_eq!(res1.unwrap(), true);

            let res2 = data.notification_value_changed(
                &fidl_avrcp::NotificationEvent::TrackChanged,
                &curr_value,
            );
            assert_eq!(res2.unwrap(), true);

            let res3 = data.notification_value_changed(
                &fidl_avrcp::NotificationEvent::PlayerApplicationSettingChanged,
                &curr_value,
            );
            assert_eq!(res3.unwrap(), true);

            let res4 = data.notification_value_changed(
                &fidl_avrcp::NotificationEvent::TrackPosChanged,
                &curr_value,
            );
            assert_eq!(res4.unwrap(), false);

            let res5 = data.notification_value_changed(
                &fidl_avrcp::NotificationEvent::SystemStatusChanged,
                &curr_value,
            );
            assert!(res5.is_err());
        }));
        let notif: Notification = Default::default();
        let _ = proxy
            .watch_notification(fidl_avrcp::NotificationEvent::TrackChanged, notif.into(), 0)
            .await;

        // TODO(aniramakri): NotificationData is getting dropped, and attempting to send.
    }

    #[test]
    /// Tests sending response over responder successfully consumes and replaces inner responder.
    // TODO(42623): Write test for this. Need to mock the channel.
    fn test_update_responder_success() {}

    #[test]
    /// Tests sending response over responder successfully consumes and replaces inner responder.
    // TODO(42623): Write test for this. Need to mock the channel.
    fn test_update_responder_error() {}
}
