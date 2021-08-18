// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{DiscoverableProtocolMarker, Proxy, ServerEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_realm_builder as ftrb, fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::{future::BoxFuture, TryStreamExt},
    io_util,
    log::*,
    std::{collections::HashMap, path::Path, sync::Arc},
};

pub const MOCK_ID_KEY: &'static str = "mock_id";
pub const RUNNER_NAME: &'static str = "realm_builder";

/// The implementation for a mock component. The contained function is called when the framework
/// asks the mock to run, and the function is given the component's handles for its namespace and
/// outgoing directory. The mock component may then use this handles to run a ServiceFs, access
/// capabilities as the mock, or perform other such actions.
#[derive(Clone)]
pub struct Mock(
    Arc<dyn Fn(MockHandles) -> BoxFuture<'static, Result<(), Error>> + Sync + Send + 'static>,
);

impl Mock {
    /// Creates a new `Mock`. The `mock_fn` must be a function which takes a `MockHandles` struct
    /// and returnes a pinned and boxed future. This future will be polled so long as the
    /// [`RealmInstance`] this mock is instantiated in is still alive.
    ///
    /// The pinned and boxed future can be constructed using [`Box::pin`] with an asynchronous
    /// function or block.
    ///
    /// ```
    /// let mock1 = Mock::new(move |m: MockHandles| { Box::pin(some_async_fn(m)) });
    /// let mock2 = Mock::new(move |m: MockHandles| { Box::pin(async move {
    ///     let service_proxy = m.connect_to_service::<SomeServiceMarker>()?;
    ///     service_proxy.some_function()?;
    ///     Ok(())
    /// })});
    /// ```
    pub fn new<M>(mock_fn: M) -> Self
    where
        M: Fn(MockHandles) -> BoxFuture<'static, Result<(), Error>> + Sync + Send + 'static,
    {
        Mock(Arc::new(mock_fn))
    }
}

/// The handles from the framework over which the mock should interact with other components.
pub struct MockHandles {
    namespace: HashMap<String, fio::DirectoryProxy>,

    /// The outgoing directory handle for a mock component. This can be used to run a ServiceFs for
    /// the mock.
    pub outgoing_dir: ServerEnd<fio::DirectoryMarker>,
}

impl MockHandles {
    /// Connects to a FIDL protocol and returns a proxy to that protocol.
    pub fn connect_to_service<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error> {
        let svc_dir_proxy = self
            .namespace
            .get(&"/svc".to_string())
            .ok_or(format_err!("the mock's namespace doesn't have a /svc directory"))?;
        let node_proxy = io_util::open_node(
            svc_dir_proxy,
            Path::new(P::PROTOCOL_NAME),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )?;
        Ok(P::Proxy::from_channel(node_proxy.into_channel().unwrap()))
    }

    /// Clones a directory from the mock's namespace.
    ///
    /// Note that this function only works on exact matches from the namespace. For example if the
    /// namespace had a `data` entry in it, and the caller wished to open the subdirectory at
    /// `data/assets`, then this function should be called with the argument `data` and the
    /// returned `DirectoryProxy` would then be used to open the subdirectory `assets`. In this
    /// scenario, passing `data/assets` in its entirety to this function would fail.
    ///
    /// ```
    /// let data_dir = mock_handles.clone_from_namespace("data")?;
    /// let assets_dir = io_util::open_directory(&data_dir, Path::new("assets"), ...)?;
    /// ```
    pub fn clone_from_namespace(&self, directory_name: &str) -> Result<fio::DirectoryProxy, Error> {
        let dir_proxy = self.namespace.get(&format!("/{}", directory_name)).ok_or(format_err!(
            "the mock's namespace doesn't have a /{} directory",
            directory_name
        ))?;
        io_util::clone_directory(dir_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
    }
}

impl From<ftrb::MockComponentStartInfo> for MockHandles {
    fn from(fidl_mock_handles: ftrb::MockComponentStartInfo) -> Self {
        let namespace = fidl_mock_handles
            .ns
            .unwrap()
            .into_iter()
            .map(|namespace_entry| {
                (
                    namespace_entry.path.unwrap(),
                    namespace_entry.directory.unwrap().into_proxy().unwrap(),
                )
            })
            .collect::<HashMap<_, _>>();
        Self { namespace, outgoing_dir: fidl_mock_handles.outgoing_dir.unwrap() }
    }
}

pub struct MocksRunner {
    pub(crate) mocks: Arc<Mutex<HashMap<String, Mock>>>,

