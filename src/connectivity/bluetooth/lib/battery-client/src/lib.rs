// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapper around the `fuchsia.power.BatteryManager` capability.
//!
//! This library provides an API for receiving updates about the current battery information on the
//! local Fuchsia device.
//! A `Stream` implementation is provided for clients that rely on notification-style updates.
//! Additional methods are also provided to get the most recently received battery information.
//!
//! ### Example Usage:
//!
//! // Create the BatteryClient. Under the hood, this will connect to the `BatteryManager`
//! // capability.
//! let batt_client = BatteryClient::create()?;
//!
//! // Listen for events from the BatteryClient stream implementation.
//! if let Some(battery_info) = batt_client.next().await? {
//!     if let Some(level_percent) = battery_info.level() {
//!       // Report to peer.
//!     }
//! }
//!

use anyhow::format_err;
use core::{
    convert::{TryFrom, TryInto},
    pin::Pin,
    task::{Context, Poll},
};
use derivative::Derivative;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_power as fpower;
use fuchsia_component::client::connect_to_protocol;
use futures::stream::{FusedStream, Stream, StreamExt};
use tracing::debug;

/// Error type used by this library.
mod error;
pub use crate::error::BatteryClientError;

/// The minimum battery level (in %) that will be reported.
pub const MIN_BATTERY_LEVEL: u8 = 0;
/// The maximum battery level (in %) that will be reported.
pub const MAX_BATTERY_LEVEL: u8 = 100;

/// The current battery level, represented as an integer level in the range
/// [MIN_BATTERY_LEVEL, MAX_BATTERY_LEVEL].
#[derive(Debug, PartialEq, Clone)]
pub enum BatteryLevel {
    Normal(u8),
    Warning(u8),
    Critical(u8),
    FullCharge,
}

impl BatteryLevel {
    pub fn level(&self) -> u8 {
        match self {
            Self::Normal(l) => *l,
            Self::Warning(l) => *l,
            Self::Critical(l) => *l,
            Self::FullCharge => MAX_BATTERY_LEVEL,
        }
    }
}

#[derive(Debug, PartialEq, Clone)]
pub enum BatteryInfo {
    /// No battery information is available.
    NotAvailable,
    /// Currently on battery power with a specified level percentage.
    Battery(BatteryLevel),
    /// Currently on an external power source.
    External,
}

impl BatteryInfo {
    pub fn level(&self) -> Option<u8> {
        if let Self::Battery(l) = &self {
            return Some(l.level());
        }
        None
    }
}

impl TryFrom<fpower::BatteryInfo> for BatteryInfo {
    type Error = BatteryClientError;

    fn try_from(src: fpower::BatteryInfo) -> Result<Self, Self::Error> {
        if src.status != Some(fpower::BatteryStatus::Ok) {
            return Ok(BatteryInfo::NotAvailable);
        }

        let fidl_level = src.level_status.unwrap_or(fpower::LevelStatus::Unknown);
        if fidl_level == fpower::LevelStatus::Unknown {
            return Ok(BatteryInfo::NotAvailable);
        }

        // Per the `fidl_fuchsia_power` documentation, if `level_status` is known, then the
        // level percentage will also be provided.
        let level = src
            .level_percent
            .ok_or(BatteryClientError::info(format_err!("Missing battery level percentage")))?;
        if level < MIN_BATTERY_LEVEL as f32 || level > MAX_BATTERY_LEVEL as f32 {
            return Err(BatteryClientError::info(format_err!(
                "Invalid battery level percentage: {:?}",
                level
            )));
        }
        // This operation is safe as we guarantee `level` is in [0, 100]. The floor of the level %
        // is used so that we never overestimate the battery level when rounding to the nearest
        // integer %.
        let level_floor: u8 = level.floor() as u8;

        let battery_level = match fidl_level {
            _s if level_floor == MAX_BATTERY_LEVEL => BatteryLevel::FullCharge,
            fpower::LevelStatus::Ok | fpower::LevelStatus::Low => BatteryLevel::Normal(level_floor),
            fpower::LevelStatus::Warning => BatteryLevel::Warning(level_floor),
            fpower::LevelStatus::Critical => BatteryLevel::Critical(level_floor),
            fpower::LevelStatus::Unknown => unreachable!("LevelStatus is known"),
        };

        Ok(BatteryInfo::Battery(battery_level))
    }
}

