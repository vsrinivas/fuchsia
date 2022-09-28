// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use async_trait::async_trait;
use fdio;
use fidl::endpoints::{DiscoverableProtocolMarker, Proxy};
use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
use fidl_fuchsia_bluetooth_snoop::SnoopMarker;
use fidl_fuchsia_bluetooth_sys::PairingMarker;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_component::{client, server};
use futures::{future, StreamExt};
use tracing::{error, info, warn};

mod config;

const BT_GAP_CHILD_NAME: &str = "bt-gap";
const BT_RFCOMM_CHILD_NAME: &str = "bt-rfcomm";
const BT_FASTPAIR_PROVIDER_CHILD_NAME: &str = "bt-fastpair-provider";

#[async_trait]
trait ComponentClientAdapter {
    async fn open_childs_exposed_directory(
        &mut self,
        child_name: String,
    ) -> Result<fio::DirectoryProxy, Error>;
}

struct ComponentClient;

#[async_trait]
impl ComponentClientAdapter for ComponentClient {
    async fn open_childs_exposed_directory(
        &mut self,
        child_name: String,
    ) -> Result<fio::DirectoryProxy, Error> {
        client::open_childs_exposed_directory(child_name, None).await
    }
}

/// Open the directory of the child which will underlie any services. The specified child is the
/// preferred service provider if present, but if unavailable, fall back to `bt-gap`.
//
// TODO(fxbug.dev/71315): A single child instance won't function correctly in the presence
// of multiple bt-host devices during its lifetime. When handling this is a priority, we will
// likely need to either launch an instance of the child per-bt-host (e.g. inside bt-gap), or
// modify the child component to accommodate this issue.
async fn open_childs_service_directory<C: ComponentClientAdapter>(
    child_name: &str,
    component_client: &mut C,
) -> Result<fio::DirectoryProxy, Error> {
    let underlying_svc =
        component_client.open_childs_exposed_directory(child_name.to_owned()).await;
    match underlying_svc {
        Err(e) => {
            info!(
                "failed to bind child {}: {:?}, falling back to bt-gap's service directory",
                child_name, e,
            );
            component_client.open_childs_exposed_directory(BT_GAP_CHILD_NAME.to_owned()).await
        }
        dir => {
            info!("successfully opened `{}` svc directory", child_name);
            dir
        }
    }
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-init"]).expect("Can't init logger");
    info!("starting bt-init...");

    let mut executor = fasync::LocalExecutor::new().context("error creating executor")?;
    let cfg = config::Config::load().context("error loading config")?;

    // Start bt-snoop service before anything else and hold onto the connection until bt-init exits.
    let snoop_connection;
    if cfg.autostart_snoop() {
        info!("starting snoop service...");
        snoop_connection = client::connect_to_protocol::<SnoopMarker>();
        if let Err(e) = snoop_connection {
            warn!("failed to start snoop service: {}", e);
        } else {
            info!("snoop service started successfully");
        }
    }

    let run_bluetooth = async move {
        // Get the backing service directory of the `bt-rfcomm` and `bt-fastpair-provider` child
        // components.
        let underlying_profile_svc =
            open_childs_service_directory::<_>(BT_RFCOMM_CHILD_NAME, &mut ComponentClient).await?;
        let underlying_pairing_svc = open_childs_service_directory::<_>(
            BT_FASTPAIR_PROVIDER_CHILD_NAME,
            &mut ComponentClient,
        )
        .await?;
        // Expose the `bredr.Profile` and `sys.Pairing` protocols to the system.
        let mut fs = server::ServiceFs::new();
        let _ = fs
            .dir("svc")
            .add_service_at(ProfileMarker::PROTOCOL_NAME, |chan| {
                Some((ProfileMarker::PROTOCOL_NAME, chan))
            })
            .add_service_at(PairingMarker::PROTOCOL_NAME, |chan| {
                Some((PairingMarker::PROTOCOL_NAME, chan))
            });
        let _ = fs.take_and_serve_directory_handle()?;

        info!(
            "initialization complete, begin serving [{}, {}]",
            ProfileMarker::PROTOCOL_NAME,
            PairingMarker::PROTOCOL_NAME
        );
        let outer_fs = fs.for_each(move |(name, chan)| {
            let directory = match name {
                ProfileMarker::PROTOCOL_NAME => underlying_profile_svc.as_channel().as_ref(),
                PairingMarker::PROTOCOL_NAME => underlying_pairing_svc.as_channel().as_ref(),
                name => {
                    error!(
                        "Received unexpected service {} when we only expect to serve [{}, {}]",
                        name,
                        ProfileMarker::PROTOCOL_NAME,
                        PairingMarker::PROTOCOL_NAME,
                    );
                    return future::ready(());
                }
            };
            let _ = fdio::service_connect_at(directory, name, chan)
                .map_err(|e| warn!("error passing {} handle to service: {:?}", name, e));
            future::ready(())
        });
        Ok::<(), Error>(outer_fs.await)
    };

    executor
        .run_singlethreaded(run_bluetooth)
        .context("bt-init encountered an error during execution")
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        anyhow::format_err, fuchsia_async::Channel as AsyncChannel, fuchsia_zircon as zx,
        std::collections::HashSet,
    };
    struct MockComponentClient {
        pub children_to_fail_for: HashSet<String>,
        pub bt_gap_channel: Option<zx::Channel>,
        pub bt_rfcomm_channel: Option<zx::Channel>,
        pub bt_fastpair_provider_channel: Option<zx::Channel>,
    }

    impl MockComponentClient {
        fn new() -> Self {
            Self {
                children_to_fail_for: HashSet::new(),
                bt_gap_channel: None,
                bt_rfcomm_channel: None,
                bt_fastpair_provider_channel: None,
            }
        }

        fn child_channels_empty(&self) -> bool {
            self.bt_rfcomm_channel.is_none() && self.bt_fastpair_provider_channel.is_none()
        }
    }

    #[async_trait]
    impl ComponentClientAdapter for MockComponentClient {
        async fn open_childs_exposed_directory(
            &mut self,
            child_name: String,
        ) -> Result<fio::DirectoryProxy, Error> {
            if self.children_to_fail_for.contains(&child_name) {
                return Err(format_err!("couldn't open {}'s directory", &child_name));
            }
            let (local, client) = zx::Channel::create()?;
            match child_name.as_str() {
                BT_RFCOMM_CHILD_NAME => self.bt_rfcomm_channel = Some(local),
                BT_GAP_CHILD_NAME => self.bt_gap_channel = Some(local),
                BT_FASTPAIR_PROVIDER_CHILD_NAME => self.bt_fastpair_provider_channel = Some(local),
                _ => panic!("MockComponentClient received unexpected child name: {}", child_name),
            }
            Ok(fio::DirectoryProxy::from_channel(AsyncChannel::from_channel(client).unwrap()))
        }
    }

    fn assert_channels_connected(writer: &zx::Channel, reader: &zx::Channel) {
        let expected_bytes = [1, 2, 3, 4, 5];
        writer.write(&expected_bytes, &mut []).unwrap();
        let mut bytes = zx::MessageBuf::new();
        reader.read(&mut bytes).unwrap();
        assert_eq!(&expected_bytes, bytes.bytes());
    }

    #[fuchsia::test]
    async fn test_open_fastpair_provider_works() {
        let mut mock_client = MockComponentClient::new();

        // Directory should be connected to `bt-fastpair-provider`.
        let child_directory =
            open_childs_service_directory(BT_FASTPAIR_PROVIDER_CHILD_NAME, &mut mock_client)
                .await
                .unwrap();
        assert!(mock_client.bt_fastpair_provider_channel.is_some());
        assert_channels_connected(
            mock_client.bt_fastpair_provider_channel.unwrap().as_ref(),
            child_directory.as_channel().as_ref(),
        );
        // `bt-gap`s directory should not be used.
        assert!(mock_client.bt_gap_channel.is_none());
    }

    #[fuchsia::test]
    async fn test_open_rfcomm_works() {
        let mut mock_client = MockComponentClient::new();

        // If opening bt-rfcomm's directory works, the directory should be connected to bt-rfcomm.
        let profile_svc =
            open_childs_service_directory(BT_RFCOMM_CHILD_NAME, &mut mock_client).await.unwrap();
        assert!(mock_client.bt_rfcomm_channel.is_some());
        assert_channels_connected(
            mock_client.bt_rfcomm_channel.unwrap().as_ref(),
            profile_svc.as_channel().as_ref(),
        );
        // `bt-gap`s directory should not be used.
        assert!(mock_client.bt_gap_channel.is_none());
    }

    #[fuchsia::test]
    async fn test_open_child_fails() {
        // If opening the child directory fails, the directory should be connected to bt-gap.
        let children = [BT_RFCOMM_CHILD_NAME, BT_FASTPAIR_PROVIDER_CHILD_NAME];
        for child in children {
            let mut mock_client = MockComponentClient::new();
            let _ = mock_client.children_to_fail_for.insert(child.to_owned());
            let child_directory =
                open_childs_service_directory(child, &mut mock_client).await.unwrap();
            assert!(mock_client.child_channels_empty());
            assert!(mock_client.bt_gap_channel.is_some());
            assert_channels_connected(
                mock_client.bt_gap_channel.unwrap().as_ref(),
                child_directory.as_channel().as_ref(),
            );
        }
    }

    #[fuchsia::test]
    async fn test_open_child_and_gap_fail() {
        // If opening both bt-gap and child's directory fail, opening the service should fail.
        let mut mock_client = MockComponentClient::new();
        let _ = mock_client.children_to_fail_for.insert(BT_RFCOMM_CHILD_NAME.to_owned());
        let _ = mock_client.children_to_fail_for.insert(BT_GAP_CHILD_NAME.to_owned());
        let _ = mock_client.children_to_fail_for.insert(BT_FASTPAIR_PROVIDER_CHILD_NAME.to_owned());
        assert!(open_childs_service_directory(BT_RFCOMM_CHILD_NAME, &mut mock_client)
            .await
            .is_err());
        assert!(open_childs_service_directory(BT_FASTPAIR_PROVIDER_CHILD_NAME, &mut mock_client)
            .await
            .is_err());
    }
}
