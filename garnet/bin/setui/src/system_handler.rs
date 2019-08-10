// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::setui_handler::{SetUIHandler, SettingDataMap};
use failure::Error;
use fidl_fuchsia_setui::*;
use futures::prelude::*;
use std::collections::HashMap;
use std::sync::Arc;
use std::sync::RwLock;

/// Handles FIDL calls from system settings.
pub struct SystemStreamHandler {
    setui_handler: Arc<SetUIHandler>,
}

impl SystemStreamHandler {
    pub fn new(handler: Arc<SetUIHandler>) -> SystemStreamHandler {
        Self { setui_handler: handler }
    }

    pub async fn handle_system_stream(
        &self,
        mut stream: fidl_fuchsia_settings::SystemRequestStream,
    ) -> Result<(), Error> {
        // Map of the last setting sent per type through this connection. This map is shared by all
        // watch calls per connection, and ends when the stream ends.
        let last_seen_settings = Arc::new(RwLock::new(HashMap::new()));

        while let Some(req) = stream.try_next().await? {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                fidl_fuchsia_settings::SystemRequest::Set { settings, responder } => {
                    if let Some(login_override) = settings.mode {
                        self.set_login_override(login_override, |mut result| {
                            responder.send(&mut result)
                        })?;
                    } else {
                        panic!("No operation requested");
                    }
                }
                fidl_fuchsia_settings::SystemRequest::Watch { responder } => {
                    self.watch(last_seen_settings.clone(), |mut result| {
                        responder.send(&mut result)
                    })?;
                }
                _ => {}
            }
        }

        Ok(())
    }

    fn set_login_override<R>(
        &self,
        login_override: fidl_fuchsia_settings::LoginOverride,
        responder: R,
    ) -> Result<(), Error>
    where
        R: FnOnce(Result<(), fidl_fuchsia_settings::Error>) -> Result<(), fidl::Error>
            + Send
            + 'static,
    {
        let response = self.setui_handler.mutate(
            SettingType::Account,
            Mutation::AccountMutationValue(AccountMutation {
                operation: Some(AccountOperation::SetLoginOverride),
                login_override: Some(convert_login_override(login_override)),
            }),
        );
        if response.return_code == ReturnCode::Ok {
            responder(Ok(()))?;
        } else {
            responder(Err(fidl_fuchsia_settings::Error::Failed))?;
        }
        Ok(())
    }

    fn watch<R>(&self, last_seen_settings: SettingDataMap, responder: R) -> Result<(), Error>
    where
        R: FnOnce(
                Result<fidl_fuchsia_settings::SystemSettings, fidl_fuchsia_settings::Error>,
            ) -> Result<(), fidl::Error>
            + Send
            + 'static,
    {
        self.setui_handler.watch(SettingType::Account, last_seen_settings, |data| {
            if let SettingData::Account(account_settings) = data {
                if let Some(mode) = account_settings.mode {
                    let mut settings = fidl_fuchsia_settings::SystemSettings::empty();
                    settings.mode = Some(convert_setui_login_override(mode));
                    return responder(Ok(settings));
                }
            }
            responder(Err(fidl_fuchsia_settings::Error::Failed))
        });
        Ok(())
    }
}

fn convert_login_override(login_override: fidl_fuchsia_settings::LoginOverride) -> LoginOverride {
    match login_override {
        fidl_fuchsia_settings::LoginOverride::AutologinGuest => LoginOverride::AutologinGuest,
        fidl_fuchsia_settings::LoginOverride::AuthProvider => LoginOverride::AuthProvider,
        _ => LoginOverride::None,
    }
}

