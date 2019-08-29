// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::results,
    failure::{bail, Error, ResultExt},
    fidl_test_inspect_validate as validate,
    fuchsia_component::client as fclient,
    fuchsia_zircon::{self as zx, Vmo},
    log::*,
};

const VMO_SIZE: u64 = 4096;

pub struct Blocks {}

impl Blocks {}

pub struct Puppet {
    vmo: Vmo,
    // Need to remember the connection to avoid dropping the VMO
    #[allow(dead_code)]
    connection: Connection,
}

impl Puppet {
    pub fn apply(&mut self, _actions: &[validate::Action], _results: &mut results::Results) {}

    pub fn vmo_blocks(&self, _results: &mut results::Results) -> Result<Blocks, Error> {
        let mut header_bytes: [u8; 16] = [0; 16];
        self.vmo.read(&mut header_bytes, 0)?;

        Ok(Blocks {})
    }

    pub async fn connect(server_url: &str, results: &mut results::Results) -> Result<Self, Error> {
        Puppet::initialize_with_connection(
            Connection::start_and_connect(server_url).await?,
            results,
        )
        .await
    }

    pub async fn connect_local(
        local_fidl: validate::ValidateProxy,
        results: &mut results::Results,
    ) -> Result<Puppet, Error> {
        Puppet::initialize_with_connection(Connection::new(local_fidl, None), results).await
    }

    async fn initialize_with_connection(
        mut connection: Connection,
        results: &mut results::Results,
    ) -> Result<Puppet, Error> {
        Ok(Puppet { vmo: connection.initialize_vmo(results).await?, connection })
    }
}

struct Connection {
    fidl: validate::ValidateProxy,
    // We need to keep the 'app' un-dropped on non-local connections so the
    // remote program doesn't go away. But we never use it once we have the
    // FIDL connection.
    #[allow(dead_code)]
    app: Option<fuchsia_component::client::App>,
}

impl Connection {
    async fn start_and_connect(server_url: &str) -> Result<Self, Error> {
        let launcher = fclient::launcher().context("Failed to open launcher service")?;
        let app = fclient::launch(&launcher, server_url.to_owned(), None)
            .context(format!("Failed to launch Validator puppet {}", server_url))?;
        let puppet_fidl = app
            .connect_to_service::<validate::ValidateMarker>()
            .context("Failed to connect to validate puppet")?;
        Ok(Self::new(puppet_fidl, Some(app)))
    }

    fn new(fidl: validate::ValidateProxy, app: Option<fuchsia_component::client::App>) -> Self {
        Self { fidl, app }
    }

    async fn initialize_vmo(&mut self, results: &mut results::Results) -> Result<Vmo, Error> {
        let params = validate::InitializationParams { vmo_size: Some(VMO_SIZE) };
        let out = self.fidl.initialize(params).await.context("Calling vmo init")?;
        info!("Out from initialize: {:?}", out);
        let mut handle: Option<zx::Handle> = None;
        if let (Some(out_handle), _) = out {
            handle = Some(out_handle);
        } else {
            results.error("Didn't get a VMO handle".into());
        }
        match handle {
            Some(unwrapped_handle) => Ok(Vmo::from(unwrapped_handle)),
            None => {
                results.error("Didn't unwrap a handle".into());
                bail!("Failed to connect; see JSON output for details");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, RequestStream, ServerEnd},
        fidl_test_inspect_validate::*,
        fuchsia_async as fasync,
        fuchsia_inspect::Inspector,
        futures::prelude::*,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_loopback() -> Result<(), Error> {
        let mut results = results::Results::new();
        let (client_end, server_end) = create_proxy().unwrap();
        serve(server_end).await;
        let puppet = Puppet::connect_local(client_end, &mut results).await?;
        assert_eq!(puppet.vmo.get_size().unwrap(), VMO_SIZE);
        Ok(())
    }

    async fn serve(server_end: ServerEnd<ValidateMarker>) {
        fasync::spawn(
            async move {
                // Inspector must be remembered so its VMO persists
                let mut _inspector_maybe: Option<Inspector> = None;
                let server_chan = fasync::Channel::from_channel(server_end.into_channel())?;
                let mut stream = ValidateRequestStream::from_channel(server_chan);
                while let Some(event) = stream.try_next().await? {
                    match event {
                        ValidateRequest::Initialize { params, responder } => {
                            let inspector = match params.vmo_size {
                                Some(size) => Inspector::new_with_size(size as usize),
                                None => Inspector::new(),
                            };
                            responder
                                .send(inspector.vmo_handle_for_test(), TestResult::Ok)
                                .context("responding to initialize")?;
                            _inspector_maybe = Some(inspector);
                        }
                        unexpected => {
                            info!("Unexpected FIDL command {:?} was invoked.", unexpected);
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: failure::Error| {
                    error!("error running validate interface: {:?}", e)
                }),
        );
    }
}
