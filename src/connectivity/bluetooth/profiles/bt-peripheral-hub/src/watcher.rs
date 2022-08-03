// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_power::{WatcherRequest, WatcherRequestStream, WatcherWatchResponder};
use futures::{Future, TryStreamExt};
use std::sync::Arc;
use tracing::debug;

use crate::error::Error;
use crate::peripheral_state::{PeripheralState, PeripheralSubscriber};

/// Represents a handler for a client connection to the `fuchsia.bluetooth.power.Watcher` FIDL
/// capability.
pub struct Watcher {
    /// Hanging-get subscriber to register `Watcher::Watch` requests.
    subscriber: PeripheralSubscriber<WatcherWatchResponder>,
}

impl Watcher {
    pub fn new(shared_state: Arc<PeripheralState>) -> Self {
        let subscriber = shared_state.new_subscriber();
        Self { subscriber }
    }

    fn handle_watcher_request(&mut self, watch: WatcherRequest) -> Result<(), Error> {
        debug!("Received Watcher::Watch request: {:?}", watch);
        // There is only one method in the `power.Watcher` protocol.
        // TODO(fxbug.dev/86556): Filter responses by `_ids`.
        let (_ids, responder) = watch.into_watch().expect("Watcher::Watch request");
        self.subscriber.register(responder)?;
        Ok(())
    }

    pub fn run(mut self, stream: WatcherRequestStream) -> impl Future<Output = Result<(), Error>> {
        stream
            .map_err(Into::into)
            .try_for_each(move |watch| futures::future::ready(self.handle_watcher_request(watch)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_test_helpers::run_while;
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_power::{Identifier, Information, WatcherMarker, WatcherProxy};
    use fidl_fuchsia_power_battery::{BatteryInfo, LevelStatus};
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::PeerId;

    fn make_watcher_task(
    ) -> (impl Future<Output = Result<(), Error>>, WatcherProxy, Arc<PeripheralState>) {
        let shared_state = Arc::new(PeripheralState::new());
        let server = Watcher::new(shared_state.clone());
        let (c, s) = fidl::endpoints::create_proxy_and_stream::<WatcherMarker>().unwrap();
        let server_task = server.run(s);

        (server_task, c, shared_state)
    }

    #[fuchsia::test]
    async fn server_task_finishes_when_client_end_closes() {
        let (watcher_task, watcher_proxy, _state) = make_watcher_task();

        drop(watcher_proxy);
        let result = watcher_task.await;
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    async fn watcher_client_is_notified() {
        let (watcher_task, watcher_proxy, state) = make_watcher_task();
        let _server_task = fasync::Task::spawn(async move {
            let result = watcher_task.await;
            panic!("Watcher Server finished unexpectedly: {:?}", result);
        });

        // The first Watch request should immediately resolve, per hanging-get invariants.
        let peripherals = watcher_proxy.watch(&mut [].into_iter()).await.expect("FIDL response");
        // No peripherals to report.
        assert_eq!(peripherals, vec![]);

        // A subsequent Watch request should only resolve when the state changes.
        let watch_fut =
            watcher_proxy.watch(&mut [].into_iter()).check().expect("valid FIDL request");
        // Simulate a change in state.
        let id = PeerId(123);
        let battery_info = BatteryInfo {
            level_percent: Some(10.0),
            level_status: Some(LevelStatus::Ok),
            ..BatteryInfo::EMPTY
        };
        state.record_power_update(id, battery_info.clone().try_into().unwrap());

        // Expect the Watch request to resolve.
        let peripherals = watch_fut.await.expect("FIDL response");
        let expected_peripherals = vec![Information {
            identifier: Some(Identifier::PeerId(id.into())),
            battery_info: Some(battery_info),
            ..Information::EMPTY
        }];
        assert_eq!(peripherals, expected_peripherals);
    }

    #[fuchsia::test]
    fn duplicate_watch_requests_is_error() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (watcher_task, watcher_proxy, _state) = make_watcher_task();

        let watch_request1 = watcher_proxy.watch(&mut [].into_iter());
        let (peripherals, mut watcher_task) = run_while(&mut exec, watcher_task, watch_request1);
        assert_eq!(peripherals.expect("FIDL response"), vec![]);

        let mut watch_request2 = watcher_proxy.watch(&mut [].into_iter());
        let _ = exec.run_until_stalled(&mut watcher_task).expect_pending("main loop active");
        let _ = exec.run_until_stalled(&mut watch_request2).expect_pending("no FIDL response");
        // A subsequent Watch request while one is outstanding is an Error and a violation of the
        // API. The Watcher server associated with this FIDL client should terminate.
        let _watch_request3 = watcher_proxy.watch(&mut [].into_iter());
        let server_result =
            exec.run_until_stalled(&mut watcher_task).expect("main loop terminated");
        assert_matches!(server_result, Err(Error::HangingGet(_)));
    }

    #[fuchsia::test]
    fn duplicate_power_update_is_noop() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (mut watcher_task, watcher_proxy, state) = make_watcher_task();

        // Initial state.
        let id = PeerId(123);
        let battery_info = BatteryInfo {
            level_percent: Some(10.0),
            level_status: Some(LevelStatus::Ok),
            ..BatteryInfo::EMPTY
        };
        state.record_power_update(id, battery_info.clone().try_into().unwrap());
        let _ = exec.run_until_stalled(&mut watcher_task).expect_pending("main loop active");

        // The first Watch request should immediately resolve, per hanging-get invariants.
        let watch_request1 = watcher_proxy.watch(&mut [].into_iter());
        let (peripherals, mut watcher_task) = run_while(&mut exec, watcher_task, watch_request1);
        assert_matches!(peripherals.expect("FIDL response")[..], [Information { .. }]);

        // Client re-registers the hanging-get request.
        let mut watch_request2 = watcher_proxy.watch(&mut [].into_iter());
        let _ = exec.run_until_stalled(&mut watcher_task).expect_pending("main loop active");
        let _ = exec.run_until_stalled(&mut watch_request2).expect_pending("no FIDL response");

        // An identical report - don't expect the client to be notified.
        state.record_power_update(id, battery_info.try_into().unwrap());
        let _ = exec.run_until_stalled(&mut watch_request2).expect_pending("no FIDL response");
    }
}