    // We want the async task handling run requests from the framework intermediary to run as long
    // as this MocksRunner is alive, so hold on to the task for it in this struct.
    //
    // This is in an option because we want to be able to take this task during
    // `RealmInstance::destroy` in order to keep mocks running during the realm shutdown.
    pub(crate) event_stream_handling_task: Option<fasync::Task<()>>,
}

impl MocksRunner {
    pub fn new(
        framework_intermediary_event_stream: ftrb::FrameworkIntermediaryEventStream,
    ) -> Self {
        let mocks = Arc::new(Mutex::new(HashMap::new()));
        let event_stream_handling_task = Some(Self::run_event_stream_handling_task(
            mocks.clone(),
            framework_intermediary_event_stream,
        ));
        Self { mocks, event_stream_handling_task }
    }

    pub async fn register_mock(&self, mock_id: String, mock: Mock) {
        let mut mocks_guard = self.mocks.lock().await;
        mocks_guard.insert(mock_id, mock);
    }

    /// Takes ownership of the task containing the mocks runner and all running mock components.
    /// Useful if the RealmInstance this is part of is being destroyed and we want to wait for
    /// realm destruction to complete before stopping the mock components.
    pub(crate) fn take_runner_task(&mut self) -> Option<fasync::Task<()>> {
        self.event_stream_handling_task.take()
    }

    fn run_event_stream_handling_task(
        mocks: Arc<Mutex<HashMap<String, Mock>>>,
        event_stream: ftrb::FrameworkIntermediaryEventStream,
    ) -> fasync::Task<()> {
        fasync::Task::local(async move {
            if let Err(e) = Self::handle_event_stream(mocks, event_stream).await {
                error!(
                    "error encountered while handling framework intermediary event stream: {:?}",
                    e
                );
            }
        })
    }

