// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{Mocks, SetupTest};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_hardware_power_statecontrol::{AdminRequest, AdminRequestStream, RebootReason};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::lock::Mutex;
use futures::{StreamExt, TryStreamExt};
use std::sync::Arc;
use test_case::test_case;

mod common;

type Action = common::Action;

#[async_trait]
impl Mocks for SetupTest {
    // Mock the power service dependency and verify the settings service interacts with the power
    // dependency by checking the correct actions have been passed.
    async fn hardware_power_statecontrol_service_impl(
        handles: LocalComponentHandles,
        recorded_actions: Arc<Mutex<Vec<Action>>>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let recorded_actions = recorded_actions.clone();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: AdminRequestStream| {
                let recorded_actions_clone = recorded_actions.clone();
                fasync::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        // Support future expansion of FIDL.
                        #[allow(unreachable_patterns)]
                        if let AdminRequest::Reboot {
                            reason: RebootReason::UserRequest,
                            responder,
                        } = req
                        {
                            recorded_actions_clone.lock().await.push(Action::Reboot);
                            responder.send(&mut Ok(())).unwrap();
                        }
                    }
                })
                .detach();
            });
        let _: &mut ServiceFs<_> =
            fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

pub(crate) async fn verify_action_sequence(
    expected_actions: Arc<Mutex<Vec<Action>>>,
    actions: Vec<Action>,
) -> bool {
    let expected_actions = expected_actions.lock().await;
    actions.len() == expected_actions.len()
        && expected_actions.iter().zip(actions.iter()).all(|(action1, action2)| action1 == action2)
}

const REBOOT_ACTION: [Action; 1] = [Action::Reboot];
const NO_ACTIONS: [Action; 0] = [];

#[test_case(true, &REBOOT_ACTION)]
#[test_case(false, &NO_ACTIONS)]
#[fuchsia::test]
async fn test_setup(should_reboot: bool, test_actions: &[Action]) {
    let test_actions = test_actions.to_vec();
    let actions = Arc::new(Mutex::new(Vec::new()));
    let instance =
        SetupTest::create_realm(actions.clone()).await.expect("Failed to set up test realm");
    let setup_service = SetupTest::connect_to_setup_marker(&instance);

    // Ensure retrieved value matches default value.
    let settings = setup_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.enabled_configuration_interfaces,
        Some(fidl_fuchsia_settings::ConfigurationInterfaces::WIFI)
    );

    // Ensure setting interface propagates change correctly.
    let expected_interfaces = fidl_fuchsia_settings::ConfigurationInterfaces::ETHERNET;
    let mut setup_settings = fidl_fuchsia_settings::SetupSettings::EMPTY;
    setup_settings.enabled_configuration_interfaces = Some(expected_interfaces);
    setup_service
        .set(setup_settings, should_reboot)
        .await
        .expect("set completed")
        .expect("set successful");

    // Ensure retrieved value matches set value.
    let settings = setup_service.watch().await.expect("watch completed");
    assert_eq!(settings.enabled_configuration_interfaces, Some(expected_interfaces));

    // Verify expected actions were called.
    assert!(verify_action_sequence(actions, test_actions).await);
}
