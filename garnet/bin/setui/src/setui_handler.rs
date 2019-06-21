// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::Adapter;
use crate::fidl_clone::*;
use failure::{format_err, Error};
use fidl_fuchsia_setui::*;
use fuchsia_syslog::fx_log_err;
use futures::channel::oneshot::{channel, Sender};
use futures::prelude::*;
use std::collections::HashMap;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::RwLock;

use fuchsia_async as fasync;

pub type SettingDataMap = Arc<RwLock<HashMap<SettingType, SettingData>>>;

pub struct SetUIHandler {
    // Tracks active adapters.
    adapters: RwLock<HashMap<SettingType, Mutex<Box<dyn Adapter + Send + Sync>>>>,
}

/// SetUIHandler handles all API calls for the service. It is intended to be
/// used as a single instance to service multiple streams.
impl SetUIHandler {
    pub fn new() -> SetUIHandler {
        Self { adapters: RwLock::new(HashMap::new()) }
    }

    /// Adds (or replaces) any existing adapter. Setting type mapping is
    /// determined from the adapter's reported type. Each adapter is used for all requests by any
    /// number of clients.
    pub fn register_adapter(&self, adapter: Box<dyn Adapter + Send + Sync>) {
        self.adapters.write().unwrap().insert(adapter.get_type(), Mutex::new(adapter));
    }

    /// Asynchronous handling of the given stream. Note that we must consider the
    /// possibility of simultaneous active streams.
    pub async fn handle_stream(&self, mut stream: SetUiServiceRequestStream) -> Result<(), Error> {
        // Map of the last setting sent per type through this connection. This map is shared by all
        // watch calls per connection, and ends when the stream ends.
        let last_seen_settings = Arc::new(RwLock::new(HashMap::new()));

        while let Some(req) = await!(stream.try_next())? {
            match req {
                SetUiServiceRequest::Mutate { setting_type, mutation, responder } => {
                    let mut response = self.mutate(setting_type, mutation);
                    responder.send(&mut response)?;
                }
                SetUiServiceRequest::Watch { setting_type, responder } => {
                    self.watch(setting_type, last_seen_settings.clone(), move |data| {
                        responder.send(&mut SettingsObject {
                            setting_type: setting_type,
                            data: data.clone(),
                        })
                    });
                }
                _ => {}
            }
        }

        Ok(())
    }

    /// Returns synchronously after registering a listener with the correct adapter and spawning
    /// a thread to handle the next change.
    pub fn watch<R>(
        &self,
        setting_type: SettingType,
        last_seen_settings: SettingDataMap,
        responder: R,
    ) where
        R: FnOnce(SettingData) -> Result<(), fidl::Error> + Send + 'static,
    {
        let (sender, receiver) = channel();

        if self
            .listen(setting_type, sender, last_seen_settings.read().unwrap().get(&setting_type))
            .is_ok()
        {
            // Creates a new thread per watcher which waits for a change before calling the
            // responder.
            fasync::spawn(async move {
                await!(receiver.map(|data| {
                    if let Ok(data) = data {
                        last_seen_settings.write().unwrap().insert(setting_type, data.clone());
                        responder(data.clone()).ok();
                    }
                }));
            });
        } else {
            fx_log_err!("watch: no valid adapter for type");
        }
    }

    /// The core logic behind listening to a setting is separate from the watch
    /// method definition to facilitate testing.
    fn listen(
        &self,
        setting_type: SettingType,
        sender: Sender<SettingData>,
        last_seen: Option<&SettingData>,
    ) -> Result<(), Error> {
        if let Some(adapter_lock) = self.adapters.read().unwrap().get(&setting_type) {
            // The adapter is locked only for registering the listener. It will not block until the
            // next change.
            if let Ok(adapter) = adapter_lock.lock() {
                adapter.listen(sender, last_seen);
                return Ok(());
            }
        }

        return Err(format_err!("cannot listen. non-existent adapter"));
    }

    /// Applies a mutation, blocking until the mutation completes.
    pub fn mutate(&self, setting_type: SettingType, mutation: Mutation) -> MutationResponse {
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
    use crate::setting_adapter::{MutationHandler, SettingAdapter};

    struct TestStore {}

    impl TestStore {
        fn new() -> TestStore {
            TestStore {}
        }
    }

    impl Store for TestStore {
        fn write(&mut self, _data: SettingData, _sync: bool) -> Result<(), Error> {
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
            MutationHandler { process: &process_string_mutation, check_sync: None },
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

        // Verify request with current value as seen returns nothing.
        {
            let (sender, mut receiver) = channel();
            assert!(handler
                .listen(
                    SettingType::Unknown,
                    sender,
                    Some(&SettingData::StringValue(test_val.clone()))
                )
                .is_ok());
            let listen_result = receiver.try_recv();

            assert!(listen_result.is_ok());
            assert!(listen_result.unwrap() == None);
        }

        // Verify request with no seen value returns current value.
        {
            let (sender, mut receiver) = channel();

            // Listen for change
            assert!(handler.listen(SettingType::Unknown, sender, None).is_ok());

            let listen_result = receiver.try_recv();

            assert!(listen_result.is_ok());

            let data = listen_result.unwrap();

            // Ensure value matches original change.
            match data {
                Some(SettingData::StringValue(string_val)) => {
                    assert_eq!(string_val, test_val.clone());
                }
                _ => {
                    panic!("Did not receive back expected SettingData type");
                }
            }
        }
    }

    /// A test to verify behavior of the account adapter.
    #[test]
    fn test_account() {
        let mut adapter = SettingAdapter::new(
            SettingType::Account,
            Box::new(TestStore::new()),
            MutationHandler { process: &process_account_mutation, check_sync: None },
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
        let (sender, mut receiver) = channel();

        // Ensure initial account settings returned.
        adapter.listen(sender, None);

        let listen_result = receiver.try_recv();
        assert!(listen_result.is_ok());

        let data = listen_result.unwrap();

        match data {
            Some(SettingData::Account(val)) => {
                assert_eq!(val.mode, expected_override);
            }
            _ => {
                panic!("unexpected listen value");
            }
        }
    }
}
