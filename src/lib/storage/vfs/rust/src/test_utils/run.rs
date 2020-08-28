// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities to run asynchronous tests that use `pseudo-fs` objects.

use crate::{
    directory::{entry::DirectoryEntry, mutable::entry_constructor::EntryConstructor},
    execution_scope::ExecutionScope,
    path::Path,
    registry::{InodeRegistry, TokenRegistry},
};

use {
    fidl::endpoints::{create_proxy, ServiceMarker},
    fuchsia_async::Executor,
    std::{future::Future, pin::Pin, sync::Arc, task::Poll},
};

/// A helper to connect a pseudo fs server to a client and the run the client on a single threaded
/// executor. Execution is run until the executor reports the execution has stalled. The client
/// must complete it's execution at this point. It is a failure if the client is stalled.
///
/// This is the most common case for the test execution, and is actualy just forwarding to
/// [`test_server_client()`] followed immediately by a [`run()`] call.
pub fn run_server_client<Marker, GetClient, GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: GetClient,
) where
    Marker: ServiceMarker,
    GetClient: FnOnce(Marker::Proxy) -> GetClientRes,
    GetClientRes: Future<Output = ()>,
{
    test_server_client::<Marker, _, _>(flags, server, get_client).run();
}

/// Similar to [`run_server_client()`] but does not automatically connect the server and the
/// client.  The client is expected to connect to the server on it's own.  Otherwise behaviour is
/// the same as for [`run_server_client()`], except the executor is not implicit, but is specified
/// via the `exec` argument.
///
/// For example, this way the client can control when the first `open()` call will happen on the
/// server and/or perform additional `open()` calls on the server.  With [`run_server_client()`]
/// the first call to `open()` is already finished by the time client code starts running.
///
/// This is the second most common case for the test execution, and, similarly to
/// [`run_server_client`] it is actually just forwarding to [`test_client()`] followed by a
/// [`run()`] call.
pub fn run_client<GetClient, GetClientRes>(exec: Executor, get_client: GetClient)
where
    GetClient: FnOnce() -> GetClientRes,
    GetClientRes: Future<Output = ()>,
{
    test_client(get_client).exec(exec).run();
}

/// [`test_server_client`] and [`test_client`] allow for a "coordinator" closure - something
/// responsible for coordinating test execution.  In particular in certain tests it is important to
/// make sure all the operations have completed before running the next portion of the test.
///
/// This type represents a controller that the coordinator uses to achieve this effect.
/// Coordinator will use `oneshot` or `mpsc` channels to synchronize with the test execution and
/// will call [`run_until_stalled`] to separate portions of the test, optinally followed by
/// [`run_until_complete`].  In any case, [`TestController`] will ensure that the test execution
/// finishes completely, not just stalls.
pub struct TestController<'test_refs> {
    exec: Executor,
    client: Pin<Box<dyn Future<Output = ()> + 'test_refs>>,
    drop_checks: bool,
}

impl<'test_refs> TestController<'test_refs> {
    fn new(exec: Executor, client: Pin<Box<dyn Future<Output = ()> + 'test_refs>>) -> Self {
        Self { exec, client, drop_checks: true }
    }

    /// Runs the client test code until it is stalled.  Will panic if the test code runs to
    /// completion.
    pub fn run_until_stalled(&mut self) {
        // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
        // I can not do this:
        //
        //   let timeout = 300.millis();
        //   let client = self.client.on_timeout(
        //       timeout.after_now(),
        //       || panic!("Test did not finish in {}ms", timeout.millis()));

        let res = self.exec.run_until_stalled(&mut self.client);
        assert_eq!(res, Poll::Pending, "Test was not expected to complete");
    }

    /// Runs the client test code until it is stalled and then dropps the controller.  The test
    /// will not execute any more - only useful in case when stalling in the test is expected.
    /// Generally this is a bad idea, as there is no controll over where exactly the test is
    /// stalled making it easy to miss bugs.
    pub fn run_until_stalled_and_forget(mut self) {
        let res = self.exec.run_until_stalled(&mut self.client);
        assert_eq!(res, Poll::Pending, "Test was not expected to complete");

        self.drop_checks = false;
    }