fn convert_setui_login_override(
    login_override: LoginOverride,
) -> fidl_fuchsia_settings::LoginOverride {
    match login_override {
        LoginOverride::AutologinGuest => fidl_fuchsia_settings::LoginOverride::AutologinGuest,
        LoginOverride::AuthProvider => fidl_fuchsia_settings::LoginOverride::AuthProvider,
        _ => fidl_fuchsia_settings::LoginOverride::None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::Store;
    use crate::fidl_clone::*;

    use crate::mutation::*;
    use crate::setting_adapter::{MutationHandler, SettingAdapter};
    use futures::channel::oneshot::{channel, Sender};
    struct TestStore {
        data: Option<SettingData>,
    }

    impl TestStore {
        fn new() -> TestStore {
            return TestStore { data: None };
        }
    }

    impl Store for TestStore {
        fn write(&mut self, data: SettingData, _sync: bool) -> Result<(), Error> {
            self.data = Some(data);
            Ok(())
        }

        fn read(&self) -> Result<Option<SettingData>, Error> {
            match &self.data {
                Some(data) => Ok(Some(data.clone())),
                None => Ok(None),
            }
        }
    }

    fn set_up(login_mode: fidl_fuchsia_setui::LoginOverride) -> SystemStreamHandler {
        let handler = Arc::new(SetUIHandler::new());

        handler.register_adapter(Box::new(SettingAdapter::new(
            SettingType::Account,
            Box::new(TestStore::new()),
            MutationHandler {
                process: &process_account_mutation,
                check_sync: Some(&should_sync_account_mutation),
            },
            Some(SettingData::Account(AccountSettings { mode: Some(login_mode) })),
        )));

        SystemStreamHandler::new(handler)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_set_login_override() {
        let handler = set_up(LoginOverride::None);

        // Check initial value
        let last_seen_settings = Arc::new(RwLock::new(HashMap::new()));
        let (sender, receiver) = channel();
        expect_watch(
            &handler,
            last_seen_settings.clone(),
            sender,
            fidl_fuchsia_settings::LoginOverride::None,
        );
        let result = receiver.await;
        assert!(result.is_ok());

        // Set login override
        let (sender, receiver) = channel();
        let result = handler.set_login_override(
            fidl_fuchsia_settings::LoginOverride::AutologinGuest,
            |_| {
                let result = sender.send(());
                assert!(result.is_ok());
                Ok(())
            },
        );
        assert!(result.is_ok());
        let result = receiver.await;
        assert!(result.is_ok());

        // Check new value
        let last_seen_settings = Arc::new(RwLock::new(HashMap::new()));
        let (sender, receiver) = channel();
        expect_watch(
            &handler,
            last_seen_settings.clone(),
            sender,
            fidl_fuchsia_settings::LoginOverride::AutologinGuest,
        );
        let result = receiver.await;
        assert!(result.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_delayed_watch() {
        let handler = set_up(LoginOverride::AutologinGuest);

        // Send once to get current value
        let last_seen_settings = Arc::new(RwLock::new(HashMap::new()));
        let (sender, receiver) = channel();
        expect_watch(
            &handler,
            last_seen_settings.clone(),
            sender,
            fidl_fuchsia_settings::LoginOverride::AutologinGuest,
        );
        let result = receiver.await;
        assert!(result.is_ok());

        // watch again to hang the watch callback.
        let (sender, receiver) = channel();
        expect_watch(
            &handler,
            last_seen_settings.clone(),
            sender,
            fidl_fuchsia_settings::LoginOverride::AuthProvider,
        );

        // change to auth provider
        let (mutate_sender, mutate_receiver) = channel();
        let result =
            handler.set_login_override(fidl_fuchsia_settings::LoginOverride::AuthProvider, |_| {
                let result = mutate_sender.send(());
                assert!(result.is_ok());
                Ok(())
            });
        assert!(result.is_ok());
        let result = mutate_receiver.await;
        assert!(result.is_ok());

        // should change here
        let result = receiver.await;
        assert!(result.is_ok());
    }

    fn expect_watch(
        handler: &SystemStreamHandler,
        last_seen_settings: SettingDataMap,
        sender: Sender<()>,
        login_mode: fidl_fuchsia_settings::LoginOverride,
    ) {
        let result = handler.watch(last_seen_settings, move |result| {
            assert!(result.is_ok());
            match result {
                Ok(value) => {
                    assert_eq!(value.mode, Some(login_mode));
                }
                _ => {}
            }
            let result = sender.send(());
            assert!(result.is_ok());
            Ok(())
        });
        assert!(result.is_ok());
    }

}
