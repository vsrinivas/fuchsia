// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::PrivacyTest;
mod common;

struct PrivacyInfo {
    pub user_data_sharing_consent: Option<bool>,
}

#[fuchsia::test]
async fn test_privacymarker() {
    let instance = PrivacyTest::create_realm().await.expect("setting up test realm");
    let initial_value = PrivacyInfo { user_data_sharing_consent: None };
    let changed_value = PrivacyInfo { user_data_sharing_consent: Some(true) };

    {
        let proxy = PrivacyTest::connect_to_privacymarker(&instance);

        // Make a watch call.
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings.user_data_sharing_consent, initial_value.user_data_sharing_consent);

        // Ensure setting interface propagates correctly
        let mut privacy_settings = fidl_fuchsia_settings::PrivacySettings::EMPTY;
        privacy_settings.user_data_sharing_consent = Some(true);
        proxy.set(privacy_settings).await.expect("set completed").expect("set successful");
    }

    {
        let proxy = PrivacyTest::connect_to_privacymarker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings.user_data_sharing_consent, changed_value.user_data_sharing_consent);
    }

    let _ = instance.destroy().await;
}