    /// Runs the client test code to completion.  As this will consume the controller, this method
    /// can only be called last.  Note that the controller will effectively run this methods for
    /// you when it is dropped, if you do not do it explicitly.
    pub fn run_until_complete(self) {
        // [`Drop::drop`] will actually do the final execution, when `self` is dropped.
    }
}

impl<'test_refs> Drop for TestController<'test_refs> {
    fn drop(&mut self) {
        // See `run_until_stalled` above the a comment about timeouts.

        if self.drop_checks {
            let res = self.exec.run_until_stalled(&mut self.client);
            assert_eq!(res, Poll::Ready(()), "Test did not complete");
        }
    }
}

/// Collects a basic required set of parameters for a server/client test.  Additional parameteres
/// can be specified using [`exec`], [`mode`], and [`coordinator`] methods via a builder patter.
/// Actual execution of the test happen when [`run`] method is invoked.
pub fn test_server_client<'test_refs, Marker, GetClient, GetClientRes>(
    flags: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: GetClient,
) -> AsyncServerClientTestParams<'test_refs, Marker>
where
    Marker: ServiceMarker,
    GetClient: FnOnce(Marker::Proxy) -> GetClientRes + 'test_refs,
    GetClientRes: Future<Output = ()> + 'test_refs,
{
    AsyncServerClientTestParams {
        exec: None,
        flags,
        mode: None,
        server,
        get_client: Box::new(move |proxy| Box::pin(get_client(proxy))),
        coordinator: None,
        token_registry: None,
        inode_registry: None,
        entry_constructor: None,
    }
}

/// Collects a basic required set of parameters for a server/client test.  Similar to
/// [`test_server_client`] but also takes the `mode` argument as part of the invocation, in order
/// to keep it close the the `flag` and the other related arguments.
pub fn test_server_client_with_mode<'test_refs, Marker, GetClient, GetClientRes>(
    flags: u32,
    mode: u32,
    server: Arc<dyn DirectoryEntry>,
    get_client: GetClient,
) -> AsyncServerClientTestParams<'test_refs, Marker>
where
    Marker: ServiceMarker,
    GetClient: FnOnce(Marker::Proxy) -> GetClientRes + 'test_refs,
    GetClientRes: Future<Output = ()> + 'test_refs,
{
    AsyncServerClientTestParams {
        exec: None,
        flags,
        mode: Some(mode),
        server,
        get_client: Box::new(move |proxy| Box::pin(get_client(proxy))),
        coordinator: None,
        token_registry: None,
        inode_registry: None,
        entry_constructor: None,
    }
}

/// Collects a basic required set of parameters for a client-only test.  Additional parameteres can
/// be specified using [`exec`], and [`coordinator`] methods via a builder patter.  Actual
/// execution of the test happen when [`run`] method is invoked.
pub fn test_client<'test_refs, GetClient, GetClientRes>(
    get_client: GetClient,
) -> AsyncClientTestParams<'test_refs>
where
    GetClient: FnOnce() -> GetClientRes + 'test_refs,
    GetClientRes: Future<Output = ()> + 'test_refs,
{
    AsyncClientTestParams {
        exec: None,
        get_client: Box::new(move || Box::pin(get_client())),
        coordinator: None,
    }
}

/// A helper that holds all the parameters necessary to run an async test with a server and a
/// client.
#[must_use = "Need to call `run` to actually run the test"]
pub struct AsyncServerClientTestParams<'test_refs, Marker>
where
    Marker: ServiceMarker,
{
    exec: Option<Executor>,
    flags: u32,
    mode: Option<u32>,
    server: Arc<dyn DirectoryEntry>,
    get_client: Box<
        dyn FnOnce(Marker::Proxy) -> Pin<Box<dyn Future<Output = ()> + 'test_refs>> + 'test_refs,
    >,
    coordinator: Option<Box<dyn FnOnce(TestController) + 'test_refs>>,
    token_registry: Option<Arc<dyn TokenRegistry + Send + Sync>>,
    inode_registry: Option<Arc<dyn InodeRegistry + Send + Sync>>,
    entry_constructor: Option<Arc<dyn EntryConstructor + Send + Sync>>,
}

