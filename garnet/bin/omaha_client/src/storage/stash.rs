// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use omaha_client::storage::Storage;

use failure::Fail;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_stash::{StoreAccessorMarker, StoreAccessorProxy, StoreMarker, Value};
use futures::future::BoxFuture;
use futures::prelude::*;
use log::{error, warn};

/// This is an implementation of the [`omaha_client::storage::Storage`] trait that uses the Stash
/// service on Fuchsia to store its data.
///
/// This data is erased when the device is factory reset.

type Result<T> = std::result::Result<T, StashError>;

#[derive(Debug, Fail)]
pub enum StashError {
    #[fail(display = "generic error: {}", 0)]
    Failure(failure::Error),

    #[fail(display = "FIDL error: {}", 0)]
    FIDL(fidl::Error),
}

impl From<fidl::Error> for StashError {
    fn from(e: fidl::Error) -> StashError {
        StashError::FIDL(e)
    }
}

impl From<failure::Error> for StashError {
    fn from(e: failure::Error) -> StashError {
        StashError::Failure(e)
    }
}

pub struct Stash {
    proxy: StoreAccessorProxy,
}

impl Stash {
    /// Create a new client for talking to the Stash storage API.
    ///
    /// The |identity| param provides a namespace for the client.  Each unique client identity has
    /// its own private namespace.
    pub async fn new(identity: &str) -> Result<Self> {
        let stash_svc = fuchsia_component::client::connect_to_service::<StoreMarker>()?;
        stash_svc.identify(identity)?;

        let (proxy, server_end) = create_proxy::<StoreAccessorMarker>()?;
        stash_svc.create_accessor(false, server_end)?;
        Ok(Stash { proxy })
    }

    #[cfg(test)]
    fn new_mock() -> (Self, fidl_fuchsia_stash::StoreAccessorRequestStream) {
        let (proxy, server_end) = create_proxy::<StoreAccessorMarker>().unwrap();
        (Stash { proxy }, server_end.into_stream().unwrap())
    }

    async fn get_value<'a>(&'a self, key: &'a str) -> Option<Box<Value>> {
        let result = self.proxy.get_value(key).await;
        match result {
            Ok(opt_value) => opt_value,
            Err(e) => {
                error!("Unable to read from stash for key: {}; {}", key, e);
                None
            }
        }
    }

    /// the Stash set_value() fn doesn't return any error from the service itself (but will receive
    /// a synchronous Error in case the channel is closed.  Any errors in storing the values happen
    /// during the call to commit().
    fn set_value<'a>(&'a self, key: &'a str, mut value: Value) -> Result<()> {
        match self.proxy.set_value(key, &mut value) {
            Ok(_) => Ok(()),
            Err(e) => {
                error!("Unable to write to stash for key: {}; {}", key, e);
                Err(e.into())
            }
        }
    }
}

impl Storage for Stash {
    type Error = StashError;

    fn get_string<'a>(&'a self, key: &'a str) -> BoxFuture<'a, Option<String>> {
        async move {
            if let Some(v) = self.get_value(key).await {
                if let Value::Stringval(s) = *v {
                    return Some(s);
                }
                warn!("found key '{}' in stash, but it was not a Stringval, is: {:?}", key, v)
            }
            None
        }
            .boxed()
    }

    fn get_int<'a>(&'a self, key: &'a str) -> BoxFuture<'a, Option<i64>> {
        async move {
            if let Some(v) = self.get_value(key).await {
                if let Value::Intval(s) = *v {
                    return Some(s);
                }
                warn!("found key '{}' in stash, but it was not an Intval, is: {:?}", key, v)
            }
            None
        }
            .boxed()
    }

    fn get_bool<'a>(&'a self, key: &'a str) -> BoxFuture<'_, Option<bool>> {
        async move {
            if let Some(v) = self.get_value(key).await {
                if let Value::Boolval(s) = *v {
                    return Some(s);
                }
                warn!("found key '{}' in stash, but it was not a Boolval, is: {:?}", key, v)
            }
            None
        }
            .boxed()
    }

    fn set_string<'a>(&'a mut self, key: &'a str, value: &'a str) -> BoxFuture<'_, Result<()>> {
        future::ready(self.set_value(key, Value::Stringval(value.to_string()))).boxed()
    }

    fn set_int<'a>(&'a mut self, key: &'a str, value: i64) -> BoxFuture<'_, Result<()>> {
        future::ready(self.set_value(key, Value::Intval(value))).boxed()
    }

    fn set_bool<'a>(&'a mut self, key: &'a str, value: bool) -> BoxFuture<'_, Result<()>> {
        future::ready(self.set_value(key, Value::Boolval(value))).boxed()
    }

    fn remove<'a>(&'a mut self, key: &'a str) -> BoxFuture<'_, Result<()>> {
        future::ready(match self.proxy.delete_value(key) {
            Ok(_) => Ok(()),
            Err(e) => {
                error!("Unable to write to stash for key: {}; {}", key, e);
                Err(e.into())
            }
        })
        .boxed()
    }

    fn commit(&mut self) -> BoxFuture<'_, Result<()>> {
        future::ready(match self.proxy.commit() {
            Ok(_) => Ok(()),
            Err(e) => {
                error!("Unable to commit changes to stash! {}", e);
                Err(e.into())
            }
        })
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_stash::StoreAccessorRequest;
    use fuchsia_async as fasync;
    use omaha_client::storage::tests::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_set_get_remove_string() {
        let mut storage = Stash::new("test_set_get_remove_string").await.unwrap();
        do_test_set_get_remove_string(&mut storage).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_get_remove_int() {
        let mut storage = Stash::new("test_set_get_remove_int").await.unwrap();
        do_test_set_get_remove_int(&mut storage).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_get_remove_bool() {
        let mut storage = Stash::new("test_set_get_remove_bool").await.unwrap();
        do_test_set_get_remove_bool(&mut storage).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_none_for_wrong_value_type() {
        let mut storage = Stash::new("test_return_none_for_wrong_value_type").await.unwrap();
        do_return_none_for_wrong_value_type(&mut storage).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ensure_no_error_remove_nonexistent_key() {
        let mut storage = Stash::new("test_ensure_no_error_remove_nonexistent_key").await.unwrap();
        do_ensure_no_error_remove_nonexistent_key(&mut storage).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_commit() {
        let (mut storage, mut stream) = Stash::new_mock();
        storage.commit().await.unwrap();
        match stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Commit { .. }) => {} // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }
}
