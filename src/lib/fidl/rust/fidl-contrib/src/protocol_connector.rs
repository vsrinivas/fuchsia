// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides libs for connecting to and interacting with a FIDL protocol.
//!
//! If you have a fidl protocol like this:
//!
//! ```fidl
//! type Error = strict enum : int32 {
//!     PERMANENT = 1;
//!     TRANSIENT = 2;
//! };
//!
//! @discoverable
//! protocol ProtocolFactory {
//!     CreateProtocol(resource struct {
//!         protocol server_end:Protocol;
//!     }) -> () error Error;
//! };
//!
//! protocol Protocol {
//!     DoAction() -> () error Error;
//! };
//! ```
//!
//! Then you could implement ConnectedProtocol as follows:
//!
//! ```rust
//! struct ProtocolConnectedProtocol;
//! impl ConnectedProtocol for ProtocolConnectedProtocol {
//!     type Protocol = ProtocolProxy;
//!     type ConnectError = anyhow::Error;
//!     type Message = ();
//!     type SendError = anyhow::Error;
//!
//!     fn get_protocol<'a>(
//!         &'a mut self,
//!     ) -> BoxFuture<'a, Result<Self::Protocol, Self::ConnectError>> {
//!         async move {
//!             let (protocol_proxy, server_end) =
//!                 fidl::endpoints::create_proxy().context("creating server endpoints failed")?;
//!             let protocol_factory = connect_to_protocol::<ProtocolFactoryMarker>()
//!                 .context("Failed to connect to test.protocol.ProtocolFactory")?;
//!
//!             protocol_factory
//!                 .create_protocol(server_end)
//!                 .await?
//!                 .map_err(|e| format_err!("Failed to create protocol: {:?}", e))?;
//!
//!             Ok(protocol_proxy)
//!         }
//!         .boxed()
//!     }
//!
//!     fn send_message<'a>(
//!         &'a mut self,
//!         protocol: &'a Self::Protocol,
//!         _msg: (),
//!     ) -> BoxFuture<'a, Result<(), Self::SendError>> {
//!         async move {
//!             protocol.do_action().await?.map_err(|e| format_err!("Failed to do action: {:?}", e))?;
//!             Ok(())
//!         }
//!         .boxed()
//!     }
//! }
//! ```
//!
//! Then all you would have to do to connect to the service is:
//!
//! ```rust
//! let connector = ProtocolConnector::new(ProtocolConnectedProtocol);
//! let (sender, future) = connector.serve_and_log_errors();
//! let future = Task::spawn(future);
//! // Use sender to send messages to the protocol
//! ```

use {
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::BoxFuture, Future, StreamExt},
    std::sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    tracing::error,
};

/// A trait for implementing connecting to and sending messages to a FIDL protocol.
pub trait ConnectedProtocol {
    /// The protocol that will be connected to.
    type Protocol: fidl::endpoints::Proxy;

    /// An error type returned for connection failures.
    type ConnectError: std::fmt::Display;

    /// The message type that will be forwarded to the `Protocol`.
    type Message;

    /// An error type returned for message send failures.
    type SendError: std::fmt::Display;

    /// Connects to the protocol represented by `Protocol`.
    ///
    /// If this is a two-step process as in the case of the ServiceHub pattern,
    /// both steps should be performed in this function.
    fn get_protocol<'a>(&'a mut self) -> BoxFuture<'a, Result<Self::Protocol, Self::ConnectError>>;

    /// Sends a message to the underlying `Protocol`.
    ///
    /// The protocol object should be assumed to be connected.
    fn send_message<'a>(
        &'a mut self,
        protocol: &'a Self::Protocol,
        msg: Self::Message,
    ) -> BoxFuture<'a, Result<(), Self::SendError>>;
}

/// A ProtocolSender wraps around an `mpsc::Sender` object that is used to send
/// messages to a running ProtocolConnector instance.
#[derive(Clone, Debug)]
pub struct ProtocolSender<Msg> {
    sender: mpsc::Sender<Msg>,
    is_blocked: Arc<AtomicBool>,
}

/// Returned by ProtocolSender::send to notify the caller about the state of the underlying mpsc::channel.
/// None of these status codes should be considered an error state, they are purely informational.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ProtocolSenderStatus {
    /// channel is accepting new messages.
    Healthy,

    /// channel has rejected its first message.
    BackoffStarts,

    /// channel is not accepting new messages.
    InBackoff,

    /// channel has begun accepting messages again.
    BackoffEnds,
}