/// A helper that holds all the parameters necessary to run an async client-only test.
#[must_use = "Need to call `run` to actually run the test"]
pub struct AsyncClientTestParams<'test_refs> {
    exec: Option<Executor>,
    get_client: Box<dyn FnOnce() -> Pin<Box<dyn Future<Output = ()> + 'test_refs>> + 'test_refs>,
    coordinator: Option<Box<dyn FnOnce(TestController) + 'test_refs>>,
}

macro_rules! field_setter {
    ($name:ident, $type:ty) => {
        pub fn $name(mut self, $name: $type) -> Self {
            assert!(self.$name.is_none(), concat!("`", stringify!($name), "` is already set"));
            self.$name = Some($name);
            self
        }
    };
}

impl<'test_refs, Marker> AsyncServerClientTestParams<'test_refs, Marker>
where
    Marker: ServiceMarker,
{
    field_setter!(exec, Executor);
    field_setter!(mode, u32);

    pub fn coordinator(
        mut self,
        get_coordinator: impl FnOnce(TestController) + 'test_refs,
    ) -> Self {
        assert!(self.coordinator.is_none(), "`coordinator` is already set");
        self.coordinator = Some(Box::new(get_coordinator));
        self
    }

    field_setter!(token_registry, Arc<dyn TokenRegistry + Send + Sync>);
    field_setter!(inode_registry, Arc<dyn InodeRegistry + Send + Sync>);
    field_setter!(entry_constructor, Arc<dyn EntryConstructor + Send + Sync>);

    /// Runs the test based on the parameters specified in the [`test_server_client`] and other
    /// method calls.
    pub fn run(self) {
        let exec = self.exec.unwrap_or_else(|| Executor::new().expect("Executor creation failed"));

        let (client_proxy, server_end) =
            create_proxy::<Marker>().expect("Failed to create connection endpoints");

        let scope_builder = ExecutionScope::build();
        let scope_builder = match self.token_registry {
            Some(token_registry) => scope_builder.token_registry(token_registry),
            None => scope_builder,
        };
        let scope_builder = match self.inode_registry {
            Some(inode_registry) => scope_builder.inode_registry(inode_registry),
            None => scope_builder,
        };
        let scope_builder = match self.entry_constructor {
            Some(entry_constructor) => scope_builder.entry_constructor(entry_constructor),
            None => scope_builder,
        };
        self.server.open(
            scope_builder.new(),
            self.flags,
            self.mode.unwrap_or(0),
            Path::empty(),
            server_end.into_channel().into(),
        );

        let client = (self.get_client)(client_proxy);

        let coordinator = self.coordinator.unwrap_or_else(|| Box::new(|_controller| ()));

        let controller = TestController::new(exec, client);
        coordinator(controller);
    }
}

impl<'test_refs> AsyncClientTestParams<'test_refs> {
    field_setter!(exec, Executor);

    pub fn coordinator(
        mut self,
        get_coordinator: impl FnOnce(TestController) + 'test_refs,
    ) -> Self {
        assert!(self.coordinator.is_none(), "`coordinator` is already set");
        self.coordinator = Some(Box::new(get_coordinator));
        self
    }

    /// Runs the test based on the parameters specified in the [`test_server_client`] and other
    /// method calls.
    pub fn run(self) {
        let exec = self.exec.unwrap_or_else(|| Executor::new().expect("Executor creation failed"));

        let client = (self.get_client)();

        let coordinator = self.coordinator.unwrap_or_else(|| Box::new(|_controller| ()));

        let controller = TestController::new(exec, client);
        coordinator(controller);
    }
}
