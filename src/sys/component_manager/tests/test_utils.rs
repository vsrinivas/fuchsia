// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    breakpoint_system_client::BreakpointSystemClient,
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_sys::{
        ComponentControllerEvent, EnvironmentControllerEvent, EnvironmentControllerProxy,
        EnvironmentMarker, EnvironmentOptions, FileDescriptor, LauncherProxy,
    },
    fidl_fuchsia_test_breakpoints::*,
    fuchsia_component::client::*,
    fuchsia_runtime::HandleType,
    fuchsia_zircon as zx,
    futures::future,
    futures::stream::{StreamExt, TryStreamExt},
    parking_lot::{Condvar, Mutex},
    rand::random,
    std::fs::*,
    std::path::PathBuf,
    std::{fs::File, io::Read, sync::Arc, thread, time::Duration},
};

/// This static string is used by BlackBoxTest to start Component Manager v2.
/// A fuchsia system is expected to have Component Manager v2 at this URL for
/// BlackBoxTest::default to work correctly.
pub static COMPONENT_MANAGER_URL: &str =
    "fuchsia-pkg://fuchsia.com/component_manager#meta/component_manager.cmx";

/// This structure contains all elements responsible for a black-box test
/// of Component Manager v2. If this object is dropped, the component manager
/// will be killed, ending the test.
///
/// Any component using BlackBoxTest must satisfy the following minimal requirements
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
/// BlackBoxTest passes them into component manager from the parent component.
///
/// The Environment service is required to ensure hermeticity. This is important
/// for integration tests, where a single component runs multiple tests simultaneously.
/// There will be multiple component managers and they must all run in isolated environments.
///
/// The hub is required to access the Breakpoints FIDL service exposed by
/// component manager when the "--debug" flag is passed in.
///
/// Usage: /src/sys/component_manager/tests contains multiple tests such as
/// routing, base_resolver and shutdown which use this class.

pub struct BlackBoxTest {
    /// The environment under which component manager runs
    pub env: EnvironmentControllerProxy,

    /// The app that handles component manager
    pub component_manager_app: App,

    /// The file that stores the output from component manager.
    /// Since component manager itself has no output, this is usually the
    /// output from the components started by component manager.
    pub out_file: File,

    /// The breakpoints FIDL service connected to component manager.
    /// An integration test can use this service to halt tasks of component manager
    /// at various points during its execution.
    pub breakpoint_system: BreakpointSystemClient,

    /// The path from Hub v1 to component manager.
    /// To get to Hub v2, append "out/hub" to this path.
    pub component_manager_path: PathBuf,
}

impl BlackBoxTest {
    /// Creates a black box test with the default component manager URL and no
    /// additional directory handles that are passed in to component manager.
    /// At the end of this function call, a component manager has been created
    /// in a hermetic environment and its execution is halted.
    pub async fn default(root_component_url: &str) -> Result<Self, Error> {
        Self::custom(COMPONENT_MANAGER_URL, root_component_url, vec![]).await
    }

    pub async fn custom(
        component_manager_url: &str,
        root_component_url: &str,
        dir_handles: Vec<(String, zx::Handle)>,
    ) -> Result<Self, Error> {
        // Use a random integer to identify this component manager
        let random_num = random::<u32>();
        let label = format!("test_{}", random_num);

        let (env, launcher) = create_isolated_environment(&label).await?;
        let (component_manager_app, out_file) = launch_component_manager(
            launcher,
            component_manager_url,
            root_component_url,
            dir_handles,
        )
        .await?;
        let component_manager_path = find_component_manager_in_hub(component_manager_url, &label);
        let breakpoint_system = connect_to_breakpoint_system(&component_manager_path).await?;

        let test = Self {
            env,
            component_manager_app,
            out_file,
            breakpoint_system,
            component_manager_path,
        };

        Ok(test)
    }
}

/// Starts component manager, launches the provided v2 component and expects the provided output.
/// The output may have already arrived or is expected to arrive within a constant amount
/// of time (defined by WAIT_TIMEOUT_SEC) after invoking this function.
pub async fn launch_component_and_expect_output(
    root_component_url: &str,
    expected_output: String,
) -> Result<(), Error> {
    launch_component_and_expect_output_with_extra_dirs(root_component_url, vec![], expected_output)
        .await
}

