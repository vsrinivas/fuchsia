// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
  crate::create_fidl_service, crate::registry::device_storage::testing::*,
  crate::service_context::ServiceContext, crate::switchboard::base::SettingType,
  fidl_fuchsia_setui::*, fuchsia_async as fasync, fuchsia_component::server::ServiceFs,
  futures::prelude::*, parking_lot::RwLock, std::sync::Arc,
};

const ENV_NAME: &str = "setui_service_system_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn tests_setui() {
  const STARTING_LOGIN_MODE: fidl_fuchsia_setui::LoginOverride =
    fidl_fuchsia_setui::LoginOverride::None;
  const CHANGED_LOGIN_MODE: fidl_fuchsia_setui::LoginOverride =
    fidl_fuchsia_setui::LoginOverride::AuthProvider;
  let mut fs = ServiceFs::new();

  create_fidl_service(
    fs.root_dir(),
    [SettingType::System].iter().cloned().collect(),
    Arc::new(RwLock::new(ServiceContext::new(None))),
    Box::new(InMemoryStorageFactory::create()),
  );

  let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
  fasync::spawn(fs.collect());

  let system_proxy = env.connect_to_service::<SetUiServiceMarker>().unwrap();

  let settings_obj =
    system_proxy.watch(fidl_fuchsia_setui::SettingType::Account).await.expect("watch completed");

  if let SettingData::Account(data) = settings_obj.data {
    assert_eq!(data.mode, Some(STARTING_LOGIN_MODE));
  } else {
    panic!("Should have had account data");
  }

  let mut mutation = Mutation::AccountMutationValue(AccountMutation {
    operation: Some(AccountOperation::SetLoginOverride),
    login_override: Some(CHANGED_LOGIN_MODE),
  });

  system_proxy
    .mutate(fidl_fuchsia_setui::SettingType::Account, &mut mutation)
    .await
    .expect("mutate completed");

  let updated_settings_obj =
    system_proxy.watch(fidl_fuchsia_setui::SettingType::Account).await.expect("watch completed");

  if let SettingData::Account(data) = updated_settings_obj.data {
    assert_eq!(data.mode, Some(CHANGED_LOGIN_MODE));
  } else {
    panic!("Should have had account data");
  }
}