/// Manages the connection to the Fuchsia `BatteryManager` service.
#[derive(Derivative)]
#[derivative(Debug)]
pub struct BatteryClient {
    /// The client end of the connection to the `BatteryManager` service.
    _svc: fpower::BatteryManagerProxy,
    /// A stream of battery updates received from the system.
    #[derivative(Debug = "ignore")]
    watcher: fpower::BatteryInfoWatcherRequestStream,
    /// The most recent battery information received from the `watcher`.
    current_info: BatteryInfo,
    /// A flag indicating the termination status of the `BatteryClient`.
    terminated: bool,
}

impl BatteryClient {
    /// Creates and returns an object to track updates from the Fuchsia Battery Service.
    pub fn create() -> Result<Self, BatteryClientError> {
        let battery_svc = connect_to_protocol::<fpower::BatteryManagerMarker>()
            .map_err(BatteryClientError::manager_unavailable)?;
        Self::register_updates(battery_svc)
    }

    /// Register for battery change updates from the Fuchsia battery `svc`.
    pub fn register_updates(
        battery_svc: fpower::BatteryManagerProxy,
    ) -> Result<Self, BatteryClientError> {
        let (watcher_client, watcher) =
            create_request_stream::<fpower::BatteryInfoWatcherMarker>()?;
        battery_svc.watch(watcher_client)?;

        Ok(Self {
            _svc: battery_svc,
            watcher,
            current_info: BatteryInfo::NotAvailable,
            terminated: false,
        })
    }

    /// The current battery percentage (from 0 to 100), or None if it is not known.
    pub fn battery_percent(&self) -> Option<u8> {
        self.current_info.level()
    }

    pub fn battery_status(&self) -> &BatteryInfo {
        &self.current_info
    }

    /// Handle a battery update request - returns the extracted battery information on success,
    /// BatteryClientError otherwise.
    fn handle_battery_info_request(
        &mut self,
        request: fpower::BatteryInfoWatcherRequest,
    ) -> Result<BatteryInfo, BatteryClientError> {
        let fpower::BatteryInfoWatcherRequest::OnChangeBatteryInfo { info, responder, .. } =
            request;
        debug!("Received battery update from system: {:?}", info);
        responder.send()?;

        // TODO(fxbug.dev/89894): Invalid upstream information likely indicates a bug or a mismatch
        // between this library and the battery manager. Close the watcher channel and attempt to
        // re-register for updates.
        let converted_result: Result<BatteryInfo, BatteryClientError> = info.try_into();
        self.current_info =
            converted_result.as_ref().map_or(BatteryInfo::NotAvailable, Clone::clone);
        converted_result
    }
}

impl Stream for BatteryClient {
    type Item = Result<BatteryInfo, BatteryClientError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("Cannot poll a terminated stream");
        }

        match self.watcher.poll_next_unpin(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(Some(Ok(request))) => {
                let update = self.handle_battery_info_request(request);
                Poll::Ready(Some(update))
            }
            Poll::Ready(Some(Err(e))) => Poll::Ready(Some(Err(BatteryClientError::watcher(e)))),
            Poll::Ready(None) => {
                self.terminated = true;
                Poll::Ready(None)
            }
        }
    }
}

impl FusedStream for BatteryClient {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use fuchsia_async as fasync;
    use futures::pin_mut;

