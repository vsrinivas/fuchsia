// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl::{
    endpoints::{DiscoverableProtocolMarker, RequestStream},
    AsHandleRef as _,
};
use fidl_fuchsia_netemul as fnetemul;
use fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj};
use fuchsia_zircon as zx;

use futures::{StreamExt as _, TryStreamExt as _};
use netemul::{TestRealm, TestSandbox};
use netstack_testing_common::realms::NetstackVersion;

const MOCK_SERVICES_NAME: &str = "mock";

fn create_netstack_with_mock_endpoint<'s, RS: RequestStream + 'static>(
    sandbox: &'s TestSandbox,
    service_name: String,
    name: &'s str,
) -> (TestRealm<'s>, ServiceFs<ServiceObj<'s, RS>>)
where
    RS::Protocol: DiscoverableProtocolMarker,
{
    let mut netstack: fnetemul::ChildDef =
        (&netstack_testing_common::realms::KnownServiceProvider::Netstack(
            NetstackVersion::ProdNetstack2,
        ))
            .into();
    {
        let fnetemul::ChildUses::Capabilities(capabilities) =
            netstack.uses.as_mut().expect("empty uses");
        capabilities.push(fnetemul::Capability::ChildDep(fnetemul::ChildDep {
            name: Some(MOCK_SERVICES_NAME.to_string()),
            capability: Some(fnetemul::ExposedCapability::Protocol(service_name.clone())),
            ..fnetemul::ChildDep::EMPTY
        }));
    }

    let (mock_dir, server_end) = fidl::endpoints::create_endpoints().expect("create endpoints");
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> = fs.dir("svc").add_fidl_service_at(service_name, |rs: RS| rs);
    let _: &mut ServiceFs<_> = fs.serve_connection(server_end).expect("serve connection");

    let realm = sandbox
        .create_realm(
            name,
            [
                netstack,
                (&netstack_testing_common::realms::KnownServiceProvider::SecureStash).into(),
                fnetemul::ChildDef {
                    source: Some(fnetemul::ChildSource::Mock(mock_dir)),
                    name: Some(MOCK_SERVICES_NAME.to_string()),
                    ..fnetemul::ChildDef::EMPTY
                },
            ],
        )
        .expect("failed to create realm");

    // Connect to any service to get netstack launched.
    let _: fidl_fuchsia_net_interfaces::StateProxy = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("connect to protocol");

    (realm, fs)
}

#[fuchsia::test]
async fn ns2_sets_thread_profiles() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_realm, fs) =
        create_netstack_with_mock_endpoint::<fidl_fuchsia_scheduler::ProfileProviderRequestStream>(
            &sandbox,
            fidl_fuchsia_scheduler::ProfileProviderMarker::PROTOCOL_NAME.to_string(),
            "ns2_sets_thread_profiles",
        );

    // And expect that we'll see a connection to profile provider.
    let (thread, profile, responder) = fs
        .flatten()
        .try_next()
        .await
        .expect("fs failure")
        .expect("fs terminated unexpectedly")
        .into_set_profile_by_role()
        .expect("unexpected request");
    assert_eq!(profile, "fuchsia.netstack.go-worker");
    assert_eq!(
        thread.basic_info().expect("failed to get basic info").rights,
        zx::Rights::TRANSFER | zx::Rights::MANAGE_THREAD
    );
    responder.send(zx::Status::OK.into_raw()).expect("failed to respond");
}

#[fuchsia::test]
async fn ns2_requests_inspect_persistence() {
    let persist_path = format!(
        "{}-netstack",
        fidl_fuchsia_diagnostics_persist::DataPersistenceMarker::PROTOCOL_NAME
    );

    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let (_realm, fs) = create_netstack_with_mock_endpoint::<
        fidl_fuchsia_diagnostics_persist::DataPersistenceRequestStream,
    >(&sandbox, persist_path, "ns2_requests_persistence");

    // And expect that we'll see a connection to profile provider.
    let (tag, responder) = fs
        .flatten()
        .try_next()
        .await
        .expect("fs failure")
        .expect("fs terminated unexpectedly")
        .into_persist()
        .expect("unexpected request");

    assert_eq!(tag, "netstack-counters");
    responder
        .send(fidl_fuchsia_diagnostics_persist::PersistResult::Queued)
        .expect("failed to respond");
}
