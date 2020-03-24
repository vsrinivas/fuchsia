// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::get_proxy_or_connect;
use anyhow::Error;
use fidl_fuchsia_weave::{ErrorCode, FactoryDataManagerMarker, FactoryDataManagerProxy};
use parking_lot::RwLock;

/// Perform Weave FIDL operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct WeaveFacade {
    factory_data_manager: RwLock<Option<FactoryDataManagerProxy>>,
}

impl WeaveFacade {
    pub fn new() -> WeaveFacade {
        WeaveFacade { factory_data_manager: RwLock::new(None) }
    }

    /// Returns the proxy provided on instantiation or establishes a new connection.
    fn factory_data_manager(&self) -> Result<FactoryDataManagerProxy, Error> {
        get_proxy_or_connect::<FactoryDataManagerMarker>(&self.factory_data_manager)
    }

    /// Returns a string mapped from the provided Weave error code.
    fn map_weave_err(&self, code: ErrorCode) -> anyhow::Error {
        anyhow!(match code {
            ErrorCode::FileNotFound => "FileNotFound",
            ErrorCode::CryptoError => "CryptoError",
        })
    }

    /// Returns the pairing code from the FactoryDataManager proxy service.
    pub async fn get_pairing_code(&self) -> Result<Vec<u8>, Error> {
        self.factory_data_manager()?.get_pairing_code().await?.map_err(|e| self.map_weave_err(e))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_weave::{FactoryDataManagerMarker, FactoryDataManagerRequest};
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref PAIRING_CODE: Vec<u8> = { b"ABC1234".to_vec() };
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_pairing_code() {
        let (proxy, mut stream) = create_proxy_and_stream::<FactoryDataManagerMarker>().unwrap();
        let facade = WeaveFacade { factory_data_manager: RwLock::new(Some(proxy)) };
        let facade_fut = async move {
            assert_eq!(facade.get_pairing_code().await.unwrap(), *PAIRING_CODE);
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(FactoryDataManagerRequest::GetPairingCode { responder })) => {
                    responder.send(&mut Ok((*PAIRING_CODE).clone())).unwrap();
                }
                err => panic!("Error in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }
}
