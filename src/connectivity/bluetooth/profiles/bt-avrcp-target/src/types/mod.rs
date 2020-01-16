// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
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
    use crate::tests::generate_empty_watch_notification;

    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_avrcp::{self as fidl_avrcp, TargetHandlerMarker};
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests the comparison of `Notification` values works as intended.
    async fn test_notification_value_changed() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;
        {
            let mut prev_value = Notification::default();
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

            let mut curr_value = Notification::default();
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
        }

        assert!(result_fut.await.is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests sending response with a changed value successfully sends over the responder.
    async fn test_update_responder_changed_value_success() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            // Create the data with responder.
            let curr_value = Notification::default();
            let data = NotificationData::new(curr_value, 0, responder);

            // Send an update over the responder with a changed value. Should succeed.
            let changed_value = Notification {
                status: Some(fidl_avrcp::PlaybackStatus::Paused),
                ..Notification::default()
            };
            let result = data
                .update_responder(
                    &fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
                    Ok(changed_value),
                )
                .expect("Updating the responder should succeed");
            assert!(result.is_none());
        }

        // The response should be the changed_value.
        let expected = Notification {
            status: Some(fidl_avrcp::PlaybackStatus::Paused),
            ..Notification::default()
        };
        assert_eq!(
            Ok(expected),
            result_fut.await.expect("FIDL call should work").map(|v| v.into())
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests sending response with an unchanged value does not send/consume the responder.
    /// Instead, it should return itself, with the unchanged responder.
    async fn test_update_responder_same_value_success() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            // Create the data with responder.
            let curr_value = Notification::default();
            let data = NotificationData::new(curr_value, 0, responder);

            // Send an update over the responder with the same value.
            // Should return Some(Self).
            let same_value = Notification::default();
            let result = data
                .update_responder(
                    &fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
                    Ok(same_value),
                )
                .expect("Should work");
            assert!(result.is_some());
        }

        // The response should be the original (unchanged) value.
        // This is because the reply over the responder is never sent and is instead
        // dropped at the end of the closure. Drop is impl'd for NotificationData, which
        // sends the current value.
        let expected = Notification::default();
        assert_eq!(
            Ok(expected),
            result_fut.await.expect("FIDL call should work").map(|v| v.into())
        );
        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    /// Tests sending an error response sends & consumes the responder.
    async fn test_update_responder_with_error() -> Result<(), Error> {
        let (mut proxy, mut stream) = create_proxy_and_stream::<TargetHandlerMarker>()
            .expect("Couldn't create proxy and stream");

        let (result_fut, responder) =
            generate_empty_watch_notification(&mut proxy, &mut stream).await?;

        {
            // Create the data with responder.
            let curr_value = Notification::default();
            let data = NotificationData::new(curr_value, 0, responder);

            // Send an update over the responder with an error value.
            // Should successfully send over the responder, and consume it.
            let result = data
                .update_responder(
                    &fidl_avrcp::NotificationEvent::PlaybackStatusChanged,
                    Err(fidl_avrcp::TargetAvcError::RejectedAddressedPlayerChanged),
                )
                .expect("Error should successfully send over responder");
            assert!(result.is_none());
        }

        // The response should be the error.
        let expected: Result<fidl_avrcp::Notification, String> =
            Err("RejectedAddressedPlayerChanged".to_string());
        assert_eq!(
            expected,
            result_fut.await.expect("FIDL call should work").map_err(|e| format!("{:?}", e))
        );
        Ok(())
    }
}
