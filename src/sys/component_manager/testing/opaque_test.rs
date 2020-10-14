// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::EventSource,
    anyhow::{Context as _, Error},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys::{
        ComponentControllerEvent, EnvironmentControllerEvent, EnvironmentControllerProxy,
        EnvironmentMarker, EnvironmentOptions, LauncherProxy,
    },
    fidl_fuchsia_sys2 as fsys, files_async,
    fuchsia_component::client::*,
    fuchsia_zircon as zx,
    futures::future,
    futures::stream::{StreamExt, TryStreamExt},
    rand::random,
    std::fs::*,
    std::path::PathBuf,
};

/// This static string is used by OpaqueTest to start Component Manager v2.
/// A fuchsia system is expected to have Component Manager v2 at this URL for
/// OpaqueTest::default to work correctly.
pub static COMPONENT_MANAGER_URL: &str =
    "fuchsia-pkg://fuchsia.com/component-manager#meta/component_manager.cmx";

/// This structure contains all elements responsible for a black-box test
/// of Component Manager v2. If this object is dropped, the component manager
/// will be killed, ending the test.
///
/// Any component using OpaqueTest must satisfy the following minimal requirements
/// in its manifest:
///
/// "sandbox": {
///     "features": [
///         "hub"
///     ],
///     "services": [
///         "fuchsia.process.Launcher",
///         "fuchsia.sys.Launcher",
///         "fuchsia.sys.Environment",
///         "fuchsia.logger.LogSink"
///     ]
/// }
///
/// Component manager requires the process.Launcher and LogSink services, so
/// OpaqueTest passes them into component manager from the parent component.
///
/// The Environment service is required to ensure hermeticity. This is important
/// for integration tests, where a single component runs multiple tests simultaneously.
/// There will be multiple component managers and they must all run in isolated environments.
///
/// The hub is required to access the Events FIDL service exposed by
/// component manager when the debug mode is enabled in component manager config.
///
/// Usage: /src/sys/component_manager/tests contains multiple tests such as
/// routing, base_resolver and shutdown which use this class.
pub struct OpaqueTest {
    /// The environment under which component manager runs
    pub env: EnvironmentControllerProxy,

    /// The app that handles component manager
    pub component_manager_app: App,

    /// URL for the component manager used for this test
    pub component_manager_url: String,

    /// URL of the root component given to this test's component manager
    pub root_component_url: String,

    /// Label give to this test's component manager in the hub
    pub label: String,
}

impl OpaqueTest {
    /// Creates a black box test with the default component manager URL and no
    /// additional directory handles that are passed in to component manager.
    /// The output of component manager is not redirected.
    ///
    /// At the end of this function call, a component manager has been created
    /// in a hermetic environment and its execution is halted.
    ///
    /// For greater control over test setup parameters, use [`OpaqueTestBuilder`].
    pub async fn default(root_component_url: &str) -> Result<Self, Error> {
        OpaqueTestBuilder::new(root_component_url).build().await
    }

    /// The path from Hub v1 to component manager.
    pub fn get_component_manager_path(&self) -> PathBuf {
        find_component_manager_in_hub(&self.component_manager_url, &self.label)
    }

    /// The path from Hub v1 to Hub v2
    /// To get this path, append "out/hub" to the component manager path.
    pub fn get_hub_v2_path(&self) -> PathBuf {
        let path = self.get_component_manager_path();
        path.join("out/hub")
    }

    /// The events FIDL service connected to component manager.
    /// An integration test can use this service to halt tasks of component manager
    /// at various points during its execution.
    pub async fn connect_to_event_source(&self) -> Result<EventSource, Error> {
        let path = self.get_component_manager_path();
        connect_to_event_source(&path).await
    }
}

/// Configures a [`OpaqueTest`].
pub struct OpaqueTestBuilder {
    config: String,
    root_component_url: String,
    component_manager_url: Option<String>,
    dir_handles: Vec<(String, zx::Handle)>,
    runtime_config: Option<String>,
    extra_args: Vec<String>,
}

