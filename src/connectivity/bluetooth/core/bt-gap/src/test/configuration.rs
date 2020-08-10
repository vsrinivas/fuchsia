// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_host::{HostRequest, HostRequestStream, HostSetConnectableResponder},
    fidl_fuchsia_bluetooth_sys::{
        self as sys, ConfigurationMarker, ConfigurationProxy, ConfigurationRequestStream,
        LeSecurityMode,
    },
    fuchsia_bluetooth::types::HostId,
    futures::{future, stream::TryStreamExt},
    matches::assert_matches,
};

use crate::{
    host_dispatcher::{test as hd_test, HostDispatcher},
    services::configuration,
    types,
};

#[cfg(test)]
fn setup_configuration_test() -> types::Result<(
    HostRequestStream,
    HostDispatcher,
    ConfigurationProxy,
    ConfigurationRequestStream,
)> {
    let dispatcher = hd_test::make_simple_test_dispatcher()?;
    let host_server = hd_test::create_and_add_test_host_to_dispatcher(HostId(42), &dispatcher)?;
    let (client, server) =
        fidl::endpoints::create_proxy_and_stream::<ConfigurationMarker>().unwrap();
    Ok((host_server, dispatcher, client, server))
}

#[cfg(test)]
macro_rules! handle_host_req_fut {
    ($host_server:ident, $host_request_variant:ident, $handle_request_payload:tt $(, $req_param_name:tt)*) => {
        async move {
            $host_server.try_for_each(move |req| {
                if let HostRequest::$host_request_variant { $($req_param_name,)* .. } = req {
                    $handle_request_payload($($req_param_name),*);
                } else {
                    panic!("unexpected request!");
                }
                future::ok(())
            })
            .await
            .map_err(|e| e.into())
        }
    };
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_le_privacy() {
    let (host_server, dispatcher, config_client, server) = setup_configuration_test().unwrap();
    let run_configuration = configuration::run(dispatcher, server);
    let make_request = async move {
        let response = config_client
            .update(sys::Settings { le_privacy: Some(false), ..sys::Settings::new_empty() })
            .await;
        assert_matches!(response, Ok(sys::Settings { le_privacy: Some(false), .. }));
        // The configuration client is dropped when this terminates, which causes the configuration
        // stream to terminate. This causes run_configuration to terminate which drops the host
        // dispatcher, which closes the host channel and finally causes run_host to terminate
        Ok(())
    };

    let run_host = handle_host_req_fut!(
        host_server,
        EnablePrivacy,
        (|enabled: bool| assert!(!enabled)),
        enabled
    );

    future::try_join3(make_request, run_host, run_configuration).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_le_background_scan() {
    let (host_server, dispatcher, config_client, server) = setup_configuration_test().unwrap();
    let run_configuration = configuration::run(dispatcher, server);
    let make_request = async move {
        let response = config_client
            .update(sys::Settings { le_background_scan: Some(false), ..sys::Settings::new_empty() })
            .await;
        assert_matches!(response, Ok(sys::Settings { le_background_scan: Some(false), .. }));
        Ok(())
    };

    let run_host = handle_host_req_fut!(
        host_server,
        EnableBackgroundScan,
        (|enabled: bool| assert!(!enabled)),
        enabled
    );

    future::try_join3(make_request, run_host, run_configuration).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn disable_connectable_mode() {
    let (host_server, dispatcher, config_client, server) = setup_configuration_test().unwrap();
    let run_configuration = configuration::run(dispatcher, server);
    let make_request = async move {
        let response = config_client
            .update(sys::Settings {
                bredr_connectable_mode: Some(false),
                ..sys::Settings::new_empty()
            })
            .await;
        assert_matches!(response, Ok(sys::Settings { bredr_connectable_mode: Some(false), .. }));
        Ok(())
    };

    let run_host = handle_host_req_fut!(
        host_server,
        SetConnectable,
        (|enabled: bool, responder: HostSetConnectableResponder| {
            assert!(!enabled);
            assert_matches!(responder.send(&mut Ok(())), Ok(()))
        }),
        enabled,
        responder
    );

    future::try_join3(make_request, run_host, run_configuration).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn set_secure_connections_only() {
    let (host_server, dispatcher, config_client, server) = setup_configuration_test().unwrap();
    let run_configuration = configuration::run(dispatcher, server);
    let make_request = async move {
        let response = config_client
            .update(sys::Settings {
                le_security_mode: Some(LeSecurityMode::SecureConnectionsOnly),
                ..sys::Settings::new_empty()
            })
            .await;
        assert_matches!(
            response,
            Ok(sys::Settings {
                le_security_mode: Some(LeSecurityMode::SecureConnectionsOnly), ..
            })
        );
        Ok(())
    };

    let run_host = handle_host_req_fut!(
        host_server,
        SetLeSecurityMode,
        (|mode: LeSecurityMode| assert_eq!(LeSecurityMode::SecureConnectionsOnly, mode)),
        le_security_mode
    );

    future::try_join3(make_request, run_host, run_configuration).await.unwrap();
}

#[fuchsia_async::run_singlethreaded(test)]
async fn configure_applies_to_multiple_devices() {
    // `setup_configuration_test` adds the first host, and in this test we add a second
    let (host1_server, dispatcher, config_client, server) = setup_configuration_test().unwrap();
    let host1_info = dispatcher.get_active_host_info().unwrap();
    let host2_id = HostId(host1_info.id.0 + 1);
    let host2_server =
        hd_test::create_and_add_test_host_to_dispatcher(host2_id, &dispatcher).unwrap();

    let run_configuration = configuration::run(dispatcher, server);
    let make_request = async move {
        let response = config_client
            .update(sys::Settings { le_privacy: Some(false), ..sys::Settings::new_empty() })
            .await;
        assert_matches!(response, Ok(sys::Settings { le_privacy: Some(false), .. }));
        // The configuration client is dropped when this terminates, which causes the configuration
        // stream to terminate.
        Ok(())
    };

    let run_host1 = handle_host_req_fut!(
        host1_server,
        EnablePrivacy,
        (|enabled: bool| assert!(!enabled)),
        enabled
    );
    let run_host2 = handle_host_req_fut!(
        host2_server,
        EnablePrivacy,
        (|enabled: bool| assert!(!enabled)),
        enabled
    );

    future::try_join4(run_configuration, make_request, run_host1, run_host2).await.unwrap();
}
