// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_stash::*;
use futures::lock::Mutex;
use serde::de::DeserializeOwned;
use serde::Serialize;
use std::sync::Arc;

const SETTINGS_PREFIX: &str = "settings";

/// Stores device level settings in persistent storage.
/// User level settings should not use this.
pub struct DeviceStorage<T> {
    current_data: Option<T>,
    stash_proxy: StoreAccessorProxy,
}

/// Structs that can be stored in device storage should derive the Serialize, Deserialize, and
/// Copy traits, as well as provide constants.
/// KEY should be unique the struct, usually the name of the struct itself.
/// DEFAULT_VALUE will be the value returned when nothing has yet been stored.
///
/// Anything that implements this should not introduce breaking changes with the same key.
/// Clients that want to make a breaking change should create a new structure with a new key and
/// implement conversion/cleanup logic. Adding optional fields to a struct is not breaking, but
/// removing fields, renaming fields, or adding non-optional fields are.
pub trait DeviceStorageCompatible: Serialize + DeserializeOwned + Copy {
    const DEFAULT_VALUE: Self;
    const KEY: &'static str;
}

impl<T: DeviceStorageCompatible> DeviceStorage<T> {
    pub fn write(&mut self, new_value: T) -> Result<(), Error> {
        self.current_data = Some(new_value);
        let mut serialized = Value::Stringval(serde_json::to_string(&new_value).unwrap());
        self.stash_proxy.set_value(&prefixed(T::KEY), &mut serialized)?;
        self.stash_proxy.commit()?;
        Ok(())
    }

    /// Gets the latest value cached locally, or loads the value from storage.
    /// Doesn't support multiple concurrent callers of the same struct.
    pub async fn get(&mut self) -> T {
        if let None = self.current_data {
            if let Some(stash_value) = self.stash_proxy.get_value(&prefixed(T::KEY)).await.unwrap()
            {
                if let Value::Stringval(string_value) = &*stash_value {
                    self.current_data = Some(serde_json::from_str(&string_value).unwrap());
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            } else {
                self.current_data = Some(T::DEFAULT_VALUE);
            }
        }
        if let Some(curent_value) = self.current_data {
            curent_value
        } else {
            panic!("Should never have no value");
        }

    }
}

/// Factory that vends out storage for individual structs.
pub struct DeviceStorageFactory {
    store: StoreProxy,
}

impl DeviceStorageFactory {
    pub fn new(store: StoreProxy, identity: &str) -> DeviceStorageFactory {
        store.identify(identity).unwrap();
        DeviceStorageFactory { store: store }
    }

    /// Currently, this doesn't support more than one instance of the same struct.
    pub fn get_store<T: DeviceStorageCompatible>(&self) -> Arc<Mutex<DeviceStorage<T>>> {
        let (accessor_proxy, server_end) = create_proxy().unwrap();
        self.store.create_accessor(false, server_end).unwrap();

        Arc::new(Mutex::new(DeviceStorage { stash_proxy: accessor_proxy, current_data: None }))
    }
}

fn prefixed(input_string: &str) -> String {
    format!("{}_{}", SETTINGS_PREFIX, input_string)
}


#[cfg(test)]
mod tests {
    use super::*;
    use fidl::encoding::OutOfLine;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use serde_derive::{Deserialize, Serialize};

    const VALUE0: i32 = 3;
    const VALUE1: i32 = 33;
    const VALUE2: i32 = 128;

    #[derive(Clone, Copy, Serialize, Deserialize, Debug)]
    struct TestStruct {
        value: i32,
    }

    impl DeviceStorageCompatible for TestStruct {
        const DEFAULT_VALUE: Self = TestStruct { value: VALUE0 };
        const KEY: &'static str = "testkey";
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::spawn(async move {
            let value_to_get = TestStruct { value: VALUE1 };

            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        assert_eq!(key, "settings_testkey");
                        let mut response =
                            Value::Stringval(serde_json::to_string(&value_to_get).unwrap());

                        responder.send(Some(&mut response).map(OutOfLine)).unwrap();
                    }
                    _ => {}
                }
            }
        });

        let mut storage: DeviceStorage<TestStruct> =
            DeviceStorage { stash_proxy: stash_proxy, current_data: None };

        let result = storage.get().await;

        assert_eq!(result.value, VALUE1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_default() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::spawn(async move {
            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key: _, responder } => {
                        responder.send(None.map(OutOfLine)).unwrap();
                    }
                    _ => {}
                }
            }
        });

        let mut storage: DeviceStorage<TestStruct> =
            DeviceStorage { stash_proxy: stash_proxy, current_data: None };

        let result = storage.get().await;

        assert_eq!(result.value, VALUE0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_write() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> =
            DeviceStorage { stash_proxy: stash_proxy, current_data: None };

        storage.write(TestStruct { value: VALUE2 }).expect("writing shouldn't fail");

        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::SetValue { key, val, control_handle: _ }) => {
                assert_eq!(key, "settings_testkey");
                if let Value::Stringval(string_value) = val {
                    let input_value: TestStruct = serde_json::from_str(&string_value).unwrap();
                    assert_eq!(input_value.value, VALUE2);
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            }
            request => panic!("Unexpected request: {:?}", request),
        }

        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Commit { .. }) => {} // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

}
