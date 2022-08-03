// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_utils::hanging_get::server::{HangingGet, Publisher, Subscriber};
use fidl_fuchsia_bluetooth_power::{Identifier, Information, WatcherWatchResponder};
use fuchsia_bluetooth::types::PeerId;
use parking_lot::Mutex;
use std::{collections::HashMap, convert::TryFrom};
use tracing::warn;

use crate::error::Error;

type NotifyFn<Responder> = Box<dyn Fn(&Vec<Information>, Responder) -> bool + Send + Sync>;
type PeripheralHangingGet<Responder> = HangingGet<Vec<Information>, Responder, NotifyFn<Responder>>;
type PeripheralPublisher<Responder> = Publisher<Vec<Information>, Responder, NotifyFn<Responder>>;
pub type PeripheralSubscriber<Responder> =
    Subscriber<Vec<Information>, Responder, NotifyFn<Responder>>;

/// The current state of discovered/connected Bluetooth peripherals.
pub struct PeripheralState {
    /// Current state of the peripherals. Used to record new updates.
    inner: Mutex<PeripheralStateInner>,
    /// Hanging-get server that assigns subscriptions to FIDL clients that want to watch peripheral
    /// state.
    broker: Mutex<PeripheralHangingGet<WatcherWatchResponder>>,
    /// Hanging-get publisher used to send updates to all hanging-get listeners about a change in
    /// peripheral state.
    publisher: PeripheralPublisher<WatcherWatchResponder>,
}

impl PeripheralState {
    pub fn new() -> Self {
        let notify_fn: NotifyFn<WatcherWatchResponder> =
            Box::new(|peripherals: &Vec<Information>, responder: WatcherWatchResponder| {
                if let Err(e) = responder.send(&mut peripherals.into_iter().cloned()) {
                    warn!("Unable to respond to Peripheral Watcher hanging get: {:?}", e);
                }
                true
            });
        let watch_peripherals_broker = HangingGet::new(Vec::new(), notify_fn);
        let publisher = watch_peripherals_broker.new_publisher();
        let inner = PeripheralStateInner { peripherals: HashMap::new() };
        Self { inner: Mutex::new(inner), broker: Mutex::new(watch_peripherals_broker), publisher }
    }

    fn notify_peripheral_watchers(&self, info: Vec<Information>) {
        self.publisher.update(move |state| {
            if *state == info {
                false
            } else {
                *state = info;
                true
            }
        });
    }

    pub fn new_subscriber(&self) -> PeripheralSubscriber<WatcherWatchResponder> {
        self.broker.lock().new_subscriber()
    }

    pub fn record_power_update(&self, id: PeerId, battery: BatteryInfo) {
        let info = {
            let mut inner = self.inner.lock();
            inner.record_power_update(id, battery);
            inner.peripherals()
        };

        self.notify_peripheral_watchers(info);
    }

    #[cfg(test)]
    pub fn contains_entry(&self, id: &PeerId) -> bool {
        self.inner.lock().peripherals.contains_key(id)
    }
}

struct PeripheralStateInner {
    peripherals: HashMap<PeerId, PeripheralData>,
}

impl PeripheralStateInner {
    fn peripherals(&self) -> Vec<Information> {
        self.peripherals.values().map(Into::into).collect()
    }

    fn record_power_update(&mut self, id: PeerId, battery: BatteryInfo) {
        let entry = self.peripherals.entry(id).or_insert(PeripheralData::new(id));
        entry.battery = Some(battery);
    }
}

/// A snapshot of properties associated with a Bluetooth peripheral.
#[derive(Debug)]
struct PeripheralData {
    id: PeerId,
    /// Information about battery health & status.
    battery: Option<BatteryInfo>,
}

impl PeripheralData {
    fn new(id: PeerId) -> Self {
        Self { id, battery: None }
    }
}

impl From<&PeripheralData> for Information {
    fn from(src: &PeripheralData) -> Information {
        Information {
            identifier: Some(Identifier::PeerId(src.id.into())),
            battery_info: src.battery.as_ref().map(Into::into),
            ..Information::EMPTY
        }
    }
}

/// Battery information about a peripheral.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BatteryInfo {
    level_percent: f32,
    level_status: Option<fidl_fuchsia_power_battery::LevelStatus>,
}

impl TryFrom<fidl_fuchsia_power_battery::BatteryInfo> for BatteryInfo {
    type Error = Error;

