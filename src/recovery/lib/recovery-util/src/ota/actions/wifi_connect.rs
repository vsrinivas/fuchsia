// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ota::action::EventHandlerHolder;
use crate::ota::state_machine::{Event, Network, Password};
use crate::wlan::{create_network_info, WifiConnect, WifiConnectImpl};
use anyhow::Error;
use fuchsia_async::{DurationExt, Task};
use fuchsia_zircon::Duration;

/// Connects to a Wifi network.
/// Asynchronously ends event Connected or Error as appropriate.
pub struct WifiConnectAction {}

impl WifiConnectAction {
    pub fn run(event_handler: EventHandlerHolder, network: Network, password: Password) {
        // This is split into two parts for testing. It allowing a fake wifi service to be injected.
        Self::run_with_wifi_service(
            event_handler,
            network,
            password,
            Box::new(WifiConnectImpl::new()),
        );
    }

    fn run_with_wifi_service(
        event_handler: EventHandlerHolder,
        network: Network,
        password: Password,
        wifi_service: Box<dyn WifiConnect>,
    ) {
        let task = async move {
            let mut event_handler = event_handler.lock().unwrap();
            match connect_to_wifi(&*wifi_service, &network, &password).await {
                Result::Ok(_) => event_handler.handle_event(Event::WiFiConnected),
                Result::Err(error) => {
                    // Let the "Connecting" message stay there for a second so the
                    // user can see that something was tried.
                    // TODO(b/258576788): Confirm sleep details etc. with UX
                    let sleep_time = Duration::from_seconds(1);
                    fuchsia_async::Timer::new(sleep_time.after_now()).await;
                    println!("Failed to connect: {}", error);
                    event_handler.handle_event(Event::Error(error.to_string()));
                }
            };
        };
        Task::local(task).detach();
    }
}

async fn connect_to_wifi(
    wifi_service: &dyn WifiConnect,
    ssid: &str,
    password: &str,
) -> Result<(), Error> {
    use fidl_fuchsia_wlan_policy::SecurityType;
    let network = if password == "" {
        create_network_info(ssid, None, None)
    } else {
        // TODO(b/258731148): Verify whether the security_type is required or should be passed in
        create_network_info(ssid, Some(password), Some(SecurityType::Wpa2))
    };
    wifi_service.connect(network).await
}

#[cfg(test)]
mod tests {
    use super::WifiConnectAction;
    use crate::ota::state_machine::{Event, EventHandler, MockEventHandler};
    use crate::wlan::{NetworkInfo, WifiConnect};
    use anyhow::{anyhow, Error};
    use async_trait::async_trait;
    use fidl_fuchsia_wlan_policy::NetworkConfig;
    use fuchsia_async::{self as fasync};
    use fuchsia_zircon::Duration;
    use futures::future;
    use mockall::predicate::eq;
    use std::sync::{Arc, Mutex};

    struct FakeWifiConnectImpl {
        connected: bool,
    }

    impl FakeWifiConnectImpl {
        fn new(will_connect: bool) -> Self {
            Self { connected: will_connect }
        }
    }

    #[async_trait(?Send)]
    impl WifiConnect for FakeWifiConnectImpl {
        // Not used in these tests
        async fn scan_for_networks(&self) -> Result<Vec<NetworkInfo>, Error> {
            Err(Error::msg("This should not be called"))
        }

        async fn connect(&self, _network: NetworkConfig) -> Result<(), Error> {
            if self.connected {
                Ok(())
            } else {
                Err(anyhow!("Error"))
            }
        }
    }

    #[fuchsia::test]
    fn test_connect_ok() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut event_handler = MockEventHandler::new();
        event_handler
            .expect_handle_event()
            .with(eq(Event::WiFiConnected))
            .times(1)
            .return_const(());
        let event_handler: Box<dyn EventHandler> = Box::new(event_handler);
        let event_handler = Arc::new(Mutex::new(event_handler));
        WifiConnectAction::run_with_wifi_service(
            event_handler,
            "network".to_string(),
            "password".to_string(),
            Box::new(FakeWifiConnectImpl::new(true)),
        );
        // Make sure the task under test runs to its finish
        let _ = exec.run_until_stalled(&mut future::pending::<()>());
    }

    #[fuchsia::test]
    fn test_connect_fails() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(0));
        let mut event_handler = MockEventHandler::new();
        event_handler
            .expect_handle_event()
            .with(eq(Event::Error("".to_string())))
            .times(1)
            .return_const(());
        let event_handler: Box<dyn EventHandler> = Box::new(event_handler);
        let event_handler = Arc::new(Mutex::new(event_handler));
        WifiConnectAction::run_with_wifi_service(
            event_handler,
            "network".to_string(),
            "password".to_string(),
            Box::new(FakeWifiConnectImpl::new(false)),
        );
        // Make sure the task under test runs to its delay
        let _ = exec.run_until_stalled(&mut future::pending::<()>());
        exec.set_fake_time(fasync::Time::after(Duration::from_seconds(3).into()));
        exec.wake_expired_timers();
        // Make sure the task under test runs to its finish
        let _ = exec.run_until_stalled(&mut future::pending::<()>());
    }
}
