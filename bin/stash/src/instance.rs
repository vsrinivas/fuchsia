// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;

use fuchsia_syslog::fx_log_err;
use failure::{err_msg, Error};
use fidl::encoding::OutOfLine;
use fidl::endpoints::{RequestStream, ServerEnd};
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;
use fidl_fuchsia_stash::{StoreAccessorMarker, StoreAccessorRequest, StoreAccessorRequestStream};

use crate::accessor;
use crate::store;

macro_rules! shutdown_on_error {
    ($x:expr, $y:expr) => {
        match $x {
            Ok(_) => (),
            Err(e) => {
                fx_log_err!("error encountered: {:?}", e);
                $y.shutdown();
            }
        }
    };
}

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
            self.client_name
                .clone()
                .ok_or(err_msg("identify has not been called"))?,
        );

        let server_chan = fasync::Channel::from_channel(server_end.into_channel())?;

        fasync::spawn(async move {
            let mut stream = StoreAccessorRequestStream::from_channel(server_chan);
            while let Some(req) = await!(stream.try_next())? {
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        match acc.get_value(&key) {
                            Ok(mut res) => responder.send(res.as_mut().map(OutOfLine))?,
                            Err(e) => {
                                fx_log_err!("error encountered: {:?}", e);
                                responder.control_handle().shutdown();
                            }
                        }
                    }
                    StoreAccessorRequest::SetValue {
                        key,
                        val,
                        control_handle,
                    } => shutdown_on_error!(acc.set_value(key, val), control_handle),
                    StoreAccessorRequest::DeleteValue {
                        key,
                        control_handle,
                    } => shutdown_on_error!(acc.delete_value(key), control_handle),
                    StoreAccessorRequest::ListPrefix {
                        prefix,
                        it,
                        control_handle: _,
                    } => acc.list_prefix(prefix, it),
                    StoreAccessorRequest::GetPrefix {
                        prefix,
                        it,
                        control_handle,
                    } => shutdown_on_error!(acc.get_prefix(prefix, it), control_handle),
                    StoreAccessorRequest::DeletePrefix {
                        prefix,
                        control_handle,
                    } => shutdown_on_error!(acc.delete_prefix(prefix), control_handle),
                    StoreAccessorRequest::Commit {
                        control_handle
                    } => shutdown_on_error!(acc.commit(), control_handle),
                }
            }
            Ok(())
        }.unwrap_or_else(|e: failure::Error| fx_log_err!("error running accessor interface: {:?}", e)));
        Ok(())
    }
}
