// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        data::{self, Data},
        metrics::Metrics,
    },
    anyhow::{format_err, Context as _, Error},
    fidl_test_inspect_validate as validate,
    fuchsia_component::client as fclient,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::{self as zx, Vmo},
    std::{convert::TryFrom, path::Path, str::FromStr},
};

pub const VMO_SIZE: u64 = 4096;

pub struct Puppet {
    vmo: Vmo,
    // Need to remember the connection to avoid dropping the VMO
    #[allow(dead_code)]
    connection: Connection,
    name: String,
}

impl Puppet {
    pub async fn apply(
        &mut self,
        action: &mut validate::Action,
    ) -> Result<validate::TestResult, Error> {
        Ok(self.connection.fidl.act(action).await?)
    }

    pub fn name<'a>(&'a self) -> &'a str {
        &self.name
    }

    // Extracts the .cmx file basename for output to the user.
    fn derive_my_name(url: &str) -> Result<String, Error> {
        let url_parse = PkgUrl::from_str(url)?;
        let cmx_name = url_parse.resource().ok_or(format_err!("URL parse"))?;
        let cmx_path = Path::new(cmx_name);
        if let Some(s) = cmx_path.file_stem() {
            if let Some(s) = s.to_str() {
                return Ok(s.to_owned());
            }
        }
        return Err(format_err!("Bad path {} from url {}", cmx_name, url));
    }

    pub async fn connect(server_url: &str) -> Result<Self, Error> {
        Puppet::initialize_with_connection(
            Connection::start_and_connect(server_url).await?,
            Self::derive_my_name(server_url)?,
        )
        .await
    }

    #[cfg(test)]
    pub async fn connect_local(local_fidl: validate::ValidateProxy) -> Result<Puppet, Error> {
        Puppet::initialize_with_connection(Connection::new(local_fidl, None), "*Local*".to_owned())
            .await
    }

    async fn initialize_with_connection(
        mut connection: Connection,
        name: String,
    ) -> Result<Puppet, Error> {
        Ok(Puppet { vmo: connection.initialize_vmo().await?, connection, name })
    }

    pub fn read_data(&self) -> Result<Data, Error> {
        Ok(data::Scanner::try_from(&self.vmo)?.data())
    }

    pub fn metrics(&self) -> Result<Metrics, Error> {
        Ok(data::Scanner::try_from(&self.vmo)?.metrics())
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
    // Note! In v1, the launch() and connect_to_service() functions do not return errors
    // when given a bad URL. There's no way to detect bad URLs until we actually make a
    // FIDL call that the server is supposed to serve, in initialize_vmo().
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

    async fn initialize_vmo(&mut self) -> Result<Vmo, Error> {
        let params = validate::InitializationParams { vmo_size: Some(VMO_SIZE) };
        let out = self.fidl.initialize(params).await?;
        let handle: Option<zx::Handle>;
        if let (Some(out_handle), _) = out {
            handle = Some(out_handle);
        } else {
            return Err(format_err!("Didn't get a VMO handle"));
        }
        match handle {
            Some(unwrapped_handle) => Ok(Vmo::from(unwrapped_handle)),
            None => {
                return Err(format_err!("Failed to unwrap handle"));
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use {
        super::*,
        crate::{create_node, DiffType},
        //anyhow::format_err,
        fidl::endpoints::{create_proxy, RequestStream, ServerEnd},
        fidl_test_inspect_validate::*,
        fuchsia_async as fasync,
        fuchsia_inspect::{Inspector, IntProperty, Node},
        fuchsia_zircon::HandleBased,
        futures::prelude::*,
        log::*,
        std::collections::HashMap,
    };

    #[test]
    fn puppet_name_derivation() -> Result<(), Error> {
        assert_eq!(
            Puppet::derive_my_name("fuchsia-pkg://path#meta/my_name.cmx")?,
            "my_name".to_string()
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_loopback() -> Result<(), Error> {
        let mut puppet = local_incomplete_puppet().await?;
        assert_eq!(puppet.vmo.get_size().unwrap(), VMO_SIZE);
        let tree = puppet.read_data()?;
        assert_eq!(tree.to_string(), " root ->\n\n\n".to_string());
        let mut data = Data::new();
        tree.compare(&data, DiffType::Full)?;
        let mut action = create_node!(parent: ROOT_ID, id: 1, name: "child");
        puppet.apply(&mut action).await?;
        data.apply(&action)?;
        let tree = data::Scanner::try_from(&puppet.vmo)?.data();
        assert_eq!(tree.to_string(), " root ->\n\n>  child ->\n\n\n\n".to_string());
        tree.compare(&data, DiffType::Full)?;
        Ok(())
    }

    // This is a partial implementation.
    // All it can do is initialize, and then create nodes and int properties (which it
    // will hold forever). Trying to create a uint property will return Unimplemented.
    // Other actions will give various kinds of incorrect results.
    pub(crate) async fn local_incomplete_puppet() -> Result<Puppet, Error> {
        let (client_end, server_end) = create_proxy().unwrap();
        spawn_local_puppet(server_end).await;
        Ok(Puppet::connect_local(client_end).await?)
    }

    async fn spawn_local_puppet(server_end: ServerEnd<ValidateMarker>) {
        fasync::spawn(
            async move {
                // Inspector must be remembered so its VMO persists
                let mut inspector_maybe: Option<Inspector> = None;
                let mut nodes: HashMap<u32, Node> = HashMap::new();
                let mut properties: HashMap<u32, IntProperty> = HashMap::new();
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
                                .send(
                                    inspector.duplicate_vmo().map(|v| v.into_handle()),
                                    TestResult::Ok,
                                )
                                .context("responding to initialize")?;
                            inspector_maybe = Some(inspector);
                        }
                        ValidateRequest::Act { action, responder } => match action {
                            Action::CreateNode(CreateNode { parent, id, name }) => {
                                inspector_maybe.as_ref().map(|i| {
                                    let parent_node = if parent == ROOT_ID {
                                        i.root()
                                    } else {
                                        nodes.get(&parent).unwrap()
                                    };
                                    let new_child = parent_node.create_child(name);
                                    nodes.insert(id, new_child);
                                });
                                responder.send(TestResult::Ok)?;
                            }
                            Action::CreateNumericProperty(CreateNumericProperty {
                                parent,
                                id,
                                name,
                                value: Number::IntT(value),
                            }) => {
                                inspector_maybe.as_ref().map(|i| {
                                    let parent_node = if parent == 0 {
                                        i.root()
                                    } else {
                                        nodes.get(&parent).unwrap()
                                    };
                                    properties.insert(id, parent_node.create_int(name, value))
                                });
                                responder.send(TestResult::Ok)?;
                            }
                            Action::CreateNumericProperty(CreateNumericProperty {
                                value: Number::UintT(_),
                                ..
                            }) => {
                                responder.send(TestResult::Unimplemented)?;
                            }

                            _ => responder.send(TestResult::Illegal)?,
                        },
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| info!("error running validate interface: {:?}", e)),
        );
    }
}