    fn try_from(src: fidl_fuchsia_power_battery::BatteryInfo) -> Result<BatteryInfo, Self::Error> {
        // The `level_percent` must be specified per the `fidl_fuchsia_bluetooth_power` docs.
        let level_percent = src.level_percent.ok_or(Error::battery("missing level percent"))?;
        Ok(BatteryInfo { level_percent, level_status: src.level_status })
    }
}

impl From<&BatteryInfo> for fidl_fuchsia_power_battery::BatteryInfo {
    fn from(src: &BatteryInfo) -> fidl_fuchsia_power_battery::BatteryInfo {
        fidl_fuchsia_power_battery::BatteryInfo {
            level_percent: Some(src.level_percent),
            level_status: src.level_status,
            ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
        }
    }
}

/// Returns the Bluetooth PeerId from the `identifier`, or Error otherwise.
pub fn peer_id_from_identifier(identifier: &Identifier) -> Result<PeerId, Error> {
    match identifier {
        Identifier::PeerId(id) => Ok((*id).into()),
        id => Err(id.into()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth_power::{LocalDevice, WatcherMarker};
    use futures::StreamExt;
    use std::sync::Arc;

    #[test]
    fn invalid_identifier() {
        let invalid = Identifier::unknown(10, vec![]);
        assert_matches!(peer_id_from_identifier(&invalid), Err(Error::Identifier { .. }));

        let unsupported = Identifier::LocalDevice(LocalDevice);
        assert_matches!(peer_id_from_identifier(&unsupported), Err(Error::Identifier { .. }));
    }

    #[test]
    fn valid_identifier() {
        let id = Identifier::PeerId(PeerId(123).into());
        assert_matches!(peer_id_from_identifier(&id), Ok(_));
    }

    #[test]
    fn invalid_battery_info() {
        let empty = fidl_fuchsia_power_battery::BatteryInfo::EMPTY;
        let local = BatteryInfo::try_from(empty);
        assert_matches!(local, Err(Error::BatteryInfo { .. }));

        let missing_percent = fidl_fuchsia_power_battery::BatteryInfo {
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
            ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
        };
        let local = BatteryInfo::try_from(missing_percent);
        assert_matches!(local, Err(Error::BatteryInfo { .. }));
    }

    #[test]
    fn battery_info() {
        // Extra fields are Ok - ignored.
        let valid = fidl_fuchsia_power_battery::BatteryInfo {
            level_percent: Some(1.0f32),
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
            charge_source: Some(fidl_fuchsia_power_battery::ChargeSource::Usb),
            ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
        };
        let local = BatteryInfo::try_from(valid).expect("valid conversion");
        let expected = BatteryInfo {
            level_percent: 1.0f32,
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
        };
        assert_eq!(local, expected);
    }

    type WatchRequest = QueryResponseFut<Vec<Information>>;
    async fn make_watch_request() -> (WatchRequest, WatcherWatchResponder) {
        let (c, mut s) = fidl::endpoints::create_proxy_and_stream::<WatcherMarker>().unwrap();
        let watch_fut = c.watch(&mut [].into_iter()).check().expect("can make Watch request");
        let (_ids, responder) = s
            .select_next_some()
            .await
            .expect("fidl request")
            .into_watch()
            .expect("Watcher::watch request");
        (watch_fut, responder)
    }

    #[fuchsia::test]
    async fn subscriber_notified_of_peripheral_update() {
        let shared_state = Arc::new(PeripheralState::new());
        let shared_state1 = shared_state.clone();

        // New subscriber.
        let subscriber = shared_state.new_subscriber();
        let (watch_request, responder) = make_watch_request().await;
        subscriber.register(responder).expect("can register a subscriber");

        // The first update in a hanging-get should always resolve immediately with current state.
        let info = watch_request.await.expect("FIDL response");
        assert_eq!(info, vec![]);

        // Client subscribes again.
        let (watch_request1, responder1) = make_watch_request().await;
        subscriber.register(responder1).expect("can register a subscriber");

        // Receive a power update.
        let id = PeerId(111);
        let battery_info = BatteryInfo {
            level_percent: 10.0,
            level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
        };
        shared_state1.record_power_update(id, battery_info.clone());

        // Expect subscriber to get the update.
        let info2 = watch_request1.await.expect("FIDL response");
        let expected_info = vec![Information {
            identifier: Some(Identifier::PeerId(id.into())),
            battery_info: Some(fidl_fuchsia_power_battery::BatteryInfo {
                level_percent: Some(10.0),
                level_status: Some(fidl_fuchsia_power_battery::LevelStatus::Low),
                ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
            }),
            ..Information::EMPTY
        }];
        assert_eq!(info2, expected_info);
    }
}