    fn setup_battery_client(
    ) -> (fasync::TestExecutor, BatteryClient, fpower::BatteryInfoWatcherProxy) {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (c, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fpower::BatteryManagerMarker>().unwrap();
        let mut client = BatteryClient::register_updates(c).expect("can register");
        expect_stream_pending(&mut exec, &mut client);

        let upstream_battery_notifier = {
            let fut = stream.next();
            pin_mut!(fut);
            match exec.run_until_stalled(&mut fut).expect("fut is ready").unwrap() {
                Ok(fpower::BatteryManagerRequest::Watch { watcher, .. }) => {
                    watcher.into_proxy().unwrap()
                }
                x => panic!("Expected Watch request, got: {:?}", x),
            }
        };

        (exec, client, upstream_battery_notifier)
    }

    #[track_caller]
    fn expect_stream_item<S>(exec: &mut fasync::TestExecutor, client: &mut S) -> S::Item
    where
        S: Stream + Unpin,
    {
        let fut = client.next();
        pin_mut!(fut);
        exec.run_until_stalled(&mut fut).expect("stream item is ready").expect("valid stream item")
    }

    #[track_caller]
    fn expect_stream_pending<S>(exec: &mut fasync::TestExecutor, stream: &mut S)
    where
        S: Stream + Unpin,
    {
        let stream_fut = stream.next();
        pin_mut!(stream_fut);
        exec.run_until_stalled(&mut stream_fut).expect_pending("stream waiting for item");
    }

    /// Simulates a battery update by sending the `update` via the `upstream_battery_notifier`.
    /// Expects the update to be received by the `battery_client` and returns the resulting
    /// converted update result.
    fn send_update_and_poll_battery_client(
        exec: &mut fasync::TestExecutor,
        battery_client: &mut BatteryClient,
        upstream_battery_notifier: &fpower::BatteryInfoWatcherProxy,
        update: fpower::BatteryInfo,
    ) -> Result<BatteryInfo, BatteryClientError> {
        let update_fut = upstream_battery_notifier.on_change_battery_info(update);
        pin_mut!(update_fut);
        exec.run_until_stalled(&mut update_fut).expect_pending("waiting for fidl response");

        let item = expect_stream_item(exec, battery_client);
        // Once the BatteryClient receives the update, the FIDL method should resolve.
        assert_matches!(exec.run_until_stalled(&mut update_fut).expect("should resolve"), Ok(_));

        item
    }

    #[fuchsia::test]
    fn battery_client_terminates_when_watcher_terminates() {
        let (mut exec, mut client, upstream_battery_notifier) = setup_battery_client();

        // Upstream watcher (owned by the Battery Manager) disconnects for some reason.
        drop(upstream_battery_notifier);

        // Expect the stream impl of the BatteryClient to finish.
        {
            let client_stream = client.next();
            pin_mut!(client_stream);
            let res = exec
                .run_until_stalled(&mut client_stream)
                .expect("battery client should terminate");
            assert_matches!(res, None);
        }
        assert!(client.is_terminated());
    }

    #[fuchsia::test]
    fn battery_client_stream_impl_with_empty_updates() {
        let (mut exec, mut client, upstream_battery_notifier) = setup_battery_client();
        assert!(!client.is_terminated());

        // An empty update from the `fuchsia.power` service should trigger a stream event.
        let update = fpower::BatteryInfo::EMPTY;
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::NotAvailable);

        // A not-OK battery status is fine. Stream event should still be NotAvailable.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::NotPresent),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::NotAvailable);

        // An unknown Battery Level is also a non-interesting update.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Unknown),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::NotAvailable);
    }

    #[fuchsia::test]
    fn battery_client_stream_impl_with_invalid_updates() {
        let (mut exec, mut client, upstream_battery_notifier) = setup_battery_client();

        // Battery level known, but missing level_percent is invalid per `fuchsia.power` docs.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Warning),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        );
        assert_matches!(info, Err(_));

        // Level percent of more than 100 is an error.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Ok),
            level_percent: Some(125.58f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        );
        assert_matches!(info, Err(_));

        // Level percent of less than 0 is an error.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Critical),
            level_percent: Some(-10.332),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        );
        assert_matches!(info, Err(_));
    }

    #[fuchsia::test]
    fn battery_client_stream_impl_with_normal_updates() {
        let (mut exec, mut client, upstream_battery_notifier) = setup_battery_client();
        assert!(!client.is_terminated());
        assert_eq!(client.battery_percent(), None);
        assert_eq!(client.battery_status(), &BatteryInfo::NotAvailable);

        // A typical battery info update with a level percent should trigger a battery client stream
        // item.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Low),
            level_percent: Some(88f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::Battery(BatteryLevel::Normal(88)));

        // A client can also query the BatteryClient to figure out the current battery %.
        assert_eq!(client.battery_percent(), Some(88));

        // Invalid update should be an error on the stream. The current battery info should be
        // reflect the error and be not available.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Critical),
            level_percent: Some(-10.332),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        );
        assert_matches!(info, Err(_));
        assert_eq!(client.battery_status(), &BatteryInfo::NotAvailable);

        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Critical),
            level_percent: Some(5.58f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::Battery(BatteryLevel::Critical(5)));

        // Minimum battery %.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Critical),
            level_percent: Some(0.0f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::Battery(BatteryLevel::Critical(0)));

        // Maximum battery %.
        let update = fpower::BatteryInfo {
            status: Some(fpower::BatteryStatus::Ok),
            level_status: Some(fpower::LevelStatus::Critical),
            level_percent: Some(100.0f32),
            ..fpower::BatteryInfo::EMPTY
        };
        let info = send_update_and_poll_battery_client(
            &mut exec,
            &mut client,
            &upstream_battery_notifier,
            update,
        )
        .expect("valid update");
        assert_eq!(info, BatteryInfo::Battery(BatteryLevel::FullCharge));
    }
}
