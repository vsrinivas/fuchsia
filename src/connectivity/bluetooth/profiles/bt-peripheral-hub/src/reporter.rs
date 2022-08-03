// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_power::{Information, ReporterRequest, ReporterRequestStream};
use fuchsia_bluetooth::types::PeerId;
use fuchsia_zircon as zx;
use futures::{Future, TryStreamExt};
use std::{convert::TryFrom, sync::Arc};
use tracing::debug;

use crate::error::Error;
use crate::peripheral_state::{peer_id_from_identifier, BatteryInfo, PeripheralState};

/// Represents a handler for a client connection to the `fuchsia.bluetooth.power.Reporter` FIDL
/// capability.
pub struct Reporter {
    shared_state: Arc<PeripheralState>,
}

impl Reporter {
    pub fn new(shared_state: Arc<PeripheralState>) -> Self {
        Self { shared_state }
    }

    fn validate_information(info: Information) -> Result<(PeerId, BatteryInfo), Error> {
        // The `identifier` and `battery_info` are mandatory fields.
        let identifier = info.identifier.clone().ok_or(Error::from(&info))?;
        let battery_info =
            BatteryInfo::try_from(info.battery_info.clone().ok_or(Error::from(&info))?)?;

        // Currently, there is nothing to do if the local_device is specified. This component only
        // handles updates about peripherals.
        let id = peer_id_from_identifier(&identifier)?;
        Ok((id, battery_info))
    }

    fn handle_power_report_request(&self, report: ReporterRequest) -> Result<(), Error> {
        debug!("Received Reporter::Report request: {:?}", report);
        // There is only one method in the `power.Reporter` protocol.
        let (report, responder) = report.into_report().expect("Reporter::Report request");

        let (id, battery_info) = match Self::validate_information(report) {
            Ok((id, b)) => {
                let _ = responder.send(&mut Ok(()))?;
                (id, b)
            }
            Err(e) => {
                let _ = responder.send(&mut Err(zx::Status::from(&e).into_raw()))?;
                return Ok(());
            }
        };

        self.shared_state.record_power_update(id, battery_info);
        Ok(())
    }

    pub fn run(self, stream: ReporterRequestStream) -> impl Future<Output = Result<(), Error>> {
        stream.map_err(Into::into).try_for_each(move |report| {
            futures::future::ready(self.handle_power_report_request(report))
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl_fuchsia_bluetooth_power::{Identifier, ReporterMarker, ReporterProxy};
    use futures::{pin_mut, select, Future, FutureExt};

    fn make_reporter_task(
    ) -> (impl Future<Output = Result<(), Error>>, ReporterProxy, Arc<PeripheralState>) {
        let shared_state = Arc::new(PeripheralState::new());
        let server = Reporter::new(shared_state.clone());
        let (c, s) = fidl::endpoints::create_proxy_and_stream::<ReporterMarker>().unwrap();
        let server_task = server.run(s);

        (server_task, c, shared_state)
    }

    #[fuchsia::test]
    async fn server_task_finishes_when_client_end_closes() {
        let (reporter_task, reporter_proxy, _state) = make_reporter_task();

        drop(reporter_proxy);
        let result = reporter_task.await;
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    async fn invalid_request_is_error() {
        let (reporter_task, reporter_proxy, _state) = make_reporter_task();
        let server_task = reporter_task.fuse();

        let info = Information {
            identifier: Some(Identifier::PeerId(PeerId(123).into())),
            battery_info: Some(fidl_fuchsia_power_battery::BatteryInfo::EMPTY),
            ..Information::EMPTY
        };
        let report_request_fut = reporter_proxy.report(info);
        pin_mut!(server_task, report_request_fut);

        select! {
            _ = server_task => panic!("Server shouldn't terminate"),
            result = report_request_fut => {
                assert_eq!(result.expect("valid fidl response"), Err(zx::Status::INVALID_ARGS.into_raw()));
            }
        }
    }

    #[fuchsia::test]
    async fn valid_power_report_is_saved() {
        let (reporter_task, reporter_proxy, state) = make_reporter_task();
        let server_task = reporter_task.fuse();

        let id = PeerId(123);
        let info = Information {
            identifier: Some(Identifier::PeerId(id.into())),
            battery_info: Some(fidl_fuchsia_power_battery::BatteryInfo {
                level_percent: Some(5.0f32),
                ..fidl_fuchsia_power_battery::BatteryInfo::EMPTY
            }),
            ..Information::EMPTY
        };
        let report_request_fut = reporter_proxy.report(info);
        pin_mut!(server_task, report_request_fut);

        select! {
            _ = server_task => panic!("Server shouldn't terminate"),
            result = report_request_fut => {
                assert_matches!(result.expect("valid fidl response"), Ok(_));
            }
        }
        assert!(state.contains_entry(&id));
    }
}
