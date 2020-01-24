// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapping of Overnet that is particularly useful for overnet integration tests that don't
//! depend on the surrounding environment (and can thus be run like self contained unit tests)

mod echo;

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker},
    futures::prelude::*,
    overnet_core::{log_errors, spawn, NodeId, Router, RouterOptions},
    parking_lot::Mutex,
    std::collections::VecDeque,
    std::sync::Arc,
};

pub use fidl_fuchsia_overnet::MeshControllerProxyInterface;
pub use fidl_fuchsia_overnet::ServiceConsumerProxyInterface;
pub use fidl_fuchsia_overnet::ServicePublisherProxyInterface;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Overnet <-> API bindings

#[derive(Debug)]
enum OvernetCommand {
    ListPeers(futures::channel::oneshot::Sender<Vec<Peer>>),
    RegisterService(String, ClientEnd<ServiceProviderMarker>),
    ConnectToService(NodeId, String, fidl::Channel),
    AttachSocketLink(fidl::Socket, fidl_fuchsia_overnet::SocketLinkOptions),
}

/// Overnet implementation for integration tests
pub struct Overnet {
    tx: Mutex<futures::channel::mpsc::UnboundedSender<OvernetCommand>>,
}

impl Overnet {
    /// Create a new instance
    pub fn new() -> Result<Arc<Overnet>, Error> {
        let (tx, rx) = futures::channel::mpsc::unbounded();
        spawn(log_errors(run_overnet(rx), "Main loop failed"));
        let tx = Mutex::new(tx);
        Ok(Arc::new(Overnet { tx }))
    }

