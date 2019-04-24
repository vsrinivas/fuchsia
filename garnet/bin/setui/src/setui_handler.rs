// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::Adapter;
use crate::fidl_clone::*;
use failure::{format_err, Error};
use fidl_fuchsia_setui::*;
use fuchsia_syslog::fx_log_err;
use futures::prelude::*;
use std::collections::HashMap;
use std::sync::mpsc::{channel, Sender};
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::RwLock;

use fuchsia_async as fasync;

pub struct SetUIHandler {
    // Tracks active adapters.
    adapters: RwLock<HashMap<SettingType, Mutex<Box<dyn Adapter + Send + Sync>>>>,
    // Provides bookkeeping for last value transmitted by a callback. Asynchronous
    // to allow sharing between all callback closures.
    last_seen_settings: Arc<RwLock<HashMap<SettingType, SettingData>>>,
}

/// SetUIHandler handles all API calls for the service. It is intended to be
/// used as a single instance to service multiple streams.
impl SetUIHandler {
    pub fn new() -> SetUIHandler {
        Self {
            adapters: RwLock::new(HashMap::new()),
            last_seen_settings: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Adds (or replaces) any existing adapter. Setting type mapping is
    /// determined from the adapter's reported type.
    pub fn register_adapter(&self, adapter: Box<dyn Adapter + Send + Sync>) {
        self.adapters.write().unwrap().insert(adapter.get_type(), Mutex::new(adapter));
    }

    /// Asynchronous handling of the given stream. Note that we must consider the
    /// possibility of simultaneous active streams.
    pub async fn handle_stream(&self, mut stream: SetUiServiceRequestStream) -> Result<(), Error> {
        // A single StreamSetting watcher is created for the lifetime of the stream.
        // It is used for asynchronously processing updates to a setting type and
        // tracking the current values.

        while let Some(req) = await!(stream.try_next())? {
            match req {
                SetUiServiceRequest::Mutate { setting_type, mutation, responder } => {
                    let mut response = self.mutate(setting_type, mutation);
                    responder.send(&mut response)?;
                }
                SetUiServiceRequest::Watch { setting_type, responder } => {
                    self.watch(setting_type, responder);
                }
                _ => {}
            }
        }

        Ok(())
    }

    fn watch(&self, setting_type: SettingType, responder: SetUiServiceWatchResponder) {
        let (sender, receiver) = channel();

        if self.listen(setting_type, sender).is_ok() {
            // We clone here so the value can be moved into the closure below.
            let last_seen_settings_clone = self.last_seen_settings.clone();

            fasync::spawn(
                async move {
                    let data: SettingData = receiver.recv().unwrap();
                    last_seen_settings_clone.write().unwrap().insert(setting_type, data.clone());
                    responder
                        .send(&mut SettingsObject {
                            setting_type: setting_type,
                            data: data.clone(),
                        })
                        .ok();
                },
            );
        } else {
            fx_log_err!("watch: no valid adapter for type");
        }
    }

    /// The core logic behind listening to a setting is separate from the watch
    /// method definition to facilitate testing.
    fn listen(&self, setting_type: SettingType, sender: Sender<SettingData>) -> Result<(), Error> {
        if let Some(adapter_lock) = self.adapters.read().unwrap().get(&setting_type) {
            if let Ok(adapter) = adapter_lock.lock() {
                adapter.listen(sender, self.last_seen_settings.read().unwrap().get(&setting_type));
                return Ok(());
            }
        }

        return Err(format_err!("cannot listen. non-existent adapter"));
    }

    /// Applies a mutation
    fn mutate(&self, setting_type: SettingType, mutation: Mutation) -> MutationResponse {
        if let Some(adapter_lock) = self.adapters.read().unwrap().get(&setting_type) {
            if let Ok(mut adapter) = adapter_lock.lock() {
                adapter.mutate(&mutation);
            }
        }

        MutationResponse { return_code: ReturnCode::Ok }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::Store;
    use crate::mutation::*;
    use crate::setting_adapter::SettingAdapter;

    struct TestStore {}

    impl TestStore {
        fn new() -> TestStore {
            TestStore {}
        }
    }

    impl Store for TestStore {
        fn write(&self, _data: SettingData) -> Result<(), Error> {
            Ok(())
        }

        fn read(&self) -> Result<Option<SettingData>, Error> {
            Ok(None)
        }
    }

    /// A basic test to exercise that basic functionality works. In this case, we
    /// mutate the unknown type, reserved for testing. We should always immediately
    /// receive back an Ok response.
    #[test]
    fn test_ok() {
        let handler = SetUIHandler::new();
        let string_mutation =
            StringMutation { operation: StringOperation::Update, value: "Hi".to_string() };

        let result = handler.mutate(
            SettingType::Unknown,
            fidl_fuchsia_setui::Mutation::StringMutationValue(string_mutation),
        );

        assert_eq!(result, MutationResponse { return_code: ReturnCode::Ok });
    }

    /// A test to verify the listen functionality. Note that we use the listen
    /// method directly on the handler as we do not mock the FIDL interface.
    #[test]
    fn test_listen() {
        // Test value to ensure
        let test_val = "FooBar".to_string();
        // Create handler and register test adapter
        let handler = SetUIHandler::new();
        handler.register_adapter(Box::new(SettingAdapter::new(
            SettingType::Unknown,
            Box::new(TestStore::new()),
            Box::new(process_string_mutation),
            None,
        )));

        // Introduce change to the adapter.
        assert_eq!(
            handler.mutate(
                SettingType::Unknown,
                fidl_fuchsia_setui::Mutation::StringMutationValue(StringMutation {
                    operation: StringOperation::Update,
                    value: test_val.clone()
                }),
            ),
            MutationResponse { return_code: ReturnCode::Ok }
        );

        let (sender, receiver) = channel();

        // Listen for change
        assert!(handler.listen(SettingType::Unknown, sender).is_ok());

        let listen_result = receiver.recv();

        assert!(listen_result.is_ok());

        let data = listen_result.unwrap();

        // Ensure value matches original change.
        match data {
            SettingData::StringValue(string_val) => {
                assert_eq!(string_val, test_val.clone());
            }
            _ => {
                panic!("Did not receive back expected SettingData type");
            }
        }
    }

    /// A test to verify behavior of the account adapter.
    #[test]
    fn test_account() {
        let mut adapter = SettingAdapter::new(
            SettingType::Account,
            Box::new(TestStore::new()),
            Box::new(process_account_mutation),
            Some(SettingData::Account(AccountSettings { mode: None })),
        );

        check_login_override(&adapter, None);

        let override_update = Some(LoginOverride::AutologinGuest);

        adapter.mutate(&Mutation::AccountMutationValue(AccountMutation {
            operation: Some(AccountOperation::SetLoginOverride),
            login_override: override_update,
        }));

        check_login_override(&adapter, override_update);
    }

    fn check_login_override(adapter: &Adapter, expected_override: Option<LoginOverride>) {
        let (sender, receiver) = channel();

        // Ensure initial account settings returned.
        adapter.listen(sender, None);

        let listen_result = receiver.recv();
        assert!(listen_result.is_ok());

        let data = listen_result.unwrap();

        match data {
            SettingData::Account(val) => {
                assert_eq!(val.mode, expected_override);
            }
            _ => {
                panic!("unexpected listen value");
            }
        }
    }
}
