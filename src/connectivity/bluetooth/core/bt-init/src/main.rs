// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::{DiscoverableProtocolMarker, ProtocolMarker},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_gatt::Server_Marker,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_bluetooth_rfcomm_test::RfcommTestMarker,
    fidl_fuchsia_bluetooth_snoop::SnoopMarker,
    fidl_fuchsia_bluetooth_sys::{
        AccessMarker, BootstrapMarker, ConfigurationMarker, HostWatcherMarker,
    },
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{self, App},
        fuchsia_single_component_package_url,
        server::{self, NestedEnvironment},
    },
    fuchsia_zircon as zx,
    futures::{future, Future, FutureExt, StreamExt},
    tracing::{info, warn},
};

mod config;

/// Creates a NestedEnvironment and launches the RFCOMM component at `component_url`, which
/// provides the Profile component
/// Returns the controller for the launched component and the NestedEnvironment - dropping
/// either will result in component termination.
async fn launch_rfcomm(
    rfcomm_url: String,
    bt_gap: &App,
) -> Result<(App, NestedEnvironment, impl Future<Output = ()> + '_), Error> {
    // RFCOMM's main responsibility is serving the Profile service. It handles RFCOMM Profile
    // requests itself, and uses the Profile service of bt-gap (which is just a passthrough for
    // bt-host's Profile server) for non-RFCOMM requests (i.e. L2CAP requests).
    //
    // TODO(fxbug.dev/71315): A single bt-rfcomm instance won't function correctly in the presence
    // of multiple bt-host devices during its lifetime. When handling this is a priority, we will
    // likely need to either launch an instance of bt-rfcomm per-bt-host (e.g. inside bt-gap), or
    // modify bt-rfcomm to accommodate this issue.
    let mut rfcomm_fs = server::ServiceFs::new();
    let _ = rfcomm_fs
        .add_service_at(ProfileMarker::PROTOCOL_NAME, move |chan| {
            info!("Connecting bt-rfcomm's Profile Service to bt-gap");
            if let Err(e) = bt_gap.pass_to_named_protocol(ProfileMarker::PROTOCOL_NAME, chan) {
                warn!("Failed to connect profile from bt-rfcomm to bt-gap: {:?}", e);
            }
            None
        })
        .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
        .add_proxy_service::<fidl_fuchsia_cobalt::LoggerFactoryMarker, _>();

    let env = rfcomm_fs.create_salted_nested_environment("bt-rfcomm")?;

    let bt_rfcomm = client::launch(&env.launcher(), rfcomm_url, None)?;

    // Spawn the RFCOMM ServiceFs onto a Task so that it is polled. Otherwise, the
    // ComponentControllerEventStream will block without returning the OnDirectoryReady event.
    let rfcomm_fut = rfcomm_fs.collect();

    // Wait until component has successfully launched before returning the controller.
    info!("Waiting for successful notification of bt-rfcomm launch...");
    let mut event_stream = bt_rfcomm.controller().take_event_stream();
    match future::select(event_stream.next(), rfcomm_fut).await {
        future::Either::Left((
            Some(Ok(ComponentControllerEvent::OnDirectoryReady { .. })),
            rfcomm_fut,
        )) => Ok((bt_rfcomm, env, rfcomm_fut)),
        future::Either::Left((Some(Err(e)), ..)) => {
            Err(format_err!("bt-rfcomm launch failure: {:?}", e))
        }
        _ => Err(format_err!("unable to launch bt-rfcomm")),
    }
}

// For mocking out the App class in tests
trait AppAdapter {
    fn pass_channel_to_named_service(
        &self,
        service_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), Error>;
}

impl AppAdapter for App {
    fn pass_channel_to_named_service(
        &self,
        service_name: &str,
        server_channel: zx::Channel,
    ) -> Result<(), Error> {
        self.pass_to_named_protocol(service_name, server_channel)
    }
}

