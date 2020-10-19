// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        data::{self, Data, LazyNode},
        metrics::Metrics,
    },
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_inspect as fidl_inspect,
    fidl_fuchsia_sys::{EnvironmentControllerProxy, EnvironmentMarker, EnvironmentOptions},
    fidl_test_inspect_validate as validate,
    fuchsia_component::client as fclient,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::{self as zx, Vmo},
    std::sync::atomic::{AtomicU64, Ordering},
    std::{convert::TryFrom, path::Path, str::FromStr},
};

pub const VMO_SIZE: u64 = 4096;

/// Create a unique environment name with the given prefix.
fn make_environment_name(prefix: impl AsRef<str>) -> String {
    static NEXT_ENVIRONMENT_SUFFIX: AtomicU64 = AtomicU64::new(0);
    let v = NEXT_ENVIRONMENT_SUFFIX.fetch_add(1, Ordering::Relaxed);
    format!("{}_{}", prefix.as_ref(), v)
}

pub struct Puppet {
    pub vmo: Vmo,
    // Need to remember the connection to avoid dropping the VMO
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

    pub async fn apply_lazy(
        &mut self,
        lazy_action: &mut validate::LazyAction,
    ) -> Result<validate::TestResult, Error> {
        match &self.connection.root_link_channel {
            Some(_) => Ok(self.connection.fidl.act_lazy(lazy_action).await?),
            None => Ok(validate::TestResult::Unimplemented),
        }
    }

    pub async fn publish(&mut self) -> Result<validate::TestResult, Error> {
        Ok(self.connection.fidl.publish().await?)
    }

    pub async fn unpublish(&mut self) -> Result<validate::TestResult, Error> {
        Ok(self.connection.fidl.unpublish().await?)
    }

    pub fn name<'a>(&'a self) -> &'a str {
        &self.name
    }

    pub fn component_name(&self) -> String {
        format!("{}.cmx", self.name())
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

    /// Get the environment name the puppet was run in.
    pub fn environment_name<'a>(&'a self) -> &'a str {
        &self.connection.environment_name
    }

    #[cfg(test)]
    pub async fn connect_local(local_fidl: validate::ValidateProxy) -> Result<Puppet, Error> {
        Puppet::initialize_with_connection(
            Connection::new(local_fidl, None, None, "".to_owned()),
            "*Local*".to_owned(),
        )
        .await
    }

    async fn initialize_with_connection(
        mut connection: Connection,
        name: String,
    ) -> Result<Puppet, Error> {
        Ok(Puppet { vmo: connection.initialize_vmo().await?, connection, name })
    }

    pub async fn read_data(&self) -> Result<Data, Error> {
        Ok(match &self.connection.root_link_channel {
            None => data::Scanner::try_from(&self.vmo)?.data(),
            Some(root_link_channel) => {
                let vmo_tree = LazyNode::new(root_link_channel.clone()).await?;
                data::Scanner::try_from(vmo_tree)?.data()
            }
        })
    }

    pub fn metrics(&self) -> Result<Metrics, Error> {
        Ok(data::Scanner::try_from(&self.vmo)?.metrics())
    }
}

struct Connection {
    fidl: validate::ValidateProxy,
    // Connection to Tree FIDL if Puppet supports it.
    // Puppets can add support by implementing InitializeTree method.
    root_link_channel: Option<fidl_inspect::TreeProxy>,
    // We need to keep the 'app' un-dropped on non-local connections so the
    // remote program doesn't go away. But we never use it once we have the
    // FIDL connection.
    _app: Option<fuchsia_component::client::App>,

    // The nested environment we are starting the puppet in.
    _env: Option<EnvironmentControllerProxy>,

    // The name of the environment we started the component in.
    pub environment_name: String,
}