impl OpaqueTestBuilder {
    /// Creates a new OpaqueTestBuilder instance configured to launch the root component
    /// identified by the URL `root_component_url`.
    pub fn new(root_component_url: impl Into<String>) -> Self {
        OpaqueTestBuilder {
            config: "/pkg/data/component_manager_debug_config".to_string(),
            root_component_url: root_component_url.into(),
            component_manager_url: None,
            dir_handles: Vec::new(),
            runtime_config: None,
            extra_args: Vec::new(),
        }
    }

    /// Changes the URL of the component manager used for the black box test from the
    /// default [`COMPONENT_MANAGER_URL`] to `url`.
    pub fn component_manager_url(mut self, url: impl Into<String>) -> Self {
        self.component_manager_url = Some(url.into());
        self
    }

    /// Adds a directory `handle` to be installed in the component manager's namespace at
    /// the given `path`. If an output file descriptor is supplied, it is attached to the
    /// component manager's output.
    pub fn add_dir_handle(mut self, path: impl Into<String>, handle: zx::Handle) -> Self {
        self.dir_handles.push((path.into(), handle));
        self
    }

    pub fn config(mut self, config: impl Into<String>) -> Self {
        self.config = config.into();
        self
    }

    /// Sets the path to the configuration file for component manager.
    pub fn runtime_config(mut self, runtime_config: impl Into<String>) -> Self {
        self.runtime_config = Some(runtime_config.into());
        self
    }

    /// Extends the set of directory handles to include those in `handles`. If an output file
    /// descriptor is supplied, it is attached to the component manager's output.
    pub fn extend_dir_handles(
        mut self,
        handles: impl IntoIterator<Item = (String, zx::Handle)>,
    ) -> Self {
        self.dir_handles.extend(handles);
        self
    }

    /// Adds an extra parameter to pass to the started component manager.
    pub fn add_extra_arg(mut self, arg: impl Into<String>) -> Self {
        self.extra_args.push(arg.into());
        self
    }

    /// Builds a OpaqueTest. Upon success, a component manager has been created
    /// in a hermetic environment and its execution is halted.
    pub async fn build(self) -> Result<OpaqueTest, Error> {
        // Use a random integer to identify this component manager
        let random_num = random::<u32>();
        let label = format!("test_{}", random_num);

        let (env, launcher) = create_isolated_environment(&label).await?;
        let component_manager_url =
            self.component_manager_url.unwrap_or_else(|| COMPONENT_MANAGER_URL.to_string());
        let component_manager_app = launch_component_manager(
            launcher,
            &component_manager_url,
            &self.config,
            &self.root_component_url,
            self.dir_handles,
            self.runtime_config,
            self.extra_args,
        )
        .await?;

        Ok(OpaqueTest {
            env,
            component_manager_app,
            component_manager_url,
            root_component_url: self.root_component_url,
            label,
        })
    }
}

