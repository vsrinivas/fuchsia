// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main process for Fuchsia builds - uses fidl rather than stdin / stdout

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_xi::{JsonRequest, JsonRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{AsHandleRef, Signals, Socket, Status, Time},
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    std::{
        io,
        sync::Arc,
        thread,
    },
    xi_core_lib::XiCore,
    xi_rpc::RpcLoop,
};

// TODO: this should be moved into fuchsia_zircon.
pub struct BlockingSocket(Arc<Socket>);

impl io::Read for BlockingSocket {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let wait_sigs = Signals::SOCKET_READABLE | Signals::SOCKET_PEER_CLOSED;
        let signals = self.0.wait_handle(wait_sigs, Time::INFINITE)?;
        if signals.contains(Signals::SOCKET_PEER_CLOSED) {
            return Ok(0);
        }
        self.0.read(buf).or_else(|status|
            if status == Status::PEER_CLOSED {
                Ok(0)
            } else {
                Err(status.into())
            }
        )
    }
}

impl io::Write for BlockingSocket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.write(buf).map_err(Into::into)
        // TODO: handle case where socket is full (wait and retry)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

fn editor_main(sock: Socket) {
    eprintln!("editor_main");
    let mut state = XiCore::new();
    let arc_sock = Arc::new(sock);
    let my_in = io::BufReader::new(BlockingSocket(arc_sock.clone()));
    let my_out = BlockingSocket(arc_sock);
    let mut rpc_looper = RpcLoop::new(my_out);

    let _ = rpc_looper.mainloop(|| my_in, &mut state);
}

#[allow(deprecated)]
fn spawn_json_server(mut stream: JsonRequestStream) {
    fasync::spawn(
        async move {
            while let Some(request) = await!(stream.try_next())? {
                let JsonRequest::ConnectSocket { sock, control_handle: _ } = request;
                eprintln!("connect_socket");
                let _ = thread::spawn(move || editor_main(sock));
            }
            Ok(())
        }
            .unwrap_or_else(|e: fidl::Error| eprintln!("error running xi Json server {:?}", e))
    )
}

fn main() {
    if let Err(e) = main_xi() {
        eprintln!("xi-core: Error: {:?}", e);
    }
}

fn main_xi() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("unable to create executor")?;

    let mut server = ServiceFs::new();
    server.dir("public").add_fidl_service(|stream| spawn_json_server(stream));
    server.take_and_serve_directory_handle()?;

    let n_threads = 2;
    executor.run(server.collect::<()>(), n_threads);
    Ok(())
}
