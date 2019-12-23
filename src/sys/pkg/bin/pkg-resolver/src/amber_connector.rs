// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_amber::{self, ControlMarker as AmberMarker, ControlProxy as AmberProxy},
    fuchsia_component::client::connect_to_service_at,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon::Status,
    parking_lot::RwLock,
    std::sync::Arc,
};

/// AmberConnect abstracts over connecting to the Amber service, in order to allow mock connections
/// to be created in a test environment.
pub trait AmberConnect: Send + Sync {
    /// Connect to Amber, or return an error if Amber is offline.
    fn connect(&self) -> Result<AmberProxy, Status>;
}

/// AmberConnector maintains a connection to Amber. If the connection is ever closed, it it
/// automatically reconnected.
#[derive(Clone, Debug)]
pub struct AmberConnector {
    amber: Arc<RwLock<Option<AmberProxy>>>,
    service_prefix: String,
}

impl AmberConnector {
    /// Construct a new [AmberConnector]
    pub fn new() -> Self {
        AmberConnector { amber: Arc::new(RwLock::new(None)), service_prefix: "/svc".into() }
    }
}

impl AmberConnect for AmberConnector {
    fn connect(&self) -> Result<AmberProxy, Status> {
        // If we have an open connection to amber, just return it.
        if let Some(ref amber) = *self.amber.read() {
            if !amber.is_closed() {
                return Ok(amber.clone());
            }
        }

        // Otherwise, try to reconnect to amber.
        let mut amber = self.amber.write();
        if let Some(ref amber) = *amber {
            if !amber.is_closed() {
                return Ok(amber.clone());
            }
        }

        fx_log_info!("amber connection is closed, reconnecting");

        // Reconnect to amber.
        match connect_to_service_at::<AmberMarker>(&self.service_prefix) {
            Ok(a) => {
                fx_log_info!("reconnected to amber");
                *amber = Some(a.clone());
                Ok(a)
            }
            Err(err) => {
                fx_log_err!("failed to connect to amber: {}", err);
                Err(Status::INTERNAL)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl_fuchsia_amber::{ControlRequest, ControlRequestStream},
        fuchsia_async as fasync,
        fuchsia_component::server::ServiceFs,
        fuchsia_zircon as zx,
        futures::prelude::*,
        std::cell::Cell,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_amber_connector() {
        let ns = fdio::Namespace::installed().expect("installed namespace");
        let mut c = AmberConnector::new();

        c.service_prefix = "/test/amber_connector/svc".into();
        let (service_channel, server_end) = zx::Channel::create().expect("create channel");
        ns.bind(&c.service_prefix, service_channel).expect("bind test svc");

        // In order to test that we reconnect, we create a mock amber instance that closes the
        // connection if the `do_test` method when it receives the number `2`. `gen` is used to
        // tell if we created a new connection.
        let gen = Cell::new(1i32);

        let mut fs = ServiceFs::new_local();
        fs.add_fidl_service(move |mut stream: ControlRequestStream| {
            let current_gen = gen.get();
            gen.set(current_gen + 1);
            fasync::spawn_local(async move {
                while let Some(req) = stream.try_next().await.unwrap_or(None) {
                    if let ControlRequest::DoTest { input, responder } = req {
                        if input == 1 {
                            responder
                                .send(&format!("gen {}", current_gen))
                                .expect("patient client");
                        }
                    }
                }
            })
        })
        .serve_connection(server_end)
        .expect("serve_connection");

        fasync::spawn_local(fs.collect());

        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.do_test(1).await.expect("ping"), "gen 1",);

        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.do_test(1).await.expect("ping"), "gen 1",);

        proxy.do_test(2).await.expect_err("oops");
        let proxy = c.connect().expect("can connect");
        assert_eq!(proxy.do_test(1).await.expect("ping"), "gen 2",);
    }
}
