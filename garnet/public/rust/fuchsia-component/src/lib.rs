// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connect to or provide Fuchsia services.

#![deny(missing_docs)]

#[allow(unused)] // Remove pending fix to rust-lang/rust#53682
use {
    byteorder::{LittleEndian, WriteBytesExt},
    failure::{bail, Error, Fail, ResultExt},
    fidl::{
        encoding::{Decodable, OutOfLine},
        endpoints::{Proxy, RequestStream, ServiceMarker},
    },
    fidl_fuchsia_io::{
        DirectoryObject, DirectoryRequest, DirectoryRequestStream, NodeAttributes, NodeInfo,
        NodeMarker, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys::{
        ComponentControllerEvent, ComponentControllerProxy, EnvironmentControllerProxy,
        EnvironmentMarker, EnvironmentOptions, EnvironmentProxy, FileDescriptor, FlatNamespace,
        LaunchInfo, LauncherMarker, LauncherProxy, LoaderMarker, ServiceList, TerminationReason,
    },
    fuchsia_async as fasync,
    fuchsia_runtime::HandleType,
    fuchsia_zircon::{self as zx, Peered, Signals, Socket, SocketOpts},
    futures::{
        future::{self, FutureExt, TryFutureExt},
        io::AsyncReadExt,
        ready,
        stream::{FuturesUnordered, StreamExt, StreamFuture, TryStreamExt},
        task::Context,
        Future, Poll, Stream,
    },
    std::{
        collections::hash_map::{Entry, HashMap},
        fs::File,
        io::Write,
        marker::{PhantomData, Unpin},
        os::unix::io::IntoRawFd,
        pin::Pin,
        sync::Arc,
    },
};

/// Creates an `&'static str` containing the URL of a Fuchsia package
/// from a string literal containng the name of a fuchsia component
/// containing only a single package.
///
/// e.g. `fuchsia_single_component_package_url!("my_server")` would
/// create `fuchsia-pkg://fuchsia.com/my_server#meta/my_server.cmx`.
#[macro_export]
macro_rules! fuchsia_single_component_package_url {
    ($component_name:expr) => {
        concat!("fuchsia-pkg://fuchsia.com/", $component_name, "#meta/", $component_name, ".cmx",)
    };
}

/// Tools for starting or connecting to existing Fuchsia applications and services.
pub mod client {
    use super::*;

    /// Connect to a FIDL service using the provided channel and namespace prefix.
    pub fn connect_channel_to_service_at<S: ServiceMarker>(
        server_end: zx::Channel,
        service_prefix: &str,
    ) -> Result<(), Error> {
        let service_path = format!("{}/{}", service_prefix, S::NAME);
        fdio::service_connect(&service_path, server_end)
            .with_context(|_| format!("Error connecting to service path: {}", service_path))?;
        Ok(())
    }

    /// Connect to a FIDL service using the provided channel.
    pub fn connect_channel_to_service<S: ServiceMarker>(
        server_end: zx::Channel,
    ) -> Result<(), Error> {
        connect_channel_to_service_at::<S>(server_end, "/svc")
    }

    /// Connect to a FIDL service using the provided namespace prefix.
    pub fn connect_to_service_at<S: ServiceMarker>(
        service_prefix: &str,
    ) -> Result<S::Proxy, Error> {
        let (proxy, server) = zx::Channel::create()?;
        connect_channel_to_service_at::<S>(server, service_prefix)?;
        let proxy = fasync::Channel::from_channel(proxy)?;
        Ok(S::Proxy::from_channel(proxy))
    }

    /// Connect to a FIDL service using the application root namespace.
    pub fn connect_to_service<S: ServiceMarker>() -> Result<S::Proxy, Error> {
        connect_to_service_at::<S>("/svc")
    }

    /// Adds a new directory to the namespace for the new process.
    pub fn add_dir_to_namespace(
        namespace: &mut FlatNamespace,
        path: String,
        dir: File,
    ) -> Result<(), Error> {
        let handle = fdio::transfer_fd(dir)?;
        namespace.paths.push(path);
        namespace.directories.push(zx::Channel::from(handle));

        Ok(())
    }

    /// Returns a connection to the application launcher service.
    pub fn launcher() -> Result<LauncherProxy, Error> {
        connect_to_service::<LauncherMarker>()
    }

    /// Launch an application at the specified URL.
    pub fn launch(
        launcher: &LauncherProxy,
        url: String,
        arguments: Option<Vec<String>>,
    ) -> Result<App, Error> {
        launch_with_options(launcher, url, arguments, LaunchOptions::new())
    }

    /// Options for the launcher when starting an applications.
    pub struct LaunchOptions {
        namespace: Option<Box<FlatNamespace>>,
        out: Option<Box<FileDescriptor>>,
    }

    impl LaunchOptions {
        /// Creates default launch options.
        pub fn new() -> LaunchOptions {
            LaunchOptions { namespace: None, out: None }
        }

        /// Adds a new directory to the namespace for the new process.
        pub fn add_dir_to_namespace(
            &mut self,
            path: String,
            dir: File,
        ) -> Result<&mut Self, Error> {
            let handle = fdio::transfer_fd(dir)?;
            let namespace = self.namespace.get_or_insert_with(|| {
                Box::new(FlatNamespace { paths: vec![], directories: vec![] })
            });
            namespace.paths.push(path);
            namespace.directories.push(zx::Channel::from(handle));

            Ok(self)
        }

        /// Sets the out handle.
        pub fn set_out(&mut self, f: FileDescriptor) {
            self.out = Some(Box::new(f));
        }
    }

    /// Launch an application at the specified URL.
    pub fn launch_with_options(
        launcher: &LauncherProxy,
        url: String,
        arguments: Option<Vec<String>>,
        options: LaunchOptions,
    ) -> Result<App, Error> {
        let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
        let (directory_request, directory_server_chan) = zx::Channel::create()?;
        let directory_request = Arc::new(directory_request);
        let mut launch_info = LaunchInfo {
            url,
            arguments,
            out: options.out,
            err: None,
            directory_request: Some(directory_server_chan),
            flat_namespace: options.namespace,
            additional_services: None,
        };
        launcher
            .create_component(&mut launch_info, Some(controller_server_end.into()))
            .context("Failed to start a new Fuchsia application.")?;
        Ok(App { directory_request, controller, stdout: None, stderr: None })
    }

    /// `App` represents a launched application.
    ///
    /// When `App` is dropped, launched application will be terminated.
    #[must_use = "Dropping `App` will cause the application to be terminated."]
    pub struct App {
        // directory_request is a directory protocol channel
        directory_request: Arc<zx::Channel>,

        // Keeps the component alive until `App` is dropped.
        controller: ComponentControllerProxy,

        //TODO pub accessors to take stdout/stderr in wrapper types that implement AsyncRead.
        stdout: Option<fasync::Socket>,
        stderr: Option<fasync::Socket>,
    }

    impl App {
        /// Returns a reference to the directory protocol channel of the application.
        #[inline]
        pub fn directory_channel(&self) -> &zx::Channel {
            &self.directory_request
        }

        /// Returns a reference to the component controller.
        #[inline]
        pub fn controller(&self) -> &ComponentControllerProxy {
            &self.controller
        }

        /// Connect to a service provided by the `App`.
        #[inline]
        pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
            let (client_channel, server_channel) = zx::Channel::create()?;
            self.pass_to_service::<S>(server_channel)?;
            Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
        }

        /// Connect to a service by passing a channel for the server.
        #[inline]
        pub fn pass_to_service<S: ServiceMarker>(
            &self,
            server_channel: zx::Channel,
        ) -> Result<(), Error> {
            self.pass_to_named_service(S::NAME, server_channel)
        }

        /// Connect to a service by name.
        #[inline]
        pub fn pass_to_named_service(
            &self,
            service_name: &str,
            server_channel: zx::Channel,
        ) -> Result<(), Error> {
            fdio::service_connect_at(&self.directory_request, service_name, server_channel)?;
            Ok(())
        }

        /// Forces the component to exit.
        pub fn kill(&mut self) -> Result<(), fidl::Error> {
            self.controller.kill()
        }

        /// Wait for the component to terminate and return its exit status.
        pub fn wait(&mut self) -> impl Future<Output = Result<ExitStatus, Error>> {
            self.controller
                .take_event_stream()
                .try_filter_map(|event| {
                    future::ready(match event {
                        ComponentControllerEvent::OnTerminated {
                            return_code,
                            termination_reason,
                        } => Ok(Some(ExitStatus { return_code, termination_reason })),
                        _ => Ok(None),
                    })
                })
                .into_future()
                .map(|(next, _rest)| match next {
                    Some(result) => result.map_err(|err| err.into()),
                    _ => Ok(ExitStatus {
                        return_code: -1,
                        termination_reason: TerminationReason::Unknown,
                    }),
                })
        }

        /// Wait for the component to terminate and return its exit status, stdout, and stderr.
        pub fn wait_with_output(mut self) -> impl Future<Output = Result<Output, Error>> {
            let drain = |pipe: Option<fasync::Socket>| match pipe {
                None => future::ready(Ok(vec![])).left_future(),
                Some(pipe) => pipe
                    .take_while(|maybe_result| {
                        future::ready(match maybe_result {
                            Err(zx::Status::PEER_CLOSED) => false,
                            _ => true,
                        })
                    })
                    .try_concat()
                    .map_err(|err| err.into())
                    .right_future(),
            };

            future::try_join3(self.wait(), drain(self.stdout), drain(self.stderr))
                .map_ok(|(exit_status, stdout, stderr)| Output { exit_status, stdout, stderr })
        }
    }

    /// A component builder, providing a simpler interface to
    /// [`fidl_fuchsia_sys::LauncherProxy::create_component`].
    ///
    /// `AppBuilder`s interface matches
    /// [`std:process:Command`](https://doc.rust-lang.org/std/process/struct.Command.html) as
    /// closely as possible, except where the semantics of spawning a process differ from the
    /// semantics of spawning a Fuchsia component:
    ///
    /// * Fuchsia components do not support stdin, a current working directory, or environment
    /// variables.
    ///
    /// * `AppBuilder` will move certain handles into the spawned component (see
    /// [`AppBuilder::add_dir_to_namespace`]), so a single instance of `AppBuilder` can only be
    /// used to create a single component.
    #[derive(Debug)]
    pub struct AppBuilder {
        launch_info: LaunchInfo,
        directory_request: Option<Arc<zx::Channel>>,
        stdout: Option<Stdio>,
        stderr: Option<Stdio>,
    }

    impl AppBuilder {
        /// Creates a new `AppBuilder` for the component referenced by the given `url`.
        pub fn new(url: impl Into<String>) -> Self {
            Self {
                launch_info: LaunchInfo {
                    url: url.into(),
                    arguments: None,
                    out: None,
                    err: None,
                    directory_request: None,
                    flat_namespace: None,
                    additional_services: None,
                },
                directory_request: None,
                stdout: None,
                stderr: None,
            }
        }

        /// Returns a reference to the local end of the component's directory_request channel,
        /// creating it if necessary.
        pub fn directory_request(&mut self) -> Result<&Arc<zx::Channel>, Error> {
            Ok(match self.directory_request {
                Some(ref channel) => channel,
                None => {
                    let (directory_request, directory_server_chan) = zx::Channel::create()?;
                    let directory_request = Arc::new(directory_request);
                    self.launch_info.directory_request = Some(directory_server_chan);
                    self.directory_request = Some(directory_request);
                    self.directory_request.as_ref().unwrap()
                }
            })
        }

        /// Configures stdout for the new process.
        pub fn stdout(mut self, cfg: impl Into<Stdio>) -> Self {
            self.stdout = Some(cfg.into());
            self
        }

        /// Configures stderr for the new process.
        pub fn stderr(mut self, cfg: impl Into<Stdio>) -> Self {
            self.stderr = Some(cfg.into());
            self
        }

        /// Mounts an opened directory in the namespace of the component.
        pub fn add_dir_to_namespace(mut self, path: String, dir: File) -> Result<Self, Error> {
            let handle = fdio::transfer_fd(dir)?;
            let namespace = self.launch_info.flat_namespace.get_or_insert_with(|| {
                Box::new(FlatNamespace { paths: vec![], directories: vec![] })
            });
            namespace.paths.push(path);
            namespace.directories.push(zx::Channel::from(handle));

            Ok(self)
        }

        /// Append the given `arg` to the sequence of arguments passed to the new process.
        pub fn arg(mut self, arg: impl Into<String>) -> Self {
            self.launch_info.arguments.get_or_insert_with(Vec::new).push(arg.into());
            self
        }

        /// Append all the given `args` to the sequence of arguments passed to the new process.
        pub fn args(mut self, args: impl IntoIterator<Item = impl Into<String>>) -> Self {
            self.launch_info
                .arguments
                .get_or_insert_with(Vec::new)
                .extend(args.into_iter().map(|arg| arg.into()));
            self
        }

        /// Launch the component using the provided `launcher` proxy, returning the launched
        /// application or the error encountered while launching it.
        ///
        /// By default:
        /// * when the returned [`App`] is dropped, the launched application will be terminated.
        /// * stdout and stderr will use the the default stdout and stderr for the environment.
        pub fn spawn(self, launcher: &LauncherProxy) -> Result<App, Error> {
            self.launch(launcher, Stdio::Inherit)
        }

        /// Launches the component using the provided `launcher` proxy, waits for it to finish, and
        /// collects all of its output.
        ///
        /// By default, stdout and stderr are captured (and used to provide the resulting output).
        pub fn output(
            self,
            launcher: &LauncherProxy,
        ) -> Result<impl Future<Output = Result<Output, Error>>, Error> {
            Ok(self.launch(launcher, Stdio::MakePipe)?.wait_with_output())
        }

        /// Launches the component using the provided `launcher` proxy, waits for it to finish, and
        /// collects its exit status.
        ///
        /// By default, stdout and stderr will use the default stdout and stderr for the
        /// environment.
        pub fn status(
            self,
            launcher: &LauncherProxy,
        ) -> Result<impl Future<Output = Result<ExitStatus, Error>>, Error> {
            Ok(self.launch(launcher, Stdio::Inherit)?.wait())
        }

        fn launch(mut self, launcher: &LauncherProxy, default: Stdio) -> Result<App, Error> {
            let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
            let directory_request = if let Some(directory_request) = self.directory_request.take() {
                directory_request
            } else {
                let (directory_request, directory_server_chan) = zx::Channel::create()?;
                self.launch_info.directory_request = Some(directory_server_chan);
                Arc::new(directory_request)
            };

            let (stdout, remote_stdout) =
                self.stdout.as_ref().unwrap_or(&default).to_local_and_remote()?;
            if let Some(fd) = remote_stdout {
                self.launch_info.out = Some(Box::new(fd));
            }

            let (stderr, remote_stderr) =
                self.stderr.as_ref().unwrap_or(&default).to_local_and_remote()?;
            if let Some(fd) = remote_stderr {
                self.launch_info.err = Some(Box::new(fd));
            }

            launcher
                .create_component(&mut self.launch_info, Some(controller_server_end.into()))
                .context("Failed to start a new Fuchsia application.")?;

            Ok(App { directory_request, controller, stdout, stderr })
        }
    }

    /// Describes what to do with a standard I/O stream for a child component.
    #[derive(Debug)]
    pub enum Stdio {
        /// Use the default output sink for the environment.
        Inherit,
        /// Provide a socket to the component to write output to.
        MakePipe,
    }

    impl Stdio {
        fn to_local_and_remote(
            &self,
        ) -> Result<(Option<fasync::Socket>, Option<FileDescriptor>), Error> {
            match *self {
                Stdio::Inherit => Ok((None, None)),
                Stdio::MakePipe => {
                    let (local, remote) = Socket::create(SocketOpts::STREAM)?;
                    // local end is read-only
                    local.half_close()?;

                    let local = fasync::Socket::from_socket(local)?;
                    let remote = FileDescriptor {
                        type0: HandleType::FileDescriptor as i32,
                        type1: 0,
                        type2: 0,
                        handle0: Some(remote.into()),
                        handle1: None,
                        handle2: None,
                    };

                    Ok((Some(local), Some(remote)))
                }
            }
        }
    }

    /// Describes the result of a component after it has terminated.
    #[derive(Debug, Clone)]
    pub struct ExitStatus {
        return_code: i64,
        termination_reason: TerminationReason,
    }

    impl ExitStatus {
        /// Did the component exit without an error?  Success is defined as a reason of exited and
        /// a code of 0.
        pub fn success(&self) -> bool {
            self.exited() && self.return_code == 0
        }
        /// Returns true if the component exited, as opposed to not starting at all due to some
        /// error or terminating with any reason other than `EXITED`.
        pub fn exited(&self) -> bool {
            self.termination_reason == TerminationReason::Exited
        }
        /// The reason the component was terminated.
        pub fn reason(&self) -> TerminationReason {
            self.termination_reason
        }
        /// The return code from the component. Guaranteed to be non-zero if termination reason is
        /// not `EXITED`.
        pub fn code(&self) -> i64 {
            self.return_code
        }
    }

    /// The output of a finished component.
    pub struct Output {
        /// The exit status of the component.
        pub exit_status: ExitStatus,
        /// The data that the component wrote to stdout.
        pub stdout: Vec<u8>,
        /// The data that the component wrote to stderr.
        pub stderr: Vec<u8>,
    }
}

