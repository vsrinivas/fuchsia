// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    async_trait::async_trait,
    fdio,
    fidl::endpoints::{DiscoverableProtocolMarker, Proxy},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_snoop::SnoopMarker,
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async as fasync,
    fuchsia_component::{client, server},
    futures::{future, StreamExt},
    tracing::{error, info, warn},
};

mod config;

const BT_GAP_CHILD_NAME: &str = "bt-gap";
const BT_RFCOMM_CHILD_NAME: &str = "bt-rfcomm";

#[async_trait]
trait ComponentClientAdapter {
    async fn open_childs_exposed_directory(
        &mut self,
        child_name: String,
    ) -> Result<DirectoryProxy, Error>;
}

struct ComponentClient;

#[async_trait]
impl ComponentClientAdapter for ComponentClient {
    async fn open_childs_exposed_directory(
        &mut self,
        child_name: String,
    ) -> Result<DirectoryProxy, Error> {
        client::open_childs_exposed_directory(child_name, None).await
    }
}

/// Open the directory of the child which will underlie the Profile service. bt-rfcomm is the
/// preferred Profile service provider if present, but if it is not we fall back to bt-gap.
//
// TODO(fxbug.dev/71315): A single bt-rfcomm instance won't function correctly in the presence
// of multiple bt-host devices during its lifetime. When handling this is a priority, we will
// likely need to either launch an instance of bt-rfcomm per-bt-host (e.g. inside bt-gap), or
// modify bt-rfcomm to accommodate this issue.
async fn open_childs_service_directory<C: ComponentClientAdapter>(
    component_client: &mut C,
) -> Result<DirectoryProxy, Error> {
    let underlying_profile_svc =
        component_client.open_childs_exposed_directory(BT_RFCOMM_CHILD_NAME.to_owned()).await;
    match underlying_profile_svc {
        Err(e) => {
            warn!(
                "failed to bind bt-rfcomm child ({:?}), falling back to bt-gap's Profile service",
                e
            );
            component_client.open_childs_exposed_directory(BT_GAP_CHILD_NAME.to_owned()).await
        }
        dir => {
            info!("successfully opened bt-rfcomm svc directory");
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
        let underlying_profile_svc = open_childs_service_directory(&mut ComponentClient).await?;
        let mut fs = server::ServiceFs::new();
        let _ = fs.dir("svc").add_service_at(ProfileMarker::PROTOCOL_NAME, |chan| {
            Some((ProfileMarker::PROTOCOL_NAME, chan))
        });
        let _ = fs.take_and_serve_directory_handle()?;

        info!("initialization complete, begin serving {}", ProfileMarker::PROTOCOL_NAME);
        let outer_fs = fs.for_each(move |(name, chan)| {
            if name != ProfileMarker::PROTOCOL_NAME {
                error!(
                    "Received unexpected service {} when we only expect to serve {}",
                    name,
                    ProfileMarker::PROTOCOL_NAME
                );
                return future::ready(());
            }
            let _ =
                fdio::service_connect_at(underlying_profile_svc.as_channel().as_ref(), name, chan)
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
    }

    impl MockComponentClient {
        fn new() -> Self {
            Self {
                children_to_fail_for: HashSet::new(),
                bt_gap_channel: None,
                bt_rfcomm_channel: None,
            }
        }
    }

    #[async_trait]
    impl ComponentClientAdapter for MockComponentClient {
        async fn open_childs_exposed_directory(
            &mut self,
            child_name: String,
        ) -> Result<DirectoryProxy, Error> {
            if self.children_to_fail_for.contains(&child_name) {
                return Err(format_err!("couldn't open {}'s directory :/", &child_name));
            }
            let (local, client) = zx::Channel::create()?;
            match child_name.as_str() {
                BT_RFCOMM_CHILD_NAME => self.bt_rfcomm_channel = Some(local),
                BT_GAP_CHILD_NAME => self.bt_gap_channel = Some(local),
                _ => panic!("MockComponentClient received unexpected child name: {}", child_name),
            }
            Ok(DirectoryProxy::from_channel(AsyncChannel::from_channel(client).unwrap()))
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
    async fn test_open_rfcomm_works() {
        let mut mock_client = MockComponentClient::new();

        // If opening bt-rfcomm's directory works, the directory should be connected to bt-rfcomm.
        let profile_svc = open_childs_service_directory(&mut mock_client).await.unwrap();
        assert!(mock_client.bt_rfcomm_channel.is_some());
        assert_channels_connected(
            mock_client.bt_rfcomm_channel.unwrap().as_ref(),
            profile_svc.as_channel().as_ref(),
        );
    }

    #[fuchsia::test]
    async fn test_open_rfcomm_fails() {
        // If opening bt-rfcomm's directory fails, the directory should be connected to bt-gap.
        let mut mock_client = MockComponentClient::new();
        let _ = mock_client.children_to_fail_for.insert(BT_RFCOMM_CHILD_NAME.to_owned());
        let profile_svc = open_childs_service_directory(&mut mock_client).await.unwrap();
        assert!(mock_client.bt_rfcomm_channel.is_none());
        assert!(mock_client.bt_gap_channel.is_some());
        assert_channels_connected(
            mock_client.bt_gap_channel.unwrap().as_ref(),
            profile_svc.as_channel().as_ref(),
        );
    }

    #[fuchsia::test]
    async fn test_open_rfcomm_and_gap_fail() {
        // If opening both bt-gap and bt-rfcomm's directory fail, opening the profile service should fail.
        let mut mock_client = MockComponentClient::new();
        let _ = mock_client.children_to_fail_for.insert(BT_RFCOMM_CHILD_NAME.to_owned());
        let _ = mock_client.children_to_fail_for.insert(BT_GAP_CHILD_NAME.to_owned());
        assert!(open_childs_service_directory(&mut mock_client).await.is_err());
    }
}
