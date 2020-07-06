// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::get_proxy_or_connect;
use anyhow::Error;
use fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsProxy};
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};

/// Facade providing access to fuchsia.boot.Arguments service.
#[derive(Debug)]
pub struct BootArgumentsFacade {
    proxy: RwLock<Option<ArgumentsProxy>>,
}

impl BootArgumentsFacade {
    /// Creates a new [BootArgumentsFacade] with no active connection to the arguments service.
    pub fn new() -> Self {
        Self { proxy: RwLock::new(None) }
    }

    /// Return a cached connection to the arguments service, or try to connect and cache the
    /// connection for later.
    fn proxy(&self) -> Result<ArgumentsProxy, Error> {
        get_proxy_or_connect::<ArgumentsMarker>(&self.proxy)
    }

    /// Get the values of a boot argument `key`.
    ///
    /// # Errors
    ///
    /// Returns an Err(_) if
    ///  * connecting to the argument service fails
    pub(super) async fn get_string(&self, key: &str) -> Result<Option<String>, Error> {
        Ok(self.proxy()?.get_string(key).await?)
    }
}

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub(super) struct GetStringRequest {
    key: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_boot::ArgumentsRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_get_string_some() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let facade = BootArgumentsFacade { proxy: RwLock::new(Some(proxy)) };
        let facade_fut = async move {
            assert_eq!(
                facade.get_string("omaha_url").await.unwrap(),
                Some("http://example.com".to_string()),
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(ArgumentsRequest::GetString { key, responder })) => {
                    assert_eq!(key, "omaha_url");
                    responder.send(Some("http://example.com")).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_string_none() {
        let (proxy, mut stream) = create_proxy_and_stream::<ArgumentsMarker>().unwrap();
        let facade = BootArgumentsFacade { proxy: RwLock::new(Some(proxy)) };
        let facade_fut = async move {
            assert_eq!(facade.get_string("omaha_url").await.unwrap(), None,);
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(ArgumentsRequest::GetString { key, responder })) => {
                    assert_eq!(key, "omaha_url");
                    responder.send(None).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }
}