fn handle_service_req<T: AppAdapter>(
    service_name: &str,
    server_channel: zx::Channel,
    bt_rfcomm: Option<&T>,
    bt_gap: &T,
) {
    let res = match (service_name, bt_rfcomm) {
        (ProfileMarker::PROTOCOL_NAME, Some(bt_rfcomm)) => {
            info!("Passing {} handle to bt-rfcomm", service_name);
            bt_rfcomm.pass_channel_to_named_service(service_name, server_channel)
        }
        (RfcommTestMarker::PROTOCOL_NAME, Some(bt_rfcomm)) => {
            info!("Passing {} handle to bt-rfcomm", service_name);
            bt_rfcomm.pass_channel_to_named_service(service_name, server_channel)
        }
        _ => {
            info!("Passing {} handle to bt-gap", service_name);
            bt_gap.pass_channel_to_named_service(service_name, server_channel)
        }
    };
    if let Err(e) = res {
        warn!("Error passing {} handle to service: {:?}", service_name, e);
    }
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-init"]).expect("Can't init logger");
    info!("Starting bt-init...");

    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
    let cfg = config::Config::load().context("Error loading config")?;

    // Start bt-snoop service before anything else and hold onto the connection until bt-init exits.
    let snoop_connection;
    if cfg.autostart_snoop() {
        info!("Starting snoop service...");
        snoop_connection = client::connect_to_protocol::<SnoopMarker>();
        if let Err(e) = snoop_connection {
            warn!("Failed to start snoop service: {}", e);
        } else {
            info!("Snoop service started successfully");
        }
    }

    info!("Launching BT-GAP service...");
    let launcher = client::launcher()
        .expect("Failed to launch bt-gap (bluetooth) service; could not access launcher service");
    let bt_gap = &client::launch(
        &launcher,
        fuchsia_single_component_package_url!("bt-gap").to_string(),
        None,
    )
    .context("Error launching BT-GAP component")?;
    info!("BT-GAP launched successfully");

    let run_bluetooth = async move {
        let rfcomm_url = fuchsia_single_component_package_url!("bt-rfcomm").to_string();
        let (bt_rfcomm, _env, rfcomm_fut) = match launch_rfcomm(rfcomm_url, bt_gap).await {
            Ok((bt_rfcomm, env, rfcomm_fut)) => {
                info!("bt-rfcomm launched successfully");
                (Some(bt_rfcomm), Some(env), rfcomm_fut.boxed())
            }
            Err(e) => {
                warn!("Failed to launch bt-rfcomm: {:?}", e);
                (None, None, future::ready(()).boxed())
            }
        };

        // Now that the child components are launched, we can begin serving bt-init services.
        let mut fs = server::ServiceFs::new();
        let _ = fs
            .dir("svc")
            .add_service_at(AccessMarker::NAME, |chan| Some((AccessMarker::NAME, chan)))
            .add_service_at(BootstrapMarker::NAME, |chan| Some((BootstrapMarker::NAME, chan)))
            .add_service_at(ConfigurationMarker::NAME, |chan| {
                Some((ConfigurationMarker::NAME, chan))
            })
            .add_service_at(CentralMarker::NAME, |chan| Some((CentralMarker::NAME, chan)))
            .add_service_at(HostWatcherMarker::NAME, |chan| Some((HostWatcherMarker::NAME, chan)))
            .add_service_at(PeripheralMarker::NAME, |chan| Some((PeripheralMarker::NAME, chan)))
            .add_service_at(ProfileMarker::NAME, |chan| Some((ProfileMarker::NAME, chan)))
            .add_service_at(Server_Marker::NAME, |chan| Some((Server_Marker::NAME, chan)))
            .add_service_at(RfcommTestMarker::NAME, |chan| Some((RfcommTestMarker::NAME, chan)));
        let _ = fs.take_and_serve_directory_handle()?;

        info!("Initialization complete, begin serving FIDL protocols");
        let outer_fs = fs.for_each(move |(name, chan)| {
            handle_service_req(name, chan, bt_rfcomm.as_ref(), bt_gap);
            future::ready(())
        });
        future::join(outer_fs, rfcomm_fut).await;
        Ok::<(), Error>(())
    };

    executor
        .run_singlethreaded(run_bluetooth)
        .context("bt-init encountered an error during execution")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    struct MockApp {
        pub last_channel: RefCell<Option<zx::Channel>>,
    }

    impl AppAdapter for MockApp {
        fn pass_channel_to_named_service(
            &self,
            _service_name: &str,
            server_channel: zx::Channel,
        ) -> Result<(), Error> {
            let _ = self.last_channel.replace(Some(server_channel));
            Ok(())
        }
    }

    fn assert_channels_connected(writer: &zx::Channel, reader: &zx::Channel) {
        let expected_bytes = [1, 2, 3, 4, 5];
        writer.write(&expected_bytes, &mut []).unwrap();
        let mut bytes = zx::MessageBuf::new();
        reader.read(&mut bytes).unwrap();
        assert_eq!(&expected_bytes, bytes.bytes());
    }

    #[test]
    fn test_handle_service_req() {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        let bt_gap = MockApp { last_channel: RefCell::new(None) };
        let bt_rfcomm = MockApp { last_channel: RefCell::new(None) };
        // When bt_rfcomm is present, Profile requests get routed to bt_rfcomm
        handle_service_req(ProfileMarker::PROTOCOL_NAME, server_end, Some(&bt_rfcomm), &bt_gap);
        assert_channels_connected(&client_end, bt_rfcomm.last_channel.borrow().as_ref().unwrap());

        // When bt_rfcomm is present, non-Profile requests get routed to bt_gap
        let (client_end, server_end) = zx::Channel::create().unwrap();
        handle_service_req(AccessMarker::PROTOCOL_NAME, server_end, Some(&bt_rfcomm), &bt_gap);
        assert_channels_connected(&client_end, bt_gap.last_channel.borrow().as_ref().unwrap());

        // When bt_rfcomm is not present, Profile requests get routed directly to bt_gap
        let (client_end, server_end) = zx::Channel::create().unwrap();
        handle_service_req(ProfileMarker::PROTOCOL_NAME, server_end, None, &bt_gap);
        assert_channels_connected(&client_end, bt_gap.last_channel.borrow().as_ref().unwrap());
    }
}