impl<Msg> ProtocolSender<Msg> {
    /// Create a new ProtocolSender which will use `sender` to send messages.
    pub fn new(sender: mpsc::Sender<Msg>) -> Self {
        Self { sender, is_blocked: Arc::new(AtomicBool::new(false)) }
    }

    /// Send a message to the underlying channel.
    ///
    /// When the sender enters or exits a backoff state, it will log an error,
    /// but no other feedback will be provided to the caller.
    pub fn send(&mut self, message: Msg) -> ProtocolSenderStatus {
        if self.sender.try_send(message).is_err() {
            let was_blocked =
                self.is_blocked.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst);
            if let Ok(false) = was_blocked {
                ProtocolSenderStatus::BackoffStarts
            } else {
                ProtocolSenderStatus::InBackoff
            }
        } else {
            let was_blocked =
                self.is_blocked.compare_exchange(true, false, Ordering::SeqCst, Ordering::SeqCst);
            if let Ok(true) = was_blocked {
                ProtocolSenderStatus::BackoffEnds
            } else {
                ProtocolSenderStatus::Healthy
            }
        }
    }
}

struct ExponentialBackoff {
    initial: zx::Duration,
    current: zx::Duration,
    factor: f64,
}

impl ExponentialBackoff {
    fn new(initial: zx::Duration, factor: f64) -> Self {
        Self { initial, current: initial, factor }
    }

    fn next_timer(&mut self) -> fasync::Timer {
        let timer = fasync::Timer::new(self.current.after_now());
        self.current =
            zx::Duration::from_nanos((self.current.into_nanos() as f64 * self.factor) as i64);
        timer
    }

    fn reset(&mut self) {
        self.current = self.initial;
    }
}

/// Errors encountered while connecting to or sending messages to the ConnectedProtocol implementation.
#[derive(Debug, PartialEq, Eq)]
pub enum ProtocolConnectorError<ConnectError, ProtocolError> {
    /// Connecting to the protocol failed for some reason.
    ConnectFailed(ConnectError),

    /// Connection to the protocol was dropped. A reconnect will be triggered.
    ConnectionLost,

    /// The protocol returned an error while sending a message.
    ProtocolError(ProtocolError),
}
/// ProtocolConnector contains the logic to use a `ConnectedProtocol` to connect
/// to and forward messages to a protocol.
pub struct ProtocolConnector<CP: ConnectedProtocol> {
    /// The size of the `mpsc::channel` to use when sending event objects from the main thread to the worker thread.
    pub buffer_size: usize,
    protocol: CP,
}

impl<CP: ConnectedProtocol> ProtocolConnector<CP> {
    /// Construct a ProtocolConnector with the default `buffer_size` (10)
    pub fn new(protocol: CP) -> Self {
        Self::new_with_buffer_size(protocol, 10)
    }

    /// Construct a ProtocolConnector with a specified `buffer_size`
    pub fn new_with_buffer_size(protocol: CP, buffer_size: usize) -> Self {
        Self { buffer_size, protocol }
    }

    /// serve_and_log_errors creates both a ProtocolSender and a future that can
    /// be used to send messages to the underlying protocol. All errors from the
    /// underlying protocol will be logged.
    pub fn serve_and_log_errors(self) -> (ProtocolSender<CP::Message>, impl Future<Output = ()>) {
        let mut log_error = log_first_n_factory(30, |e| error!("{}", e));
        self.serve(move |e| match e {
            ProtocolConnectorError::ConnectFailed(e) => {
                log_error(format!("Error obtaining a connection to the protocol: {}", e))
            }
            ProtocolConnectorError::ConnectionLost => {
                log_error("Protocol disconnected, starting reconnect.".into())
            }
            ProtocolConnectorError::ProtocolError(e) => {
                log_error(format!("Protocol returned an error: {}", e))
            }
        })
    }

    /// serve creates both a ProtocolSender and a future that can be used to send
    /// messages to the underlying protocol.
    pub fn serve<ErrHandler: FnMut(ProtocolConnectorError<CP::ConnectError, CP::SendError>)>(
        self,
        h: ErrHandler,
    ) -> (ProtocolSender<CP::Message>, impl Future<Output = ()>) {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        let sender = ProtocolSender::new(sender);
        (sender, self.send_events(receiver, h))
    }

