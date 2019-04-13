// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use {
    failure::{format_err, Error, ResultExt},
    fidl::{encoding::OutOfLine, endpoints},
    fidl_fuchsia_vsock::{
        AcceptorMarker, AcceptorRequest, ConnectionMarker, ConnectionProxy, ConnectionTransport,
        ConnectorMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{
        io::{AsyncReadExt, AsyncWriteExt},
        StreamExt,
    },
};

const TEST_DATA_LEN: u64 = 60000;

fn make_socket_pair() -> Result<(fasync::Socket, zx::Socket), Error> {
    let (a, b) = zx::Socket::create(zx::SocketOpts::STREAM)?;
    let info = a.info()?;
    a.set_tx_threshold(&(info.tx_buf_max as usize))?;
    let a_stream = fasync::Socket::from_socket(a)?;
    Ok((a_stream, b))
}

fn wait_socket_empty(socket: &fasync::Socket) {
    socket
        .as_handle_ref()
        .wait(zx::Signals::SOCKET_WRITE_THRESHOLD, zx::Time::INFINITE)
        .unwrap();
}

async fn test_read_write<'a>(
    socket: &'a mut fasync::Socket, con: &'a ConnectionProxy,
) -> Result<(), Error> {
    let data = Box::new([42u8; TEST_DATA_LEN as usize]);

    await!(socket.write_all(&*data))?;
    wait_socket_empty(&socket);

    // Send two back to back vmos
    let vmo = zx::Vmo::create(TEST_DATA_LEN)?;
    let complete1 = con.send_vmo(vmo.clone(0, TEST_DATA_LEN)?, 0, TEST_DATA_LEN);
    let complete2 = con.send_vmo(vmo.clone(0, TEST_DATA_LEN)?, 0, TEST_DATA_LEN);
    await!(complete1)?;
    await!(complete2)?;

    // Now write into the socket again
    await!(socket.write_all(&*data))?;
    wait_socket_empty(&socket);

    // Expect a single value back
    let mut val = [0];
    await!(socket.read_exact(&mut val))?;
    if val[0] != 42 {
        return Err(format_err!("Expected to read '42' not '{}'", val[0]));
    }
    Ok(())
}

fn make_con() -> Result<(fasync::Socket, ConnectionProxy, ConnectionTransport), failure::Error> {
    let (data_stream, server_socket) = make_socket_pair()?;
    let (client_end, server_end) = endpoints::create_endpoints::<ConnectionMarker>()?;
    let client_end = client_end.into_proxy().unwrap();
    let con = ConnectionTransport {
        data: server_socket,
        con: server_end,
    };
    Ok((data_stream, client_end, con))
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let vsock =
        connect_to_service::<ConnectorMarker>().context("failed to connect to vsock service")?;
    // Register the listeners early to avoid any race conditions later.
    let (acceptor_client, acceptor) = endpoints::create_endpoints::<AcceptorMarker>()?;
    await!(vsock.listen(8001, acceptor_client))?;
    let (acceptor_client2, acceptor2) = endpoints::create_endpoints::<AcceptorMarker>()?;
    await!(vsock.listen(8002, acceptor_client2))?;
    let (acceptor_client3, acceptor3) = endpoints::create_endpoints::<AcceptorMarker>()?;
    await!(vsock.listen(8003, acceptor_client3))?;
    let mut acceptor = acceptor.into_stream()?;
    let mut acceptor2 = acceptor2.into_stream()?;
    let mut acceptor3 = acceptor3.into_stream()?;

    let (mut data_stream, client_end, mut con) = make_con()?;
    let _port = await!(vsock.connect(2, 8000, &mut con))?.0;
    await!(test_read_write(&mut data_stream, &client_end))?;

    client_end.shutdown()?;
    data_stream
        .as_handle_ref()
        .wait(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::INFINITE)?;

    // Wait for a connection
    let AcceptorRequest::Accept { addr: _, responder } =
        await!(acceptor.next()).ok_or(format_err!("Failed to get incoming connection"))??;
    let (mut data_stream, client_end, mut con) = make_con()?;
    responder.send(Some(OutOfLine(&mut con)))?;

    // Send data then wait for other end to shut us down.
    await!(test_read_write(&mut data_stream, &client_end))?;
    data_stream
        .as_handle_ref()
        .wait(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::INFINITE)?;

    // Get next connection
    let AcceptorRequest::Accept { addr: _, responder } =
        await!(acceptor2.next()).ok_or(format_err!("Failed to get incoming connection"))??;
    let (mut data_stream, _client_end, mut con) = make_con()?;
    responder.send(Some(OutOfLine(&mut con)))?;
    // Send data until the peer closes
    let data = Box::new([42u8; TEST_DATA_LEN as usize]);

    loop {
        let result = await!(data_stream.write_all(&*data));
        if let Err(e) = result {
            if e.kind() == std::io::ErrorKind::ConnectionAborted {
                break;
            }
        }
    }

    // Get next connection
    {
        let AcceptorRequest::Accept { addr: _, responder } =
            await!(acceptor3.next()).ok_or(format_err!("Failed to get incoming connection"))??;
        let (mut data_stream, _client_end, mut con) = make_con()?;
        responder.send(Some(OutOfLine(&mut con)))?;
        // Read some data then suddenly close the connection.
        let mut val = [0];
        await!(data_stream.read_exact(&mut val))?;
        if val[0] != 0 {
            return Err(format_err!("Expected to read '0' no '{}'", val[0]));
        }
    }

    println!("PASS");
    Ok(())
}