/// Tools for providing Fuchsia services.
pub mod server {
    use {super::*, fidl::endpoints::Proxy as _};

    /// An error indicating the startup handle on which the FIDL server
    /// attempted to start was missing.
    #[derive(Debug, Fail)]
    #[fail(display = "The startup handle on which the FIDL server attempted to start was missing.")]
    pub struct MissingStartupHandle;

    /// `Service` connects channels to service instances.
    ///
    /// Note that this trait is implemented by the `FidlService` type.
    pub trait Service {
        /// The type of the value yielded by the `spawn_service` callback.
        type Output;

        /// Create a new instance of the service on the provided `zx::Channel`.
        ///
        /// The value returned by this function will be yielded from the stream
        /// output of the `ServiceFs` type.
        fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output>;
    }

    impl<F, O> Service for F
    where
        F: FnMut(zx::Channel) -> Option<O>,
    {
        type Output = O;
        fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output> {
            (self)(channel)
        }
    }

    /// A wrapper for functions from `RequestStream` to `Output` which implements
    /// `Service`.
    ///
    /// This type throws away channels that cannot be converted to the approprate
    /// `RequestStream` type.
    pub struct FidlService<F, RS, Output>
    where
        F: FnMut(RS) -> Output,
        RS: RequestStream,
    {
        f: F,
        marker: PhantomData<fn(RS) -> Output>,
    }