    async fn send_events<
        ErrHandler: FnMut(ProtocolConnectorError<CP::ConnectError, CP::SendError>),
    >(
        mut self,
        mut receiver: mpsc::Receiver<<CP as ConnectedProtocol>::Message>,
        mut h: ErrHandler,
    ) {
        let mut backoff = ExponentialBackoff::new(zx::Duration::from_millis(100), 2.0);
        loop {
            let protocol = match self.protocol.get_protocol().await {
                Ok(protocol) => protocol,
                Err(e) => {
                    h(ProtocolConnectorError::ConnectFailed(e));
                    backoff.next_timer().await;
                    continue;
                }
            };

            'receiving: loop {
                match receiver.next().await {
                    Some(message) => {
                        let resp = self.protocol.send_message(&protocol, message).await;
                        match resp {
                            Ok(_) => {
                                backoff.reset();
                                continue;
                            }
                            Err(e) => {
                                if fidl::endpoints::Proxy::is_closed(&protocol) {
                                    h(ProtocolConnectorError::ConnectionLost);
                                    break 'receiving;
                                } else {
                                    h(ProtocolConnectorError::ProtocolError(e));
                                }
                            }
                        }
                    }
                    None => return,
                }
            }

            backoff.next_timer().await;
        }
    }
}

fn log_first_n_factory(n: u64, mut log_fn: impl FnMut(String)) -> impl FnMut(String) {
    let mut count = 0;
    move |message| {
        if count < n {
            count += 1;
            log_fn(message);
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        anyhow::{format_err, Context},
        fidl_test_protocol_connector::{
            ProtocolFactoryMarker, ProtocolFactoryRequest, ProtocolFactoryRequestStream,
            ProtocolProxy, ProtocolRequest, ProtocolRequestStream,
        },
        fuchsia_async as fasync,
        fuchsia_component::server as fserver,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref,
            Route,
        },
        futures::{channel::mpsc::Sender, FutureExt, TryStreamExt},
        std::sync::atomic::AtomicU8,
    };

    struct ProtocolConnectedProtocol(RealmInstance, Sender<()>);
    impl ConnectedProtocol for ProtocolConnectedProtocol {
        type Protocol = ProtocolProxy;
        type ConnectError = anyhow::Error;
        type Message = ();
        type SendError = anyhow::Error;

        fn get_protocol<'a>(
            &'a mut self,
        ) -> BoxFuture<'a, Result<Self::Protocol, Self::ConnectError>> {
            async move {
                let (protocol_proxy, server_end) =
                    fidl::endpoints::create_proxy().context("creating server endpoints failed")?;
                let protocol_factory = self
                    .0
                    .root
                    .connect_to_protocol_at_exposed_dir::<ProtocolFactoryMarker>()
                    .context("Connecting to test.protocol.ProtocolFactory failed")?;

                protocol_factory
                    .create_protocol(server_end)
                    .await?
                    .map_err(|e| format_err!("Failed to create protocol: {:?}", e))?;

                Ok(protocol_proxy)
            }
            .boxed()
        }

        fn send_message<'a>(
            &'a mut self,
            protocol: &'a Self::Protocol,
            _msg: (),
        ) -> BoxFuture<'a, Result<(), Self::SendError>> {
            async move {
                protocol
                    .do_action()
                    .await?
                    .map_err(|e| format_err!("Failed to do action: {:?}", e))?;
                self.1.try_send(())?;
                Ok(())
            }
            .boxed()
        }
    }

    async fn protocol_mock(
        stream: ProtocolRequestStream,
        calls_made: Arc<AtomicU8>,
        close_after: Option<Arc<AtomicU8>>,
    ) -> Result<(), anyhow::Error> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async {
                let calls_made = calls_made.clone();
                let close_after = close_after.clone();
                match request {
                    ProtocolRequest::DoAction { responder } => {
                        calls_made.fetch_add(1, Ordering::SeqCst);
                        responder.send(&mut Ok(()))?;
                    }
                }

                if let Some(ca) = &close_after {
                    if ca.fetch_sub(1, Ordering::SeqCst) == 1 {
                        return Err(format_err!("close_after triggered"));
                    }
                }
                Ok(())
            })
            .await
    }

    async fn protocol_factory_mock(
        handles: LocalComponentHandles,
        calls_made: Arc<AtomicU8>,
        close_after: Option<u8>,
    ) -> Result<(), anyhow::Error> {
        let mut fs = fserver::ServiceFs::new();
        let mut tasks = vec![];

        fs.dir("svc").add_fidl_service(move |mut stream: ProtocolFactoryRequestStream| {
            let calls_made = calls_made.clone();
            tasks.push(fasync::Task::local(async move {
                while let Some(ProtocolFactoryRequest::CreateProtocol { protocol, responder }) =
                    stream.try_next().await.expect("ProtocolFactoryRequestStream yielded an Err(_)")
                {
                    let close_after = close_after.map(|ca| Arc::new(AtomicU8::new(ca)));
                    responder.send(&mut Ok(())).expect("Replying to CreateProtocol caller failed");
                    let _ = protocol_mock(
                        protocol.into_stream().expect("Converting ServerEnd to stream failed"),
                        calls_made.clone(),
                        close_after,
                    )
                    .await;
                }
            }));
        });

        fs.serve_connection(handles.outgoing_dir)?;
        fs.collect::<()>().await;

        Ok(())
    }

    async fn setup_realm(
        calls_made: Arc<AtomicU8>,
        close_after: Option<u8>,
    ) -> Result<RealmInstance, anyhow::Error> {
        let builder = RealmBuilder::new().await?;

        let protocol_factory_server = builder
            .add_local_child(
                "protocol_factory",
                move |handles: LocalComponentHandles| {
                    Box::pin(protocol_factory_mock(handles, calls_made.clone(), close_after))
                },
                ChildOptions::new(),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "test.protocol.connector.ProtocolFactory",
                    ))
                    .from(&protocol_factory_server)
                    .to(Ref::parent()),
            )
            .await?;

        Ok(builder.build().await?)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_protocol_connector() -> Result<(), anyhow::Error> {
        diagnostics_log::init!(&["test_protocol_connector"]);

        let calls_made = Arc::new(AtomicU8::new(0));
        let realm = setup_realm(calls_made.clone(), None).await?;
        let (log_received_sender, mut log_received_receiver) = mpsc::channel(1);
        let connector = ProtocolConnectedProtocol(realm, log_received_sender);

        let error_count = Arc::new(AtomicU8::new(0));
        let svc = ProtocolConnector::new(connector);
        let (mut sender, fut) = svc.serve({
            let count = error_count.clone();
            move |e| {
                error!("Encountered unexpected error: {:?}", e);
                count.fetch_add(1, Ordering::SeqCst);
            }
        });

        let _server = fasync::Task::local(fut);

        for _ in 0..10 {
            assert_eq!(sender.send(()), ProtocolSenderStatus::Healthy);
            log_received_receiver.next().await;
        }

        assert_eq!(calls_made.fetch_add(0, Ordering::SeqCst), 10);
        assert_eq!(error_count.fetch_add(0, Ordering::SeqCst), 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_protocol_reconnect() -> Result<(), anyhow::Error> {
        diagnostics_log::init!(&["test_protocol_reconnect"]);

        let calls_made = Arc::new(AtomicU8::new(0));

        // Simulate the protocol closing after each successful call.
        let realm = setup_realm(calls_made.clone(), Some(1)).await?;
        let (log_received_sender, mut log_received_receiver) = mpsc::channel(1);
        let connector = ProtocolConnectedProtocol(realm, log_received_sender);

        let svc = ProtocolConnector::new(connector);
        let (mut err_send, mut err_rcv) = mpsc::channel(1);
        let (mut sender, fut) = svc.serve(move |e| {
            err_send.try_send(e).expect("Could not log error");
        });

        let _server = fasync::Task::local(fut);

        for _ in 0..10 {
            // This first send will successfully call the underlying protocol.
            assert_eq!(sender.send(()), ProtocolSenderStatus::Healthy);
            log_received_receiver.next().await;

            // The second send will not, because the protocol has shut down.
            assert_eq!(sender.send(()), ProtocolSenderStatus::Healthy);
            match err_rcv.next().await.expect("Expected err") {
                ProtocolConnectorError::ConnectionLost => {}
                _ => {
                    assert!(false, "saw unexpected error type");
                }
            }
        }

        assert_eq!(calls_made.fetch_add(0, Ordering::SeqCst), 10);

        Ok(())
    }
}