    fn send(&self, cmd: OvernetCommand) {
        self.tx.lock().unbounded_send(cmd).expect("Failed to send command to Overnet runner");
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServiceConsumer role
    pub fn connect_as_service_consumer(
        self: Arc<Self>,
    ) -> Result<impl ServiceConsumerProxyInterface, Error> {
        Ok(ServiceConsumer(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServicePublisher role
    pub fn connect_as_service_publisher(
        self: Arc<Self>,
    ) -> Result<impl ServicePublisherProxyInterface, Error> {
        Ok(ServicePublisher(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the MeshController role
    pub fn connect_as_mesh_controller(
        self: Arc<Self>,
    ) -> Result<impl MeshControllerProxyInterface, Error> {
        Ok(MeshController(self.clone()))
    }
}

async fn run_overnet(
    mut rx: futures::channel::mpsc::UnboundedReceiver<OvernetCommand>,
) -> Result<(), Error> {
    let options = RouterOptions::new();
    #[cfg(not(target_os = "fuchsia"))]
    let options = options
        .set_quic_server_key_file(hoist::hard_coded_server_key()?)
        .set_quic_server_cert_file(hoist::hard_coded_server_cert()?);
    #[cfg(target_os = "fuchsia")]
    let options = options
        .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()))
        .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string()));

    let node = Router::new(options)?;

    // Run application loop
    loop {
        let cmd = rx.next().await;
        let desc = format!("{:?}", cmd);
        let r = match cmd {
            None => return Ok(()),
            Some(OvernetCommand::ListPeers(sender)) => {
                node.list_peers(Box::new(|peers| {
                    let _ = sender.send(peers);
                }))
                .await
            }
            Some(OvernetCommand::RegisterService(service_name, provider)) => {
                node.register_service(service_name, provider).await
            }
            Some(OvernetCommand::ConnectToService(node_id, service_name, channel)) => {
                node.connect_to_service(node_id, &service_name, channel).await
            }
            Some(OvernetCommand::AttachSocketLink(socket, options)) => {
                node.attach_socket_link(socket, options)
            }
        };
        if let Err(e) = r {
            log::warn!("cmd {} failed: {:?}", desc, e);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyInterface implementations

struct MeshController(Arc<Overnet>);

impl MeshControllerProxyInterface for MeshController {
    fn attach_socket_link(
        &self,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet::SocketLinkOptions,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::AttachSocketLink(socket, options));
        Ok(())
    }
}

struct ServicePublisher(Arc<Overnet>);

impl ServicePublisherProxyInterface for ServicePublisher {
    fn publish_service(
        &self,
        service_name: &str,
        provider: ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::RegisterService(service_name.to_string(), provider));
        Ok(())
    }
}

struct ServiceConsumer(Arc<Overnet>);

impl ServiceConsumerProxyInterface for ServiceConsumer {
    type ListPeersResponseFut = futures::future::MapErr<
        futures::channel::oneshot::Receiver<Vec<Peer>>,
        fn(futures::channel::oneshot::Canceled) -> fidl::Error,
    >;

    fn list_peers(&self) -> Self::ListPeersResponseFut {
        let (sender, receiver) = futures::channel::oneshot::channel();
        self.0.send(OvernetCommand::ListPeers(sender));
        // Returning an error from the receiver means that the sender disappeared without
        // sending a response, a condition we explicitly disallow.
        receiver.map_err(|_| unreachable!())
    }

    fn connect_to_service(
        &self,
        node: &mut fidl_fuchsia_overnet_protocol::NodeId,
        service_name: &str,
        chan: fidl::Channel,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::ConnectToService(
            node.id.into(),
            service_name.to_string(),
            chan,
        ));
        Ok(())
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Connect Overnet nodes to each other

async fn copy_with_mutator(
    mut rx: impl AsyncRead + std::marker::Unpin,
    mut tx: impl AsyncWrite + std::marker::Unpin,
    mut mutator: impl FnMut(Vec<u8>) -> Vec<u8>,
) {
    let mut buf = [0u8; 1024];
    loop {
        match rx.read(&mut buf).await {
            Ok(n) => {
                let write = mutator(Vec::from(&buf[0..n]));
                match tx.write(&write).await {
                    Ok(_) => (),
                    Err(_) => return,
                }
            }
            Err(_) => return,
        }
    }
}

/// Connect two test overnet instances, with mutating functions between the stream socket connecting
/// them - to test recovery mechanisms.
pub fn connect_with_mutator(
    a: &Arc<Overnet>,
    b: &Arc<Overnet>,
    mutator_ab: Box<dyn FnMut(Vec<u8>) -> Vec<u8>>,
    mutator_ba: Box<dyn FnMut(Vec<u8>) -> Vec<u8>>,
    opt: impl Fn() -> fidl_fuchsia_overnet::SocketLinkOptions,
) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (a1, a2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (b1, b2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (arx, atx) = fidl::AsyncSocket::from_socket(a2)?.split();
    let (brx, btx) = fidl::AsyncSocket::from_socket(b1)?.split();
    a.attach_socket_link(a1, opt())?;
    b.attach_socket_link(b2, opt())?;
    spawn(copy_with_mutator(brx, atx, mutator_ab));
    spawn(copy_with_mutator(arx, btx, mutator_ba));
    Ok(())
}

/// Connect two test overnet instances with a stream socket.
pub fn connect(a: &Arc<Overnet>, b: &Arc<Overnet>) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (sa, sb) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    a.attach_socket_link(sa, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    b.attach_socket_link(sb, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    Ok(())
}

pub fn connect_with_interspersed_log_messages(
    a: &Arc<Overnet>,
    b: &Arc<Overnet>,
) -> Result<(), Error> {
    let mut log_messages = VecDeque::from(vec![
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff100a1900 (arm64_perfmon) at level 0x90000, flags 0x1\n",
"[00001.245] 00000:00000> Trying to start cpu 1 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 2 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 3 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 4 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 5 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 6 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 7 returned: 0\n",
"[00001.245] 00000:00000> initializing target\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10092fc0 (platform_dev_init) at level 0xa0000, flags 0x1\n",
"[00001.245] 00000:00000> UART: started IRQ driven TX\n",
"[00001.245] 00000:00000> moving to last init level\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10030204 (debuglog) at level 0xb0000, flags 0x1\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10098934 (kcounters) at level 0xb0000, flags 0x1\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10104270 (ktrace) at level 0xbffff, flags 0x1\n",
"[00001.246] 00000:00000> OOM: memory availability state 1\n",
"[00001.300] 00000:00000> INIT: cpu 3, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.300] 00000:00000> ARM cpu 3: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000003 aff 0:0:0:3\n",
"[00001.300] 00000:00000> entering scheduler on cpu 3\n",
"[00001.314] 00000:00000> INIT: cpu 2, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.315] 00000:00000> ARM cpu 2: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000002 aff 0:0:0:2\n",
"[00001.315] 00000:00000> entering scheduler on cpu 2\n",
"[00001.330] 00000:00000> INIT: cpu 7, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.330] 00000:00000> ARM cpu 7: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000007 aff 0:0:0:7\n",
"[00001.330] 00000:00000> entering scheduler on cpu 7\n",
"[00001.345] 00000:00000> INIT: cpu 5, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.345] 00000:00000> ARM cpu 5: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000005 aff 0:0:0:5\n",
"[00001.345] 00000:00000> entering scheduler on cpu 5\n",
"[00001.363] 00000:00000> INIT: cpu 4, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.363] 00000:00000> ARM cpu 4: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000004 aff 0:0:0:4\n",
"[00001.363] 00000:00000> entering scheduler on cpu 4\n",
"[00001.378] 00000:00000> INIT: cpu 1, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.378] 00000:00000> ARM cpu 1: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000001 aff 0:0:0:1\n",
"[00001.378] 00000:00000> entering scheduler on cpu 1\n",
"[00001.393] 00000:00000> INIT: cpu 6, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.393] 00000:00000> ARM cpu 6: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000006 aff 0:0:0:6\n",
"[00001.393] 00000:00000> entering scheduler on cpu 6\n",
"[00001.721] 00000:00000> ktrace: buffer at 0xffff008713666000 (33554432 bytes)\n",
"[00001.721] 00000:00000> INIT: cpu 0, calling hook 0xffffffff100047f8 (kernel_shell) at level 0xc0000, flags 0x1\n",
    ]);
    let mut last_was_nl = false;
    let mutator = move |mut bytes: Vec<u8>| {
        let mutate = last_was_nl;
        if bytes.len() > 0 {
            last_was_nl = bytes[bytes.len() - 1] == 10;
        };
        if mutate {
            eprintln!("Mutate bytes: {:?}", bytes);
            let prefix = log_messages.pop_front().unwrap();
            let mut output = prefix.to_string().into_bytes();
            output.append(&mut bytes);
            log_messages.push_back(prefix);
            output
        } else {
            bytes
        }
    };
    connect_with_mutator(a, b, Box::new(|v| v), Box::new(mutator), || {
        fidl_fuchsia_overnet::SocketLinkOptions {
            bytes_per_second: Some(1000),
            ..fidl_fuchsia_overnet::SocketLinkOptions::empty()
        }
    })
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Run an asynchronous test body (portably across host & Fuchsia)

// Max test time: one day by default... but can be tuned down at need.
const MAX_TEST_TIME_MS: u32 = 24 * 60 * 60 * 1000;

#[cfg(target_os = "fuchsia")]
pub use fuchsia_runner::*;

#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia_runner::*;

use timebomb::timeout_ms;

fn run_async_test_wrapper(
    f: impl 'static + Send + FnOnce() -> Result<(), Error>,
) -> Result<(), Error> {
    hoist::logger::init()?;
    let (s, r) = std::sync::mpsc::channel();
    timeout_ms(move || s.send(f()).unwrap(), MAX_TEST_TIME_MS);
    r.recv()?
}

#[cfg(target_os = "fuchsia")]
mod fuchsia_runner {
    use anyhow::Error;
    use fuchsia_async as fasync;
    use futures::prelude::*;

    pub fn run_async_test<Fut>(f: Fut) -> Result<(), Error>
    where
        Fut: Future<Output = Result<(), Error>> + 'static + Send,
    {
        crate::run_async_test_wrapper(|| fasync::Executor::new()?.run_singlethreaded(f))
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod not_fuchsia_runner {
    use anyhow::Error;
    use futures::prelude::*;

    pub fn run_async_test<Fut>(f: Fut) -> Result<(), Error>
    where
        Fut: Future<Output = Result<(), Error>> + 'static + Send,
    {
        crate::run_async_test_wrapper(|| {
            use tokio::runtime::current_thread;
            current_thread::Runtime::new()
                .unwrap()
                .block_on(f.unit_error().boxed_local().compat())
                .unwrap()
        })
    }
}