/// Starts component manager, attaches the provided directory handles, launches the provided
/// v2 component and expects the provided output.
/// The output may have already arrived or is expected to arrive within a constant amount
/// of time (defined by WAIT_TIMEOUT_SEC) after invoking this function.
pub async fn launch_component_and_expect_output_with_extra_dirs(
    root_component_url: &str,
    dir_handles: Vec<(String, zx::Handle)>,
    expected_output: String,
) -> Result<(), Error> {
    let test = BlackBoxTest::custom(COMPONENT_MANAGER_URL, root_component_url, dir_handles).await?;
    test.breakpoint_system.register(vec![]).await?;
    read_from_pipe(test.out_file, expected_output)
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
/// v2 component to start and the debug flag. Blocks until the out directory of component
/// manager is set up.
async fn launch_component_manager(
    launcher: LauncherProxy,
    component_manager_url: &str,
    root_component_url: &str,
    dir_handles: Vec<(String, zx::Handle)>,
) -> Result<(App, std::fs::File), Error> {
    // Create a pipe for the stdout from component manager
    let (file, pipe_handle) = make_pipe();
    let mut options = LaunchOptions::new();
    options.set_out(pipe_handle);

    // Add in any provided directory handles to component manager's namespace
    for dir in dir_handles {
        options.add_handle_to_namespace(dir.0, dir.1);
    }

    // Start component manager, giving the debug flag and the root component URL.
    let component_manager_app = launch_with_options(
        &launcher,
        component_manager_url.to_string(),
        Some(vec![root_component_url.to_string(), "--debug".to_string()]),
        options,
    )
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

    Ok((component_manager_app, file))
}

/// Use the path to component manager's hub to find the Breakpoint service
/// and connect to it
async fn connect_to_breakpoint_system(
    component_manager_path: &PathBuf,
) -> Result<BreakpointSystemClient, Error> {
    let path_to_svc = component_manager_path.join("out/svc");
    let path_to_svc = path_to_svc.to_str().expect("found invalid chars");
    let proxy = connect_to_service_at::<BreakpointSystemMarker>(path_to_svc)
        .context("could not connect to BreakpointSystem service")?;
    Ok(BreakpointSystemClient::from_proxy(proxy))
}

/// Explore the hub to find the component manager running in the environment
/// marked by the provided label.
fn find_component_manager_in_hub(component_manager_url: &str, label: &str) -> PathBuf {
    let path_to_env = format!("/hub/r/{}", label);

    // Get the id for the environment
    let dir: Vec<DirEntry> = read_dir(path_to_env)
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
        .expect("could not open component manager in the hub")
        .map(|x| x.expect("entry unreadable"))
        .collect();

    // There can only be one component manager inside this environment.
    // So there should be only one id.
    assert_eq!(dir.len(), 1);

    dir[0].path()
}

/// The maximum time that read_from_pipe will wait for a message to appear in a file.
/// After this time elapses, an error is returned by the function.
const WAIT_TIMEOUT_SEC: u64 = 10;

fn make_pipe() -> (std::fs::File, FileDescriptor) {
    match fdio::pipe_half() {
        Err(_) => panic!("failed to create pipe"),
        Ok((pipe, handle)) => {
            let pipe_handle = FileDescriptor {
                type0: HandleType::FileDescriptor as i32,
                type1: 0,
                type2: 0,
                handle0: Some(handle.into()),
                handle1: None,
                handle2: None,
            };
            (pipe, pipe_handle)
        }
    }
}

fn read_from_pipe(mut f: File, expected_msg: String) -> Result<(), Error> {
    let pair = Arc::new((Mutex::new(Vec::new()), Condvar::new()));

    // This uses a blocking std::file::File::read call, so we can't use async or the timeout below
    // will never trigger since the read doesn't yield. Need to spawn a thread.
    // TODO: Improve this to use async I/O and replace the thread with an async closure.
    {
        let pair = pair.clone();
        let expected_msg = expected_msg.clone();
        thread::spawn(move || {
            let expected = expected_msg.as_bytes();
            let mut buf = [0; 1024];
            loop {
                let n = f.read(&mut buf).expect("failed to read pipe");

                let (actual, cond) = &*pair;
                let mut actual = actual.lock();
                actual.extend_from_slice(&buf[0..n]);

                // If the read data equals expected message, return early; the test passed. Otherwise
                // keep gathering data until the timeout is reached. This allows tests to print info
                // about failed expectations, even though this is most often used with
                // component_manager.cmx which doesn't exit (so we can't just get output on exit).
                if &**actual == expected {
                    cond.notify_one();
                    return;
                }
            }
        });
    }

    // parking_lot::Condvar has no spurious wakeups, yay!
    let (actual, cond) = &*pair;
    let mut actual = actual.lock();
    if cond.wait_for(&mut actual, Duration::from_secs(WAIT_TIMEOUT_SEC)).timed_out() {
        let actual_msg = String::from_utf8(actual.clone())
            .map(|v| format!("'{}'", v))
            .unwrap_or(format!("{:?}", actual));

        return Err(format_err!(
            "Timed out waiting for matching output\n\
             Expected: '{}'\n\
             Actual: {}",
            expected_msg,
            actual_msg,
        ));
    }
    Ok(())
}
