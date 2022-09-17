// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_settings::*;
use fidl_fuchsia_settings::{DoNotDisturbProxy, DoNotDisturbSettings};
use fuchsia_component_test::{ChildOptions, RealmBuilder, RealmInstance};
use test_case::test_case;
use utils;

struct DoNotDisturbTest;

impl DoNotDisturbTest {
    pub async fn create_realm() -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec!["fuchsia.settings.DoNotDisturb"],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_do_not_disturb_marker(instance: &RealmInstance) -> DoNotDisturbProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<DoNotDisturbMarker>()
            .expect("Could not connect to DoNotDisturb");
    }
}

const COMPONENT_URL: &str = "#meta/setui_service.cm";

pub struct DoNotDisturbInfo {
    pub user_dnd: Option<bool>,
    pub night_mode_dnd: Option<bool>,
}

impl DoNotDisturbInfo {
    pub(crate) const fn new(user_dnd: bool, night_mode_dnd: bool) -> DoNotDisturbInfo {
        DoNotDisturbInfo { user_dnd: Some(user_dnd), night_mode_dnd: Some(night_mode_dnd) }
    }
}

#[test_case(Some(false), Some(false))]
#[test_case(Some(false), Some(true))]
#[test_case(Some(false), None)]
#[test_case(Some(true), Some(false))]
#[test_case(Some(true), Some(true))]
#[test_case(Some(true), None)]
#[test_case(None, Some(true))]
#[test_case(None, Some(false))]
#[test_case(None, None)]
#[fuchsia::test]
async fn test_do_not_disturb(user_dnd: Option<bool>, night_mode_dnd: Option<bool>) {
    let instance = DoNotDisturbTest::create_realm().await.expect("setting up test realm");

    // Verify the default values, then set the passed values.
    {
        let proxy = DoNotDisturbTest::connect_to_do_not_disturb_marker(&instance);
        verify_dnd_watch(&proxy, DoNotDisturbInfo::new(false, false)).await;
        set_dnd(&proxy, user_dnd, night_mode_dnd).await;
    }

    let user_dnd_inner = if let Some(user_dnd_inner) = user_dnd {
        user_dnd_inner
    } else {
        // If None is passed, use whatever the existing value was. Since this is a
        // new realm, it defaults to false.
        false
    };

    let night_mode_dnd_inner = if let Some(night_mode_dnd_inner) = night_mode_dnd {
        night_mode_dnd_inner
    } else {
        // If None is passed, use whatever the existing value was. Since this is a
        // new realm, it defaults to false.
        false
    };

    // Verify that a new connection gets the updated values.
    {
        let proxy = DoNotDisturbTest::connect_to_do_not_disturb_marker(&instance);
        verify_dnd_watch(&proxy, DoNotDisturbInfo::new(user_dnd_inner, night_mode_dnd_inner)).await;
    }

    let _ = instance.destroy().await;
}

async fn set_dnd(
    dnd_proxy: &DoNotDisturbProxy,
    user_dnd: Option<bool>,
    night_mode_dnd: Option<bool>,
) {
    let mut dnd_settings = DoNotDisturbSettings::EMPTY;
    dnd_settings.user_initiated_do_not_disturb = user_dnd;
    dnd_settings.night_mode_initiated_do_not_disturb = night_mode_dnd;
    dnd_proxy.set(dnd_settings).await.expect("set completed").expect("set successful");
}

async fn verify_dnd_watch(dnd_proxy: &DoNotDisturbProxy, expected_dnd: DoNotDisturbInfo) {
    let settings = dnd_proxy.watch().await.expect("watch completed");
    assert_eq!(settings.user_initiated_do_not_disturb, expected_dnd.user_dnd);
    assert_eq!(settings.night_mode_initiated_do_not_disturb, expected_dnd.night_mode_dnd);
}