    async fn handle_event_stream(
        mocks: Arc<Mutex<HashMap<String, Mock>>>,
        mut event_stream: ftrb::FrameworkIntermediaryEventStream,
    ) -> Result<(), Error> {
        let running_mocks = Arc::new(Mutex::new(HashMap::new()));
        while let Some(req) = event_stream.try_next().await? {
            match req {
                ftrb::FrameworkIntermediaryEvent::OnMockRunRequest { mock_id, start_info } => {
                    let mock = {
                        let mut mocks_guard = mocks.lock().await;
                        let mock = mocks_guard
                            .get_mut(&mock_id)
                            .ok_or(format_err!("no such mock: {:?}", mock_id))?;
                        mock.clone()
                    };
                    let mock_handles = start_info.into();
                    let mut running_mocks_guard = running_mocks.lock().await;
                    if running_mocks_guard.contains_key(&mock_id) {
                        return Err(format_err!(
                            "failed to start mock {:?} because it is already running",
                            mock_id
                        ));
                    }
                    running_mocks_guard.insert(
                        mock_id.clone(),
                        fasync::Task::local(async move {
                            if let Err(e) = (*mock.0)(mock_handles).await {
                                error!("error running mock: {:?}", e);
                            }
                        }),
                    );
                }
                ftrb::FrameworkIntermediaryEvent::OnMockStopRequest { mock_id } => {
                    if running_mocks.lock().await.remove(&mock_id).is_none() {
                        return Err(format_err!(
                            "failed to stop mock {:?} because it wasn't running"
                        ));
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_endpoints, create_proxy},
        fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_component_runner as fcrunner,
        fuchsia_component::server as fserver,
        futures::{channel::oneshot, StreamExt},
        maplit::hashmap,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, path::Path as VfsPath, pseudo_directory,
        },
    };

    #[fasync::run_until_stalled(test)]
    async fn mock_handles_clone_from_namespace() {
        let dir_name = "data";
        let file_name = "example_file";
        let file_contents = "example contents";

        let (_outgoing_dir_client_end, outgoing_dir_server_end) = create_endpoints().unwrap();

        let data_dir = pseudo_directory!(
            file_name => read_only_static(file_contents.to_string().into_bytes()),
        );

        let (data_dir_proxy, data_dir_server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        data_dir.open(
            ExecutionScope::new(),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            VfsPath::dot(),
            data_dir_server_end.into_channel().into(),
        );

        let mock_handles = MockHandles {
            namespace: hashmap! {
                format!("/{}", dir_name) => data_dir_proxy,
            },
            outgoing_dir: outgoing_dir_server_end,
        };

        let data_dir_clone =
            mock_handles.clone_from_namespace("data").expect("failed to clone from namespace");

        let file_proxy =
            io_util::open_file(&data_dir_clone, Path::new(file_name), fio::OPEN_RIGHT_READABLE)
                .expect("failed to open file");
        assert_eq!(
            file_contents,
            &io_util::read_file(&file_proxy).await.expect("failed to read file")
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn mocks_are_run() {
        let (framework_intermediary_proxy, framework_intermediary_server_end) =
            create_proxy::<ftrb::FrameworkIntermediaryMarker>().unwrap();

        let (_framework_intermediary_request_stream, framework_intermediary_server_control_handle) =
            framework_intermediary_server_end.into_stream_and_control_handle().unwrap();

        let mocks_runner = MocksRunner::new(framework_intermediary_proxy.take_event_stream());

        // Register a mock

        let mock_id_1 = "mocked mock 1".to_string();

        let (signal_mock_1_called, receive_mock_1_called) = oneshot::channel();
        let signal_mock_1_called = Arc::new(Mutex::new(Some(signal_mock_1_called)));

        mocks_runner
            .register_mock(
                mock_id_1.clone(),
                Mock::new(move |_mock_handles: MockHandles| {
                    let signal_mock_1_called = signal_mock_1_called.clone();
                    Box::pin(async move {
                        signal_mock_1_called
                            .lock()
                            .await
                            .take()
                            .expect("the mock was called twice")
                            .send(())
                            .expect("failed to signal that the mock was called");
                        Ok(())
                    })
                }),
            )
            .await;

        // Register a second mock

        let mock_id_2 = "mocked mock 2".to_string();

        let (signal_mock_2_called, receive_mock_2_called) = oneshot::channel();
        let signal_mock_2_called = Arc::new(Mutex::new(Some(signal_mock_2_called)));

        mocks_runner
            .register_mock(
                mock_id_2.clone(),
                Mock::new(move |_mock_handles: MockHandles| {
                    let signal_mock_2_called = signal_mock_2_called.clone();
                    Box::pin(async move {
                        signal_mock_2_called
                            .lock()
                            .await
                            .take()
                            .expect("the mock was called twice")
                            .send(())
                            .expect("failed to signal that the mock was called");
                        Ok(())
                    })
                }),
            )
            .await;

        // Tell the mock runner to run the second mock, and observe that its called

        let (_ignored, outgoing_dir) = create_proxy::<fio::DirectoryMarker>().unwrap();

        framework_intermediary_server_control_handle
            .send_on_mock_run_request(
                &mock_id_2,
                ftrb::MockComponentStartInfo {
                    ns: Some(vec![]),
                    outgoing_dir: Some(outgoing_dir),
                    ..ftrb::MockComponentStartInfo::EMPTY
                },
            )
            .unwrap();

        receive_mock_2_called.await.unwrap();

        // Tell the mock runner to run the first mock, and observe that its called

        let (_ignored, outgoing_dir) = create_proxy::<fio::DirectoryMarker>().unwrap();

        framework_intermediary_server_control_handle
            .send_on_mock_run_request(
                &mock_id_1,
                ftrb::MockComponentStartInfo {
                    ns: Some(vec![]),
                    outgoing_dir: Some(outgoing_dir),
                    ..ftrb::MockComponentStartInfo::EMPTY
                },
            )
            .unwrap();

        receive_mock_1_called.await.unwrap();
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_handles_service_connection() {
        let (svc_client_end, svc_server_end) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (_ignored, outgoing_dir) = create_endpoints::<fio::DirectoryMarker>().unwrap();

        let fidl_mock_handles = ftrb::MockComponentStartInfo {
            ns: Some(vec![fcrunner::ComponentNamespaceEntry {
                path: Some("/svc".to_string()),
                directory: Some(svc_client_end),
                ..fcrunner::ComponentNamespaceEntry::EMPTY
            }]),
            outgoing_dir: Some(outgoing_dir),
            ..ftrb::MockComponentStartInfo::EMPTY
        };

        let mock_handles = MockHandles::from(fidl_mock_handles);

        // Run a ServiceFs on svc_server_end

        let mut fs = fserver::ServiceFs::new_local();
        fs.add_fidl_service(move |mut stream: fecho::EchoRequestStream| {
            fasync::Task::local(async move {
                while let Some(fecho::EchoRequest::EchoString { value, responder }) =
                    stream.try_next().await.expect("failed to serve echo service")
                {
                    responder
                        .send(value.as_ref().map(|s| &**s))
                        .expect("failed to send echo response");
                }
            })
            .detach();
        });
        fs.serve_connection(svc_server_end.into_channel()).unwrap();
        let _service_fs_task = fasync::Task::local(fs.collect::<()>());

        // Connect to the ServiceFs through our mock handles, and use the echo server

        let echo_client = mock_handles.connect_to_service::<fecho::EchoMarker>().unwrap();
        let string_to_echo = "this is an echo'd string".to_string();
        assert_eq!(
            Some(string_to_echo.clone()),
            echo_client.echo_string(Some(&string_to_echo)).await.unwrap()
        );
    }
}