impl Connection {
    // Note! In v1, the launch() and connect_to_service() functions do not return errors
    // when given a bad URL. There's no way to detect bad URLs until we actually make a
    // FIDL call that the server is supposed to serve, in initialize_vmo().
    async fn start_and_connect(server_url: impl Into<String>) -> Result<Self, Error> {
        let server_url = server_url.into();
        let (new_env, new_env_server_end) = fidl::endpoints::create_proxy()?;
        let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
        let (launcher, launcher_server_end) = fidl::endpoints::create_proxy()?;

        let env = fclient::connect_to_service::<EnvironmentMarker>()?;
        let environment_name = make_environment_name("puppet");
        env.create_nested_environment(
            new_env_server_end,
            controller_server_end,
            &environment_name,
            None,
            &mut EnvironmentOptions {
                inherit_parent_services: true,
                use_parent_runners: false,
                kill_on_oom: false,
                delete_storage_on_death: false,
            },
        )
        .context("creating isolated environment")?;

        new_env.get_launcher(launcher_server_end).context("getting nested environment launcher")?;
        let app = fclient::launch(&launcher, server_url.clone(), None)
            .context(format!("Failed to launch Validator puppet {}", server_url))?;

        let puppet_fidl = app
            .connect_to_service::<validate::ValidateMarker>()
            .context("Failed to connect to validate puppet")?;

        Ok(Self::new(puppet_fidl, Some(app), Some(controller), environment_name))
    }

    async fn fetch_link_channel(fidl: &validate::ValidateProxy) -> Option<fidl_inspect::TreeProxy> {
        let params = validate::InitializationParams { vmo_size: Some(VMO_SIZE) };
        let response = fidl.initialize_tree(params).await;
        if let Ok((Some(tree_client_end), validate::TestResult::Ok)) = response {
            tree_client_end.into_proxy().ok()
        } else {
            None
        }
    }

    async fn get_vmo_handle(channel: &fidl_inspect::TreeProxy) -> Result<Vmo, Error> {
        let tree_content = channel.get_content().await?;
        let buffer = tree_content.buffer.ok_or(format_err!("Buffer doesn't contain VMO"))?;
        Ok(buffer.vmo)
    }

    fn new(
        fidl: validate::ValidateProxy,
        app: Option<fuchsia_component::client::App>,
        env: Option<EnvironmentControllerProxy>,
        environment_name: String,
    ) -> Self {
        Self { fidl, root_link_channel: None, _app: app, _env: env, environment_name }
    }

    async fn initialize_vmo(&mut self) -> Result<Vmo, Error> {
        self.root_link_channel = Self::fetch_link_channel(&self.fidl).await;
        match &self.root_link_channel {
            Some(root_link_channel) => Self::get_vmo_handle(&root_link_channel).await,
            None => {
                let params = validate::InitializationParams { vmo_size: Some(VMO_SIZE) };
                let handle: Option<zx::Handle>;
                let out = self.fidl.initialize(params).await?;
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
            Puppet::derive_my_name("fuchsia-pkg://path.com/name#meta/my_name.cmx")?,
            "my_name".to_string()
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fidl_loopback() -> Result<(), Error> {
        let mut puppet = local_incomplete_puppet().await?;
        assert_eq!(puppet.vmo.get_size().unwrap(), VMO_SIZE);
        let tree = puppet.read_data().await?;
        assert_eq!(tree.to_string(), "root ->".to_string());
        let mut data = Data::new();
        tree.compare(&data, DiffType::Full)?;
        let mut action = create_node!(parent: ROOT_ID, id: 1, name: "child");
        puppet.apply(&mut action).await?;
        data.apply(&action)?;
        let tree = data::Scanner::try_from(&puppet.vmo)?.data();
        assert_eq!(tree.to_string(), "root ->\n> child ->".to_string());
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
        fasync::Task::spawn(
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
                        ValidateRequest::InitializeTree { params: _, responder } => {
                            responder.send(None, TestResult::Unimplemented)?;
                        }
                        ValidateRequest::ActLazy { lazy_action: _, responder } => {
                            responder.send(TestResult::Unimplemented)?;
                        }
                        ValidateRequest::Publish { responder } => {
                            responder.send(TestResult::Unimplemented)?;
                        }
                        ValidateRequest::Unpublish { responder } => {
                            responder.send(TestResult::Unimplemented)?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| info!("error running validate interface: {:?}", e)),
        )
        .detach();
    }
}