    impl<F, RS, Output> From<F> for FidlService<F, RS, Output>
    where
        F: FnMut(RS) -> Output,
        RS: RequestStream,
    {
        fn from(f: F) -> Self {
            Self { f, marker: PhantomData }
        }
    }

    impl<F, RS, Output> Service for FidlService<F, RS, Output>
    where
        F: FnMut(RS) -> Output,
        RS: RequestStream,
    {
        type Output = Output;
        fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output> {
            match fasync::Channel::from_channel(channel) {
                Ok(chan) => Some((self.f)(RS::from_channel(chan))),
                Err(e) => {
                    eprintln!("ServiceFs failed to convert channel to fasync channel: {:?}", e);
                    None
                }
            }
        }
    }

    /// A `!Send` (non-thread-safe) trait object encapsulating a `Service` with
    /// the given `Output` type.
    ///
    /// Types which implement the `Service` trait can be converted to objects of
    /// this type via the `From`/`Into` traits.
    pub struct ServiceObjLocal<'a, Output>(Box<dyn Service<Output = Output> + 'a>);

    impl<'a, S: Service + 'a> From<S> for ServiceObjLocal<'a, S::Output> {
        fn from(service: S) -> Self {
            ServiceObjLocal(Box::new(service))
        }
    }

    /// A thread-safe (`Send`) trait object encapsulating a `Service` with
    /// the given `Output` type.
    ///
    /// Types which implement the `Service` trait and the `Send` trait can
    /// be converted to objects of this type via the `From`/`Into` traits.
    pub struct ServiceObj<'a, Output>(Box<dyn Service<Output = Output> + Send + 'a>);

    impl<'a, S: Service + Send + 'a> From<S> for ServiceObj<'a, S::Output> {
        fn from(service: S) -> Self {
            ServiceObj(Box::new(service))
        }
    }

    /// A trait implemented by both `ServiceObj` and `ServiceObjLocal` that
    /// allows code to be generic over thread-safety.
    ///
    /// Code that uses `ServiceObj` will require `Send` bounds but will be
    /// multithreaded-capable, while code that uses `ServiceObjLocal` will
    /// allow non-`Send` types but will be restricted to singlethreaded
    /// executors.
    pub trait ServiceObjTrait {
        /// The output type of the underlying `Service`.
        type Output;

        /// Get a mutable reference to the underlying `Service` trait object.
        fn service(&mut self) -> &mut dyn Service<Output = Self::Output>;
    }

    impl<'a, Output> ServiceObjTrait for ServiceObjLocal<'a, Output> {
        type Output = Output;

        fn service(&mut self) -> &mut dyn Service<Output = Self::Output> {
            &mut *self.0
        }
    }

    impl<'a, Output> ServiceObjTrait for ServiceObj<'a, Output> {
        type Output = Output;

        fn service(&mut self) -> &mut dyn Service<Output = Self::Output> {
            &mut *self.0
        }
    }

    enum ServiceFsNode<ServiceObjTy: ServiceObjTrait> {
        Directory {
            /// A map from filename to index in the parent `ServiceFs`.
            children: HashMap<String, usize>,
        },
        Service(ServiceObjTy),
    }

    impl<ServiceObjTy: ServiceObjTrait> ServiceFsNode<ServiceObjTy> {
        fn expect_dir(&mut self) -> &mut HashMap<String, usize> {
            match self {
                ServiceFsNode::Directory { children } => children,
                ServiceFsNode::Service(_) => panic!("ServiceFs expected directory"),
            }
        }

        fn to_dirent_type(&self) -> u8 {
            match self {
                ServiceFsNode::Directory { .. } => fidl_fuchsia_io::DIRENT_TYPE_DIRECTORY,
                ServiceFsNode::Service(_) => fidl_fuchsia_io::DIRENT_TYPE_SERVICE,
            }
        }
    }

    /// A filesystem which connects clients to services.
    ///
    /// This type implements the `Stream` trait and will yield the values
    /// returned from calling `Service::connect` on the services it hosts.
    ///
    /// This can be used to, for example, yield streams of channels, request
    /// streams, futures to run, or any other value that should be processed
    /// as the result of a request.
    #[must_use]
    pub struct ServiceFs<ServiceObjTy: ServiceObjTrait> {
        /// The open connections to this `ServiceFs`. These connections
        /// represent external clients who may attempt to open services.
        client_connections: FuturesUnordered<ClientConnection>,

        /// The tree of `ServiceFsNode`s.
        /// The root is always a directory at index 0.
        ///
        //
        // FIXME(cramertj) move to a generational index and support
        // removal of nodes.
        nodes: Vec<ServiceFsNode<ServiceObjTy>>,
    }

    const ROOT_NODE: usize = 0;
    const NO_FLAGS: u32 = 0;
    const CLONE_REQ_SUPPORTED_FLAGS: u32 =
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE;
    const OPEN_REQ_SUPPORTED_FLAGS: u32 = OPEN_RIGHT_READABLE
        | OPEN_RIGHT_WRITABLE
        | OPEN_FLAG_DESCRIBE
        | OPEN_FLAG_POSIX
        | OPEN_FLAG_DIRECTORY
        | OPEN_FLAG_NODE_REFERENCE;

    impl<'a, Output: 'a> ServiceFs<ServiceObjLocal<'a, Output>> {
        /// Create a new `ServiceFs` that is singlethreaded-only and does not
        /// require services to implement `Send`.
        pub fn new_local() -> Self {
            Self {
                client_connections: FuturesUnordered::new(),
                nodes: vec![ServiceFsNode::Directory { children: HashMap::new() }],
            }
        }
    }

    impl<'a, Output: 'a> ServiceFs<ServiceObj<'a, Output>> {
        /// Create a new `ServiceFs` that is multithreaded-capable and requires
        /// services to implement `Send`.
        pub fn new() -> Self {
            Self {
                client_connections: FuturesUnordered::new(),
                nodes: vec![ServiceFsNode::Directory { children: HashMap::new() }],
            }
        }
    }

    /// A directory within a `ServiceFs`.
    ///
    /// Services and subdirectories can be added to it.
    pub struct ServiceFsDir<'a, ServiceObjTy: ServiceObjTrait> {
        position: usize,
        fs: &'a mut ServiceFs<ServiceObjTy>,
    }

    fn dir<'a, ServiceObjTy: ServiceObjTrait>(
        fs: &'a mut ServiceFs<ServiceObjTy>,
        position: usize,
        path: String,
    ) -> ServiceFsDir<'a, ServiceObjTy> {
        let new_node_position = fs.nodes.len();
        let self_dir = fs.nodes[position].expect_dir();
        let &mut position = self_dir.entry(path.clone()).or_insert(new_node_position);
        if position == new_node_position {
            fs.nodes.push(ServiceFsNode::Directory { children: HashMap::new() });
        } else {
            if let ServiceFsNode::Service(_) = &fs.nodes[position] {
                panic!("Error adding dir to ServiceFs: existing service at \"{}\"", path)
            }
        }
        ServiceFsDir { position, fs }
    }

    fn add_service<'a, ServiceObjTy: ServiceObjTrait>(
        fs: &'a mut ServiceFs<ServiceObjTy>,
        position: usize,
        path: String,
        service: ServiceObjTy,
    ) {
        let new_node_position = fs.nodes.len();
        let self_dir = fs.nodes[position].expect_dir();
        let entry = self_dir.entry(path);
        match entry {
            Entry::Occupied(prev) => {
                panic!("Duplicate ServiceFs service added at path \"{}\"", prev.key())
            }
            Entry::Vacant(slot) => {
                slot.insert(new_node_position);
                fs.nodes.push(ServiceFsNode::Service(service));
            }
        }
    }

    /// A `Service` implementation that proxies requests
    /// to the outside environment.
    ///
    /// Not intended for direct use. Use the `add_proxy_service`
    /// function instead.
    #[doc(hidden)]
    pub struct Proxy<S, O>(PhantomData<(S, fn() -> O)>);

    impl<S: ServiceMarker, O> Service for Proxy<S, O> {
        type Output = O;
        fn connect(&mut self, channel: zx::Channel) -> Option<O> {
            if let Err(e) = crate::client::connect_channel_to_service::<S>(channel) {
                eprintln!("failed to proxy request to {}: {:?}", S::NAME, e);
            }
            None
        }
    }

    /// A `Service` implementation that proxies requests to the given component.
    ///
    /// Not intended for direct use. Use the `add_proxy_service_to` function instead.
    #[doc(hidden)]
    pub struct ProxyTo<S, O> {
        directory_request: Arc<zx::Channel>,
        _phantom: PhantomData<(S, fn() -> O)>,
    }

    impl<S: ServiceMarker, O> Service for ProxyTo<S, O> {
        type Output = O;
        fn connect(&mut self, channel: zx::Channel) -> Option<O> {
            if let Err(e) = fdio::service_connect_at(&self.directory_request, S::NAME, channel) {
                eprintln!("failed to proxy request to {}: {:?}", S::NAME, e);
            }
            None
        }
    }

    struct LaunchData {
        component_url: String,
        arguments: Option<Vec<String>>,
    }

    /// A `Service` implementation that proxies requests
    /// to a launched component.
    ///
    /// Not intended for direct use. Use the `add_component_proxy_service`
    /// function instead.
    #[doc(hidden)]
    pub struct ComponentProxy<O> {
        launch_data: Option<LaunchData>,
        launched_app: Option<crate::client::App>,
        service_name: &'static str,
        _marker: PhantomData<O>,
    }

    impl<O> Service for ComponentProxy<O> {
        type Output = O;
        fn connect(&mut self, channel: zx::Channel) -> Option<O> {
            let res = (|| {
                if let Some(LaunchData { component_url, arguments }) = self.launch_data.take() {
                    self.launched_app = Some(crate::client::launch(
                        &crate::client::launcher()?,
                        component_url,
                        arguments,
                    )?);
                }
                if let Some(app) = self.launched_app.as_ref() {
                    app.pass_to_named_service(self.service_name, channel.into())?;
                }
                Ok::<(), Error>(())
            })();
            if let Err(e) = res {
                eprintln!("ServiceFs failed to launch component: {:?}", e);
            }
            None
        }
    }

    // Not part of a trait so that clients won't have to import a trait
    // in order to call these functions.
    macro_rules! add_service_functions {
        () => {
            /// Adds a FIDL service to the directory.
            ///
            /// The FIDL service will be hosted at the name provided by the
            /// `[Discoverable]` annotation in the FIDL source.
            pub fn add_fidl_service<F, RS>(
                &mut self,
                service: F,
            ) -> &mut Self
            where
                F: FnMut(RS) -> ServiceObjTy::Output,
                RS: RequestStream,
                FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
            {
                self.add_fidl_service_at(
                    RS::Service::NAME,
                    service,
                )
            }

            /// Adds a FIDL service to the directory at the given path.
            ///
            /// The path must be a single component containing no `/` characters.
            pub fn add_fidl_service_at<F, RS>(
                &mut self,
                path: impl Into<String>,
                service: F,
            ) -> &mut Self
            where
                F: FnMut(RS) -> ServiceObjTy::Output,
                RS: RequestStream,
                FidlService<F, RS, ServiceObjTy::Output>: Into<ServiceObjTy>,
            {
                self.add_service_at(
                    path,
                    FidlService::from(service),
                )
            }

            /// Adds a service that proxies requests to the current environment.
            // NOTE: we'd like to be able to remove the type parameter `O` here,
            //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
            //  makes type checking angry.
            pub fn add_proxy_service<S: ServiceMarker, O>(&mut self) -> &mut Self
            where
                ServiceObjTy: From<Proxy<S, O>>,
                ServiceObjTy: ServiceObjTrait<Output = O>,
            {
                self.add_service_at(
                    S::NAME,
                    Proxy::<S, ServiceObjTy::Output>(PhantomData),
                )
            }

            /// Adds a service that proxies requests to the given component.
            // NOTE: we'd like to be able to remove the type parameter `O` here,
            //  but unfortunately the bound `ServiceObjTy: From<Proxy<S, ServiceObjTy::Output>>`
            //  makes type checking angry.
            pub fn add_proxy_service_to<S: ServiceMarker, O>(&mut self, directory_request: Arc<zx::Channel>) -> &mut Self
            where
                ServiceObjTy: From<ProxyTo<S, O>>,
                ServiceObjTy: ServiceObjTrait<Output = O>,
            {
                self.add_service_at(
                    S::NAME,
                    ProxyTo::<S, ServiceObjTy::Output>{
                        directory_request, _phantom: PhantomData}
                )
            }

            /// Add a service to the `ServicesServer` that will launch a component
            /// upon request, proxying requests to the launched component.
            pub fn add_component_proxy_service<O>(
                &mut self,
                service_name: &'static str,
                component_url: String,
                arguments: Option<Vec<String>>,
            ) -> &mut Self
            where
                ServiceObjTy: From<ComponentProxy<O>>,
                ServiceObjTy: ServiceObjTrait<Output = O>,
            {
                self.add_service_at(
                    service_name,
                    ComponentProxy {
                        launch_data: Some(LaunchData { component_url, arguments }),
                        launched_app: None,
                        service_name,
                        _marker: PhantomData,
                    }
                )
            }
        };
    }

    impl<'a, ServiceObjTy: ServiceObjTrait> ServiceFsDir<'a, ServiceObjTy> {
        /// Returns a reference to the subdirectory at the given path,
        /// creating one if none exists.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if a service has already been added at the given path.
        pub fn dir<'b>(&'b mut self, path: impl Into<String>) -> ServiceFsDir<'b, ServiceObjTy> {
            dir(self.fs, self.position, path.into())
        }

        add_service_functions!();

        /// Adds a service to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if any node has already been added at the given path.
        pub fn add_service_at(
            &mut self,
            path: impl Into<String>,
            service: impl Into<ServiceObjTy>,
        ) -> &mut Self {
            add_service(self.fs, self.position, path.into(), service.into());
            self
        }
    }

    impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
        /// Returns a reference to the subdirectory at the given path,
        /// creating one if none exists.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if a service has already been added at the given path.
        pub fn dir<'a>(&'a mut self, path: impl Into<String>) -> ServiceFsDir<'a, ServiceObjTy> {
            dir(self, ROOT_NODE, path.into())
        }

        add_service_functions!();

        /// Adds a service to the directory at the given path.
        ///
        /// The path must be a single component containing no `/` characters.
        ///
        /// Panics if any node has already been added at the given path.
        pub fn add_service_at(
            &mut self,
            path: impl Into<String>,
            service: impl Into<ServiceObjTy>,
        ) -> &mut Self {
            add_service(self, ROOT_NODE, path.into(), service.into());
            self
        }

        /// Start serving directory protocol service requests via a `ServiceList`.
        /// The resulting `ServiceList` can be attached to a new environment in
        /// order to provide child components with access to these services.
        pub fn host_services_list(&mut self) -> Result<ServiceList, Error> {
            let names = self.nodes[ROOT_NODE].expect_dir().keys().cloned().collect();

            let (chan1, chan2) = zx::Channel::create()?;
            self.serve_connection(chan1)?;

            Ok(ServiceList { names, provider: None, host_directory: Some(chan2) })
        }

        /// Creates a new environment that only has access to the services provided through this
        /// `ServiceFs` and the enclosing environment's `Loader` service, appending a few random
        /// bytes to the given `environment_label_prefix` to ensure this environment has a unique
        /// name.
        ///
        /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
        /// continue to exist. Once dropped, the environment and all components launched within it
        /// will be destroyed.
        pub fn create_salted_nested_environment<O>(
            &mut self,
            environment_label_prefix: &str,
        ) -> Result<NestedEnvironment, Error>
        where
            ServiceObjTy: From<Proxy<LoaderMarker, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            let mut salt = [0; 4];
            fuchsia_zircon::cprng_draw(&mut salt[..]).expect("zx_cprng_draw does not fail");
            let environment_label = format!("{}_{}", environment_label_prefix, hex::encode(&salt));
            self.create_nested_environment(&environment_label)
        }

        /// Creates a new environment that only has access to the services provided through this
        /// `ServiceFs` and the enclosing environment's `Loader` service.
        ///
        /// Note that the resulting `NestedEnvironment` must be kept alive for the environment to
        /// continue to exist. Once dropped, the environment and all components launched within it
        /// will be destroyed.
        pub fn create_nested_environment<O>(
            &mut self,
            environment_label: &str,
        ) -> Result<NestedEnvironment, Error>
        where
            ServiceObjTy: From<Proxy<LoaderMarker, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            let env = crate::client::connect_to_service::<EnvironmentMarker>()
                .context("connecting to current environment")?;
            let services_with_loader = self.add_proxy_service::<LoaderMarker, _>();
            let mut service_list = services_with_loader.host_services_list()?;

            let (new_env, new_env_server_end) = fidl::endpoints::create_proxy()?;
            let (controller, controller_server_end) = fidl::endpoints::create_proxy()?;
            let (launcher, launcher_server_end) = fidl::endpoints::create_proxy()?;
            let (directory_request, directory_server_end) = zx::Channel::create()?;

            env.create_nested_environment(
                new_env_server_end,
                controller_server_end,
                environment_label,
                Some(fidl::encoding::OutOfLine(&mut service_list)),
                &mut EnvironmentOptions {
                    inherit_parent_services: false,
                    allow_parent_runners: false,
                    kill_on_oom: false,
                    delete_storage_on_death: false,
                },
            )
            .context("creating isolated environment")?;

            new_env
                .get_launcher(launcher_server_end)
                .context("getting nested environment launcher")?;
            self.serve_connection(directory_server_end)?;

            Ok(NestedEnvironment { controller, launcher, directory_request })
        }

        /// Starts a new component inside an environment that only has access to
        /// the services provided through this `ServicesServer`.
        ///
        /// Note that the resulting `App` and `EnvironmentControllerProxy` must be kept
        /// alive for the component to continue running. Once they are dropped, the
        /// component will be destroyed.
        pub fn launch_component_in_nested_environment<O>(
            &mut self,
            url: String,
            arguments: Option<Vec<String>>,
            environment_label: &str,
        ) -> Result<(EnvironmentControllerProxy, crate::client::App), Error>
        where
            ServiceObjTy: From<Proxy<LoaderMarker, O>>,
            ServiceObjTy: ServiceObjTrait<Output = O>,
        {
            let NestedEnvironment { controller, launcher, directory_request: _ } =
                self.create_nested_environment(environment_label)?;

            let app = crate::client::launch(&launcher, url, arguments)?;
            Ok((controller, app))
        }
    }

    /// `NestedEnvironment` represents an environment nested within another.
    ///
    /// When `NestedEnvironment` is dropped, the environment and all components started within it
    /// will be terminated.
    #[must_use = "Dropping `NestedEnvironment` will cause the environment to be terminated."]
    pub struct NestedEnvironment {
        pub(crate) controller: EnvironmentControllerProxy,
        pub(crate) launcher: LauncherProxy,
        pub(crate) directory_request: zx::Channel,
    }

    impl NestedEnvironment {
        /// Returns a reference to the environment's controller.
        pub fn controller(&self) -> &EnvironmentControllerProxy {
            &self.controller
        }

        /// Returns a reference to the environment's launcher.
        pub fn launcher(&self) -> &LauncherProxy {
            &self.launcher
        }

        /// Connect to a service provided by this environment.
        #[inline]
        pub fn connect_to_service<S: ServiceMarker>(&self) -> Result<S::Proxy, Error> {
            let (client_channel, server_channel) = zx::Channel::create()?;
            self.pass_to_service::<S>(server_channel)?;
            Ok(S::Proxy::from_channel(fasync::Channel::from_channel(client_channel)?))
        }

        /// Connect to a service by passing a channel for the server.
        #[inline]
        pub fn pass_to_service<S: ServiceMarker>(
            &self,
            server_channel: zx::Channel,
        ) -> Result<(), Error> {
            self.pass_to_named_service(S::NAME, server_channel)
        }

        /// Connect to a service by name.
        #[inline]
        pub fn pass_to_named_service(
            &self,
            service_name: &str,
            server_channel: zx::Channel,
        ) -> Result<(), Error> {
            fdio::service_connect_at(&self.directory_request, service_name, server_channel)?;
            Ok(())
        }
    }

    enum ConnectionState {
        Open,
        Closed,
    }

    /// A client connection to `ServiceFs`.
    ///
    /// This type also implements the `Future` trait and resolves to itself
    /// when the channel becomes readable.
    struct ClientConnection {
        /// The stream of incoming requests. This is always `Some` unless
        /// this `ClientConnection` was used as a `Future` and completed, in
        /// which case it will have given away the stream to the output of
        /// the `Future`.
        stream: Option<DirectoryRequestStream>,

        /// The current node of the `ClientConnection` in the `ServiceFs`
        /// filesystem.
        position: usize,

        /// Buffer and position of current DirectoryRequest::ReadDirents
        dirents_buf: Option<(Vec<u8>, usize)>,
    }

    impl ClientConnection {
        fn stream(&mut self) -> &mut DirectoryRequestStream {
            self.stream.as_mut().expect("ClientConnection used after `Future` completed")
        }
    }

    impl Future for ClientConnection {
        type Output = Option<(Result<DirectoryRequest, fidl::Error>, ClientConnection)>;

        fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
            let res_opt = ready!(self.stream().poll_next_unpin(cx));
            Poll::Ready(res_opt.map(|res| {
                (
                    res,
                    ClientConnection {
                        stream: self.stream.take(),
                        position: self.position,
                        dirents_buf: self.dirents_buf.take(),
                    },
                )
            }))
        }
    }

    impl<ServiceObjTy: ServiceObjTrait> ServiceFs<ServiceObjTy> {
        /// Removes the `DirectoryRequest` startup handle for the current
        /// component and adds connects it to this `ServiceFs` as a client.
        ///
        /// Multiple calls to this function from the same component will
        /// result in `Err(MissingStartupHandle)`.
        pub fn take_and_serve_directory_handle(&mut self) -> Result<&mut Self, Error> {
            let startup_handle =
                fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest)
                    .ok_or(MissingStartupHandle)?;

            self.serve_connection(zx::Channel::from(startup_handle))
        }

        /// Add an additional connection to the `FdioServer` to provide services to.
        pub fn serve_connection(&mut self, chan: zx::Channel) -> Result<&mut Self, Error> {
            self.serve_connection_at(chan, ROOT_NODE, NO_FLAGS)?;
            Ok(self)
        }

        fn serve_connection_at(
            &mut self,
            chan: zx::Channel,
            position: usize,
            flags: u32,
        ) -> Result<(), Error> {
            // It is not an error if the other end of the channel is already
            // closed: the client may call Directory::Open, send a channel chan,
            // then write a request on their local end of chan. If the request
            // does not expect a response (eg. Directory::Open in a subdirectory),
            // the client may close chan immediately afterwards. We should keep
            // our end of the channel until we have processed all incoming requests.
            chan.signal_peer(Signals::NONE, Signals::USER_0)
                .or_else(|e| match e {
                    zx::Status::PEER_CLOSED => Ok(()),
                    e => Err(e),
                })
                .context("ServiceFs signal_peer failed")?;

            let chan = fasync::Channel::from_channel(chan)
                .context("failure to convert to async channel")?;

            let stream = DirectoryRequestStream::from_channel(chan);
            if (flags & OPEN_FLAG_DESCRIBE) != 0 {
                let mut info = self
                    .describe_node(position)
                    .expect("error serving connection for missing node");
                stream
                    .control_handle()
                    .send_on_open_(zx::sys::ZX_OK, Some(OutOfLine(&mut info)))
                    .context("fail sending OnOpen event")?;
            }

            self.client_connections.push(ClientConnection {
                stream: Some(stream),
                position,
                dirents_buf: None,
            });
            Ok(())
        }

        fn handle_request(
            &mut self,
            request: DirectoryRequest,
            connection: &mut ClientConnection,
        ) -> Result<(Option<ServiceObjTy::Output>, ConnectionState), Error> {
            assert!(self.nodes.len() > connection.position);

            macro_rules! unsupported {
                ($responder:ident $($args:tt)*) => {
                    $responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED $($args)*)
                }
            }

            macro_rules! maybe_send_error {
                ($object:ident, $flags:expr, $error:expr) => {
                    if ($flags & OPEN_FLAG_DESCRIBE) != 0 {
                        self.send_failed_on_open($object, $error)?;
                    }
                };
            }

            macro_rules! handle_potentially_unsupported_flags {
                ($object:ident, $flags:expr, $supported_flags_bitmask:expr) => {
                    if has_unsupported_flags($flags, $supported_flags_bitmask) {
                        maybe_send_error!($object, $flags, zx::sys::ZX_ERR_NOT_SUPPORTED);
                        bail!("flags contains unsupported flags: {}", $flags);
                    }
                };
            }

            match request {
                DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                    handle_potentially_unsupported_flags!(object, flags, CLONE_REQ_SUPPORTED_FLAGS);

                    if let Err(e) =
                        self.serve_connection_at(object.into_channel(), connection.position, flags)
                    {
                        eprintln!("ServiceFs failed to clone: {:?}", e);
                    }
                }
                DirectoryRequest::Close { responder } => {
                    responder.send(zx::sys::ZX_OK)?;
                    return Ok((None, ConnectionState::Closed));
                }
                DirectoryRequest::Open { flags, mode: _, path, object, control_handle: _ } => {
                    handle_potentially_unsupported_flags!(object, flags, OPEN_REQ_SUPPORTED_FLAGS);

                    if path == "." {
                        if let Err(e) = self.serve_connection_at(
                            object.into_channel(),
                            connection.position,
                            flags,
                        ) {
                            eprintln!("ServiceFS failed to open '.': {:?}", e);
                        }
                        return Ok((None, ConnectionState::Open));
                    } else if path == "" {
                        maybe_send_error!(object, flags, zx::sys::ZX_ERR_BAD_PATH);
                        return Ok((None, ConnectionState::Open));
                    }

                    let mut segments = path.rsplitn(2, "/");
                    let end_segment = segments.next().unwrap();
                    let directory_segment = segments.next();
                    let children = match self.descend(connection.position, directory_segment) {
                        Ok(children) => children,
                        Err(e) => {
                            eprintln!("invalid path {} - {}", path, e);
                            maybe_send_error!(object, flags, zx::sys::ZX_ERR_BAD_PATH);
                            return Ok((None, ConnectionState::Open));
                        }
                    };

                    if let Some(&next_node_pos) = children.get(end_segment) {
                        match self.nodes.get_mut(next_node_pos).expect("Missing child node") {
                            ServiceFsNode::Directory { .. } => {
                                if let Err(e) = self.serve_connection_at(
                                    object.into_channel(),
                                    next_node_pos,
                                    flags,
                                ) {
                                    eprintln!("ServiceFs failed to open directory: {:?}", e);
                                }
                            }
                            ServiceFsNode::Service(service) => {
                                if flags & OPEN_FLAG_DIRECTORY != 0 {
                                    self.send_failed_on_open(object, zx::sys::ZX_ERR_NOT_DIR)?;
                                    return Ok((None, ConnectionState::Open));
                                }
                                // Case 1: client opens node to get more metadata on it (by calling
                                //         `Describe` or `GetAttr` later), so there's no need to
                                //         connect to service. This is done by `find` command.
                                if flags & OPEN_FLAG_NODE_REFERENCE != 0
                                    || flags & OPEN_FLAG_DESCRIBE != 0
                                {
                                    if let Err(e) = self.serve_connection_at(
                                        object.into_channel(),
                                        next_node_pos,
                                        flags,
                                    ) {
                                        eprintln!("ServiceFs failed to open service node: {:?}", e);
                                    }
                                    return Ok((None, ConnectionState::Open));
                                }
                                // Case 2: client opens node to connect to service
                                return Ok((
                                    service.service().connect(object.into_channel()),
                                    ConnectionState::Open,
                                ));
                            }
                        }
                    } else {
                        maybe_send_error!(object, flags, zx::sys::ZX_ERR_NOT_FOUND);
                        return Ok((None, ConnectionState::Open));
                    }
                }
                DirectoryRequest::Describe { responder } => {
                    let mut info = self
                        .describe_node(connection.position)
                        .expect("node missing for Describe req");
                    responder.send(&mut info)?;
                }
                DirectoryRequest::GetAttr { responder } => {
                    let node =
                        self.nodes.get(connection.position).expect("node missing for GetAttr req");
                    let mode_type = match node {
                        ServiceFsNode::Directory { .. } => fidl_fuchsia_io::MODE_TYPE_DIRECTORY,
                        ServiceFsNode::Service(..) => fidl_fuchsia_io::MODE_TYPE_SERVICE,
                    };

                    let mut attrs = NodeAttributes {
                        mode: mode_type | 0o400, /* mode R_USR */
                        id: fidl_fuchsia_io::INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    };
                    responder.send(zx::sys::ZX_OK, &mut attrs)?
                }
                DirectoryRequest::SetAttr { responder, .. } => unsupported!(responder)?,
                DirectoryRequest::Ioctl { responder, .. } => {
                    unsupported!(responder, &mut std::iter::empty(), &mut std::iter::empty())?
                }
                DirectoryRequest::Sync { responder } => unsupported!(responder)?,
                DirectoryRequest::Unlink { responder, .. } => unsupported!(responder)?,
                DirectoryRequest::ReadDirents { max_bytes, responder } => {
                    let children = self.open_dir(connection.position)?;

                    let dirents_buf = connection
                        .dirents_buf
                        .get_or_insert_with(|| (self.to_dirent_bytes(&children), 0));
                    let (dirents_buf, offset) = (&mut dirents_buf.0, &mut dirents_buf.1);
                    if *offset >= dirents_buf.len() {
                        responder.send(zx::sys::ZX_OK, &mut std::iter::empty())?;
                    } else {
                        let new_offset =
                            std::cmp::min(dirents_buf.len(), *offset + max_bytes as usize);
                        responder.send(
                            zx::sys::ZX_OK,
                            &mut dirents_buf[*offset..new_offset].iter().cloned(),
                        )?;
                        *offset = new_offset;
                    }
                }
                DirectoryRequest::Rewind { responder } => {
                    connection.dirents_buf = None;
                    responder.send(zx::sys::ZX_OK)?;
                }
                DirectoryRequest::GetToken { responder } => unsupported!(responder, None)?,
                DirectoryRequest::Rename { responder, .. } => unsupported!(responder)?,
                DirectoryRequest::Link { responder, .. } => unsupported!(responder)?,
                DirectoryRequest::Watch { responder, .. } => unsupported!(responder)?,
            }
            Ok((None, ConnectionState::Open))
        }

        fn send_failed_on_open(
            &self,
            object: fidl::endpoints::ServerEnd<NodeMarker>,
            status: zx::sys::zx_status_t,
        ) -> Result<(), Error> {
            let (_stream, control_handle) = object
                .into_stream_and_control_handle()
                .context("fail to convert to stream and control handle")?;
            control_handle.send_on_open_(status, None).context("fail sending OnOpenEvent")?;
            Ok(())
        }

        fn describe_node(&self, pos: usize) -> Option<NodeInfo> {
            self.nodes.get(pos).map(|node| match node {
                ServiceFsNode::Directory { .. } => NodeInfo::Directory(DirectoryObject),
                ServiceFsNode::Service(..) => NodeInfo::Service(fidl_fuchsia_io::Service),
            })
        }

        /// Retrieve directory listing at |path| starting from node |start_pos|. If |path| is None,
        /// simply return directory listing of node |start_pos|.
        fn descend(
            &self,
            start_pos: usize,
            path: Option<&str>,
        ) -> Result<&HashMap<String, usize>, Error> {
            let mut pos = start_pos;
            let mut children = self.open_dir(pos)?;

            if let Some(path) = path {
                for segment in path.split("/") {
                    match children.get(segment) {
                        Some(next_pos) => pos = *next_pos,
                        _ => bail!("segment not found: {}", segment),
                    }
                    children =
                        self.open_dir(pos).context(format!("cannot open segment {}", segment))?;
                }
            }
            Ok(children)
        }

        /// Retrieve directory listing of node |pos|. Return an error if |pos| is not a directory
        /// node
        fn open_dir(&self, pos: usize) -> Result<&HashMap<String, usize>, Error> {
            let node = self.nodes.get(pos).expect(&format!("missing node {}", pos));
            match node {
                ServiceFsNode::Directory { children } => Ok(children),
                _ => bail!("node not a directory: {}", pos),
            }
        }

        fn to_dirent_bytes(&self, nodes: &HashMap<String, usize>) -> Vec<u8> {
            let mut buf = vec![];
            for (name, node) in nodes.iter() {
                let typ = self.nodes.get(*node).expect("missing child").to_dirent_type();
                if let Err(e) = write_dirent_bytes(&mut buf, *node as u64, typ, name) {
                    eprintln!("failed encoding dirent for node {}: {}", *node, e);
                }
            }
            buf
        }
    }

    fn write_dirent_bytes(buf: &mut Vec<u8>, ino: u64, typ: u8, name: &str) -> Result<(), Error> {
        // Safe to unwrap since `Write::write` on a `Vec` should never fail.
        buf.write_u64::<LittleEndian>(ino).unwrap();
        buf.write_u8(name.len() as u8).unwrap();
        buf.write_u8(typ as u8).unwrap();
        buf.write(name.as_ref()).unwrap();
        Ok(())
    }

    fn has_unsupported_flags(flags: u32, supported_flags_bitmask: u32) -> bool {
        (flags & !supported_flags_bitmask) != 0
    }

    impl<ServiceObjTy: ServiceObjTrait> Unpin for ServiceFs<ServiceObjTy> {}

    impl<ServiceObjTy: ServiceObjTrait> Stream for ServiceFs<ServiceObjTy> {
        type Item = ServiceObjTy::Output;

        fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            loop {
                let (request, mut client_connection) =
                    match ready!(self.client_connections.poll_next_unpin(cx)) {
                        // a client request
                        Some(Some(x)) => x,
                        // this client_connection has terminated
                        Some(None) => continue,
                        // all client connections have terminated
                        None => return Poll::Ready(None),
                    };
                let request = match request {
                    Ok(request) => request,
                    Err(e) => {
                        eprintln!("ServiceFs failed to parse an incoming request: {:?}", e);
                        continue;
                    }
                };
                match self.handle_request(request, &mut client_connection) {
                    Ok((value, connection_state)) => {
                        if let ConnectionState::Open = connection_state {
                            // Requeue the client to receive new requests
                            self.client_connections.push(client_connection);
                        }
                        if let Some(value) = value {
                            return Poll::Ready(Some(value));
                        }
                    }
                    Err(e) => eprintln!("ServiceFs failed to handle an incoming request: {:?}", e),
                }
            }
        }
    }
}
