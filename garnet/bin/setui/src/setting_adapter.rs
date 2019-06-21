// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use crate::fidl_clone::*;
use fidl_fuchsia_setui::*;
use fuchsia_syslog::fx_log_err;
use futures::channel::oneshot::Sender;
use std::sync::Mutex;

pub struct MutationHandler {
    pub process: &'static ProcessMutation,
    pub check_sync: Option<&'static CheckSync>,
}

/// SettingAdapter provides a basic implementation of the Adapter trait,
/// handling callbacks for hanging get interactions and keeping track of the
/// latest values. Users of this implementation can provide a callback for
/// processing mutations.
pub struct SettingAdapter {
    setting_type: SettingType,
    latest_val: Option<SettingData>,
    senders: Mutex<Vec<Sender<SettingData>>>,
    mutation_handler: MutationHandler,
    store: Mutex<BoxedStore>,
}

impl SettingAdapter {
    pub fn new(
        setting_type: SettingType,
        store: BoxedStore,
        mutation_handler: MutationHandler,
        default_value: Option<SettingData>,
    ) -> SettingAdapter {
        let mut adapter = SettingAdapter {
            setting_type: setting_type,
            latest_val: default_value,
            senders: Mutex::new(vec![]),
            mutation_handler: mutation_handler,
            store: Mutex::new(store),
        };
        adapter.initialize();

        return adapter;
    }

    fn initialize(&mut self) {
        if let Ok(store) = self.store.lock() {
            if let Ok(Some(value)) = store.read() {
                self.latest_val = Some(value);
            }
        }
    }
}

impl Adapter for SettingAdapter {
    fn get_type(&self) -> SettingType {
        return self.setting_type;
    }

    fn mutate(&mut self, mutation: &fidl_fuchsia_setui::Mutation) -> MutationResponse {
        let result = (self.mutation_handler.process)(mutation);

        match result {
            Ok(Some(setting)) => {
                self.latest_val = Some(setting.clone());

                if let Ok(mut store) = self.store.lock() {
                    if store
                        .write(
                            setting.clone(),
                            match self.mutation_handler.check_sync {
                                Some(check) => (check)(mutation),
                                _ => false,
                            },
                        )
                        .is_err()
                    {
                        fx_log_err!("failed to write setting to file");
                    }
                }

                if let Ok(mut senders) = self.senders.lock() {
                    while !senders.is_empty() {
                        let sender_option = senders.pop();

                        if let Some(sender) = sender_option {
                            sender.send(setting.clone()).ok();
                        }
                    }
                }
            }
            Ok(None) => {
                self.latest_val = None;
            }
            _ => {
                return MutationResponse { return_code: ReturnCode::Failed };
            }
        }

        return MutationResponse { return_code: ReturnCode::Ok };
    }

    /// Listen
    fn listen(&self, sender: Sender<SettingData>, last_seen_data: Option<&SettingData>) {
        if let Some(ref value) = self.latest_val {
            let mut should_send = last_seen_data == None;

            if let Some(data) = last_seen_data {
                should_send = data != value;
            }

            if should_send {
                sender.send(value.clone()).ok();
                return;
            }
        }

        if let Ok(mut senders) = self.senders.lock() {
            senders.push(sender);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::common::Store;
    use crate::setting_adapter::{MutationHandler, SettingAdapter};
    use failure::Error;
    use std::sync::{Arc, RwLock};

    struct WrittenData {
        sync: bool,
    }

    struct TestStore {
        written_data: Arc<RwLock<Vec<WrittenData>>>,
    }

    impl TestStore {
        fn new(written_data: Arc<RwLock<Vec<WrittenData>>>) -> TestStore {
            return TestStore { written_data: written_data };
        }
    }

    impl Store for TestStore {
        fn write(&mut self, _data: SettingData, sync: bool) -> Result<(), Error> {
            self.written_data.write().unwrap().push(WrittenData { sync: sync });
            Ok(())
        }

        fn read(&self) -> Result<Option<SettingData>, Error> {
            Ok(None)
        }
    }

    fn mock_process_mutation(_mutation: &Mutation) -> Result<Option<SettingData>, Error> {
        return Ok(Some(SettingData::StringValue("Foo".to_string())));
    }

    /// A basic test to exercise that basic functionality works. In this case, we
    /// mutate the unknown type, reserved for testing. We should always immediately
    /// receive back an Ok response.
    #[test]
    fn test_write_sync() {
        verify_sync(false);
        verify_sync(true);
    }

    fn verify_sync(should_sync: bool) {
        let written_data: Arc<RwLock<Vec<WrittenData>>> = Arc::new(RwLock::new(Vec::new()));

        let mut adapter = SettingAdapter::new(
            SettingType::Account,
            Box::new(TestStore::new(written_data.clone())),
            MutationHandler {
                process: &mock_process_mutation,
                check_sync: if should_sync { Some(&|_mutation| -> bool { true }) } else { None },
            },
            None,
        );

        adapter.mutate(&Mutation::StringMutationValue(StringMutation {
            operation: StringOperation::Update,
            value: "Bar".to_string(),
        }));

        let data = written_data.read().unwrap();
        assert_eq!(data.len(), 1);
        assert!(data[0].sync == should_sync);
    }
}