/// Creates an isolated environment for component manager inside this component.
/// This isolated environment will be given the provided label so as to identify
/// it uniquely in the hub.
async fn create_isolated_environment(
    label: &str,
) -> Result<(EnvironmentControllerProxy, LauncherProxy), Error> {
    let env = connect_to_service::<EnvironmentMarker>()
        .context("could not connect to current environment")?;

    let (new_env, new_env_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;
    let (controller, controller_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;
    let (launcher, launcher_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;

    // Component manager will run with these environment options
    let mut env_options = EnvironmentOptions {
        // This flag ensures that component manager gets all its required
        // services from the parent component
        inherit_parent_services: true,
        use_parent_runners: true,
        kill_on_oom: false,
        delete_storage_on_death: true,
    };

    env.create_nested_environment(
        new_env_server_end,
        controller_server_end,
        label,
        None,
        &mut env_options,
    )
    .context("could not create isolated environment")?;

    // Wait for the environment to be setup.
    // There is only one type of event (OnCreated) in this protocol, so just get the next one.
    let EnvironmentControllerEvent::OnCreated {} =
        controller.take_event_stream().next().await.unwrap().unwrap();

    // Get the launcher for this environment so it can be used to start component manager.
    new_env
        .get_launcher(launcher_server_end)
        .context("could not get isolated environment launcher")?;

    Ok((controller, launcher))
}

/// Use the provided launcher from the isolated environment to startup component manager.
/// Attach any provided directory handles. Component manager is provided the URL of the
/// v2 component to start and the config. Blocks until the out directory of component
/// manager is set up.
async fn launch_component_manager(
    launcher: LauncherProxy,
    component_manager_url: &str,
    config: &str,
    root_component_url: &str,
    dir_handles: Vec<(String, zx::Handle)>,
    runtime_config: Option<String>,
    extra_args: Vec<String>,
) -> Result<App, Error> {
    let mut options = LaunchOptions::new();

    // Add in any provided directory handles to component manager's namespace
    for dir in dir_handles {
        options.add_handle_to_namespace(dir.0, dir.1);
    }

    let mut args = vec!["--config".to_string(), config.to_string(), root_component_url.to_string()];
    if let Some(runtime_config) = runtime_config {
        args.extend(vec!["--runtime-config".to_string(), runtime_config]);
    }
    args.extend(extra_args);

    let component_manager_app =
        launch_with_options(&launcher, component_manager_url.to_string(), Some(args), options)
            .context("could not launch component manager")?;

    // Wait for component manager to setup the out directory
    let event_stream = component_manager_app.controller().take_event_stream();
    event_stream
        .try_filter_map(|event| {
            let event = match event {
                ComponentControllerEvent::OnDirectoryReady {} => Some(event),
                _ => None,
            };
            future::ready(Ok(event))
        })
        .next()
        .await;

    Ok(component_manager_app)
}

/// Use the path to component manager's hub to find the events service
/// and connect to it
async fn connect_to_event_source(component_manager_path: &PathBuf) -> Result<EventSource, Error> {
    let path_to_svc = component_manager_path.join("out/svc");
    let path_to_svc = path_to_svc.to_str().expect("found invalid chars");
    let proxy = connect_to_service_at::<fsys::BlockingEventSourceMarker>(path_to_svc)
        .context("could not connect to BlockingEventSource service")?;
    Ok(EventSource::from_proxy(proxy))
}

/// Explore the hub to find the component manager running in the environment
/// marked by the provided label.
fn find_component_manager_in_hub(component_manager_url: &str, label: &str) -> PathBuf {
    let path_to_env = format!("/hub/r/{}", label);

    // Get the id for the environment
    let dir: Vec<DirEntry> = read_dir(path_to_env)
        // TODO convert this to an error instead of causing a panic
        .expect("could not open nested environment in the hub")
        .map(|x| x.expect("entry unreadable"))
        .collect();

    // There can only be one environment with this label.
    // So there should be only one id.
    assert_eq!(dir.len(), 1);

    // Since the component manager URL can be custom, we need to extract the
    // component name.
    let component_name = component_manager_url
        .split("/")
        .last()
        .expect("the URL for component manager must have at least one '/' character");

    let path_to_cm = dir[0].path().join("c").join(component_name);

    // Get the id for component manager
    let dir: Vec<DirEntry> = read_dir(path_to_cm)
        // TOOD convert this into an error instead of causing a panic
        .expect("could not open component manager in the hub")
        .map(|x| x.expect("entry unreadable"))
        .collect();

    // There can only be one component manager inside this environment.
    // So there should be only one id.
    assert_eq!(dir.len(), 1);

    dir[0].path()
}

/// Convenience method to lists the contents of a directory proxy as a sorted vector of strings.
pub async fn list_directory(root_proxy: &DirectoryProxy) -> Result<Vec<String>, Error> {
    let entries = files_async::readdir(&root_proxy).await?;
    let mut items = entries.iter().map(|entry| entry.name.clone()).collect::<Vec<String>>();
    items.sort();
    Ok(items)
}
