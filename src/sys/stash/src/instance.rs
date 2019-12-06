// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;

use failure::{err_msg, Error};
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl_fuchsia_stash::{
    FlushError, StoreAccessorMarker, StoreAccessorRequest, StoreAccessorRequestStream,
};
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::{TryFutureExt, TryStreamExt};
use std::sync::Arc;

use crate::accessor;
use crate::store;

/// Instance represents a single instance of the stash service, handling requests from a single
/// client.
#[derive(Clone)]
pub struct Instance {
    pub client_name: Option<String>,
    pub enable_bytes: bool,
    pub store_manager: Arc<Mutex<store::StoreManager>>,
}

impl Instance {
    /// identify must be called once at initial connection, to establish which namespace in the
    /// store to use. Right now this is honor-system based for preventing clients from accessing
    /// each other's data, but once component monikers are implemented they will take the place of
    /// this function call.
    pub fn identify(&mut self, name: String) -> Result<(), Error> {
        if let Some(name) = self.client_name.as_ref() {
            return Err(err_msg(format!("client attempted to identify twice: {}", name)));
        }
        self.client_name = Some(name);
        Ok(())
    }

    /// creates a new accessor for interacting with the store.
    pub fn create_accessor(
        &mut self,
        read_only: bool,
        server_end: ServerEnd<StoreAccessorMarker>,
    ) -> Result<(), Error> {
        let mut acc = accessor::Accessor::new(
            self.store_manager.clone(),
            self.enable_bytes,
            read_only,
            self.client_name.clone().ok_or(err_msg("identify has not been called"))?,
        );

        let server_chan = fasync::Channel::from_channel(server_end.into_channel())?;

        fasync::spawn(
            async move {
                let mut stream = StoreAccessorRequestStream::from_channel(server_chan);
                while let Some(req) = stream.try_next().await? {
                    match req {
                        StoreAccessorRequest::GetValue { key, responder } => {
                            let mut res = acc.get_value(&key).await?;
                            responder.send(res.as_mut())?;
                        }
                        StoreAccessorRequest::SetValue { key, val, .. } => {
                            acc.set_value(key, val).await?
                        }
                        StoreAccessorRequest::DeleteValue { key, .. } => {
                            acc.delete_value(key).await?
                        }
                        StoreAccessorRequest::ListPrefix { prefix, it, .. } => {
                            acc.list_prefix(prefix, it).await
                        }
                        StoreAccessorRequest::GetPrefix { prefix, it, .. } => {
                            acc.get_prefix(prefix, it).await?
                        }
                        StoreAccessorRequest::DeletePrefix { prefix, .. } => {
                            acc.delete_prefix(prefix).await?
                        }
                        StoreAccessorRequest::Commit { .. } => acc.commit().await?,
                        StoreAccessorRequest::Flush { responder } => {
                            if read_only {
                                responder.send(&mut Err(FlushError::ReadOnly))?;
                            } else {
                                responder.send(&mut acc.flush().await)?;
                            }
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: failure::Error| {
                fx_log_err!("error running accessor interface: {:?}", e)
            }),
        );
        Ok(())
    }
}
