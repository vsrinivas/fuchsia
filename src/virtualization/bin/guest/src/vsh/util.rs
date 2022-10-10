// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    fuchsia_async as fasync,
    futures::{AsyncReadExt, AsyncWriteExt},
    std::convert::TryFrom,
    std::default::Default,
};

/// Default vsock port that vshd listens on.
pub const VSH_PORT: u32 = 9001;
/// Maximum amount of data that can be sent in a single DataMessage. This is
/// picked based on the max message size with generous room for protobuf
/// overhead.
pub const MAX_DATA_SIZE: usize = 4000;
/// Maximum size allowed for a single protobuf message.
pub const MAX_MESSAGE_SIZE: usize = 4096;

/// Sends a single prost::Message into the `socket`. An error is returned if the serialized
/// `message` would be larger than `MAX_MESSAGE_SIZE` or if there are problems writing to the
/// `socket`.
pub async fn send_message(
    socket: &mut fasync::Socket,
    message: &impl prost::Message,
) -> Result<()> {
    // Try to serialize message into a buffer
    let mut msg_buffer = [0u8; MAX_MESSAGE_SIZE];
    let mut buf_mut = &mut msg_buffer[..];
    message.encode(&mut buf_mut).context("Serialized message too large")?;

    // Calculate serialized size
    let unused_bytes = buf_mut.len();
    let sz = u32::try_from(msg_buffer.len() - unused_bytes)?;

    // Messages are sent prefixed by the size as a u32 in little-endian
    socket.write_all(&sz.to_le_bytes()).await?;
    socket.write_all(&msg_buffer[..sz as usize]).await?;
    Ok(())
}

/// Tries to receive a single prost::Message of known type from the `socket`. An error is returned
/// if the next message appears to be too large, or if we fail to decode all the received bytes
/// into the expected message type.
pub async fn recv_message<P: prost::Message + Default>(socket: &mut fasync::Socket) -> Result<P> {
    // Get size of next message (encoded as a little-endian u32)
    let mut sz_buf = [0u8; 4];
    socket.read_exact(&mut sz_buf).await?;
    let sz = u32::from_le_bytes(sz_buf) as usize;

    // Set up receive buffer
    let mut msg_buffer = [0u8; MAX_MESSAGE_SIZE];
    let buf_mut =
        msg_buffer.get_mut(..sz).ok_or_else(|| anyhow!("Next message too large: {sz}"))?;

    // Get next message
    socket.read_exact(buf_mut).await?;
    P::decode(&*buf_mut).context("Failed to decode next protobuf message")
}

#[cfg(test)]
mod test {
    use super::*;

    use anyhow::{Context, Result};
    use fuchsia_zircon as zx;
    use rand::{thread_rng, Rng};
    use vsh_rust_proto::vm_tools::vsh;

    fn create_async_sockets() -> Result<(fasync::Socket, fasync::Socket)> {
        let (s1, s2) =
            zx::Socket::create(zx::SocketOpts::STREAM).context("socket creation failure")?;
        let s1 = fasync::Socket::from_socket(s1).context("failed to create async socket s1")?;
        let s2 = fasync::Socket::from_socket(s2).context("failed to create async socket s2")?;
        Ok((s1, s2))
    }

    #[derive(Debug)]
    enum TestError {
        SendErr(anyhow::Error),
        RecvErr(anyhow::Error),
    }

    async fn send_and_recv_data_msg(data: Vec<u8>) -> Result<(), TestError> {
        let (mut hvsock, mut gvsock) = create_async_sockets().expect("failed initial setup");

        let test_msg = vsh::GuestMessage {
            msg: Some(vsh::guest_message::Msg::DataMessage(vsh::DataMessage {
                stream: vsh::StdioStream::StdinStream as i32,
                data,
            })),
        };

        send_message(&mut hvsock, &test_msg)
            .await
            .context("failed to send test message")
            .map_err(TestError::SendErr)?;

        let recv_msg = recv_message::<vsh::GuestMessage>(&mut gvsock)
            .await
            .context("failed to receive test message")
            .map_err(TestError::RecvErr)?;

        assert_eq!(test_msg, recv_msg);
        Ok(())
    }

    #[fuchsia::test]
    async fn send_recv_empty_message() {
        send_and_recv_data_msg(vec![]).await.expect("send_recv_empty_message failed");
    }

    #[fuchsia::test]
    async fn send_recv_max_data_size_message() {
        send_and_recv_data_msg(vec![0; MAX_DATA_SIZE])
            .await
            .expect("send_recv_max_data_size_message failed");
    }

    #[fuchsia::test]
    async fn send_recv_oversized_message() {
        // Sending data with length MAX_MESSAGE_SIZE is guaranteed to create a message larger than
        // MAX_MESSAGE_SIZE due to the protobuf encoding overhead.
        match send_and_recv_data_msg(vec![0; MAX_MESSAGE_SIZE])
            .await
            .expect_err("send_recv_oversized_message unexpectedly succeeded")
        {
            TestError::SendErr(_) => (),
            other => panic!("send_recv_oversized_message had unexpected result: {other:?}"),
        }
    }

    fn get_random_vec() -> Vec<u8> {
        let mut rng = thread_rng();
        let size = rng.gen_range(0..=MAX_DATA_SIZE);
        let mut vec = vec![0; size];
        vec.fill_with(|| rng.gen());
        vec
    }

    #[fuchsia::test]
    async fn send_recv_random_messages() {
        for _ in 0..10 {
            let v = get_random_vec();
            send_and_recv_data_msg(v.clone())
                .await
                .map_err(|e| {
                    anyhow::anyhow!(
                        "send_recv_random_messages failed for input {v:?} because of: {e:?}"
                    )
                })
                .unwrap();
        }
    }
}
