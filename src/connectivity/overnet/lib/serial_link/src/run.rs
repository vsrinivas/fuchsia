// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fragment_io::{new_fragment_io, FragmentReader, FragmentWriter};
use crate::lossy_text::LossyText;
use anyhow::{bail, ensure, format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
use fuchsia_async::TimeoutExt;
use future::Either;
use futures::prelude::*;
use overnet_core::{
    decode_fidl, encode_fidl, new_deframer, new_framer, DeframerWriter, FrameType, FramerReader,
    LinkReceiver, LinkSender, NodeId, Router,
};
use rand::Rng;
use std::sync::{Arc, Weak};
use std::time::Duration;

#[derive(Clone, Copy, Debug)]
pub enum Role {
    Client,
    Server,
}

pub async fn run(
    role: Role,
    read: impl AsyncRead + Unpin + Send,
    write: impl AsyncWrite + Unpin + Send,
    router: Weak<Router>,
    skipped: impl AsyncWrite + Unpin + Send,
    descriptor: Option<&crate::descriptor::Descriptor>,
) -> Result<(), Error> {
    let router = WeakRouter(router);
    let mut file_handler = FileHandler { read, write, skipped };
    loop {
        file_handler
            .run(|fragment_reader, fragment_writer| async {
                if let Err(e) = main(
                    role,
                    fragment_reader,
                    fragment_writer,
                    &router,
                    descriptor.map(|d| format!("{}", d)),
                )
                .await
                {
                    log::warn!("serial handler failed: {:?}", e);
                }
            })
            .await?;
        file_handler
            .run(|fragment_reader, fragment_writer| reset(role, fragment_reader, fragment_writer))
            .await?;
        fuchsia_async::Timer::new(Duration::from_millis(100)).await;
    }
}

struct FileHandler<R, W, S> {
    read: R,
    write: W,
    skipped: S,
}

impl<R: AsyncRead + Unpin + Send, W: AsyncWrite + Unpin + Send, S: AsyncWrite + Unpin + Send>
    FileHandler<R, W, S>
{
    // Build up a fragmenter -> framer -> bytes pipeline, and use that to run some inner function.
    // This allows us to drop in-flight framing work whenever the inner function finishes, whilst retaining
    // the backing files for future iterations.
    async fn run<'a, F, Fut>(&'a mut self, f: F) -> Result<(), Error>
    where
        F: FnOnce(StreamSplitter<&'a mut S>, FragmentWriter) -> Fut,
        Fut: Send + Future<Output = ()>,
    {
        const INCOMING_BYTE_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(5);
        let (framer_writer, framer_reader) = new_framer(LossyText::new(INCOMING_BYTE_TIMEOUT), 256);
        let (deframer_writer, deframer_reader) =
            new_deframer(LossyText::new(INCOMING_BYTE_TIMEOUT));
        let (fragment_writer, fragment_reader, fragment_io_runner) =
            new_fragment_io(framer_writer, deframer_reader);
        let fragment_reader = StreamSplitter { fragment_reader, skipped_bytes: &mut self.skipped };

        let support = future::try_join3(
            write_bytes(framer_reader, &mut self.write),
            read_bytes(&mut self.read, deframer_writer),
            fragment_io_runner,
        )
        .map_ok(drop)
        .boxed();

        let fut = f(fragment_reader, fragment_writer).boxed();

        match future::select(fut, support).await {
            Either::Left((_, _)) => {
                log::trace!("main task finished");
                Ok(())
            }
            Either::Right((r, m)) => {
                if m.now_or_never().is_some() {
                    log::trace!("main task finished at the last moment");
                    Ok(())
                } else {
                    log::trace!("support task finished: {:?}", r);
                    match r {
                        Err(e) => Err(e),
                        Ok(_) => unreachable!(),
                    }
                }
            }
        }
    }
}

struct WeakRouter(Weak<Router>);

impl WeakRouter {
    fn get(&self) -> Result<Arc<Router>, Error> {
        Weak::upgrade(&self.0).ok_or_else(|| format_err!("Router gone"))
    }
}

struct StreamSplitter<OutputSink> {
    fragment_reader: FragmentReader,
    skipped_bytes: OutputSink,
}

impl<OutputSink: AsyncWrite + Unpin> StreamSplitter<OutputSink> {
    async fn read(&mut self) -> Result<(FrameType, Vec<u8>), Error> {
        loop {
            match self.fragment_reader.read().await? {
                (None, frame) => self.skipped_bytes.write_all(&frame).await?,
                (Some(frame_type), frame) => return Ok((frame_type, frame)),
            }
        }
    }
}

async fn write_bytes(
    mut framer_reader: FramerReader<LossyText>,
    mut f_write: impl AsyncWrite + Unpin,
) -> Result<(), Error> {
    loop {
        let bytes = framer_reader.read().await.context("framer_reader failed")?;
        log::trace!("SERIAL WRITE: {:?}", bytes);
        f_write.write_all(&bytes).await.context("serial.write_all failed")?;
        log::trace!("WRITE COMPLETE");
        f_write.flush().await?;
        log::trace!("FLUSHED");
    }
}

async fn read_bytes(
    mut f_read: impl AsyncRead + Unpin,
    mut deframer_writer: DeframerWriter<LossyText>,
) -> Result<(), Error> {
    let mut buf = [0u8; 1024];
    loop {
        log::trace!("SERIAL READ");
        let n = f_read.read(&mut buf).await.context("serial.read failed")?;
        log::trace!("SERIAL GOT BYTES: {:?}", &buf[..n]);
        if n == 0 {
            return Ok(());
        }
        deframer_writer.write(&buf[..n]).await?;
    }
}

const GREETING_STRING: &str = "serial_link";

async fn send_greeting(
    fragment_writer: &mut FragmentWriter,
    node_id: NodeId,
    key: u64,
) -> Result<u64, Error> {
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(GREETING_STRING.to_string()),
        node_id: Some(node_id.into()),
        connection_label: Some(format!("fuchsia serial")),
        key: Some(key),
    };
    fragment_writer.write(FrameType::OvernetHello, encode_fidl(&mut greeting)?).await?;
    Ok(key)
}

#[derive(Debug, thiserror::Error)]
enum ReadGreetingError {
    #[error("empty greeting")]
    EmptyGreeting,
    #[error("received non-greeting bytes in a frame of type {:?}: {:?}", .0, .1)]
    SkippedBytes(FrameType, Vec<u8>),
    #[error("no magic string present in greeting")]
    NoMagicString,
    #[error("got bad magic string '{0}'")]
    BadMagicString(String),
    #[error("no node id in greeting")]
    NoNodeId,
    #[error("deframing error {0}")]
    Deframing(#[from] anyhow::Error),
    #[error("no key in greeting")]
    NoKey,
    #[error("timeout")]
    Timeout,
}

fn parse_greeting(mut frame: Vec<u8>) -> Result<(NodeId, u64), ReadGreetingError> {
    if frame.is_empty() {
        return Err(ReadGreetingError::EmptyGreeting);
    }
    match decode_fidl(frame.as_mut_slice())? {
        StreamSocketGreeting { magic_string: None, .. } => Err(ReadGreetingError::NoMagicString),
        StreamSocketGreeting { magic_string: Some(ref x), .. } if x != GREETING_STRING => {
            Err(ReadGreetingError::BadMagicString(x.to_string()))
        }
        StreamSocketGreeting { node_id: None, .. } => Err(ReadGreetingError::NoNodeId),
        StreamSocketGreeting { key: None, .. } => Err(ReadGreetingError::NoKey),
        StreamSocketGreeting { node_id: Some(node_id), key: Some(key), .. } => {
            Ok((node_id.into(), key))
        }
    }
}

async fn read_greeting<OutputSink: AsyncWrite + Unpin>(
    fragment_reader: &mut StreamSplitter<OutputSink>,
) -> Result<(NodeId, u64), ReadGreetingError> {
    let (frame_type, frame) = fragment_reader.read().await?;
    if frame_type != FrameType::OvernetHello {
        Err(ReadGreetingError::SkippedBytes(frame_type, frame))
    } else {
        parse_greeting(frame)
    }
}

async fn reset<OutputSink: AsyncWrite + Unpin>(
    role: Role,
    mut fragment_reader: StreamSplitter<OutputSink>,
    mut fragment_writer: FragmentWriter,
) {
    let drain_time = match role {
        Role::Client => Duration::from_secs(3),
        Role::Server => Duration::from_secs(1),
    };
    if let Err(e) = futures::future::try_join(
        drain(&mut fragment_reader, drain_time),
        send_reset(&mut fragment_writer),
    )
    .await
    {
        log::warn!("reset failed: {:?}", e);
    }
    // Explicitly drop here to remind us that we don't want to inadvertently close
    // one side of the fragment reader/writer until both drain and send_reset are done.
    drop(fragment_reader);
    drop(fragment_writer);
}

async fn drain<OutputSink: AsyncWrite + Unpin>(
    fragment_reader: &mut StreamSplitter<OutputSink>,
    drain_time: Duration,
) -> Result<(), Error> {
    enum DrainError {
        FromRead(Error),
        Timeout,
    };
    loop {
        match fragment_reader
            .read()
            .map_err(DrainError::FromRead)
            .on_timeout(drain_time, || Err(DrainError::Timeout))
            .await
        {
            Err(DrainError::Timeout) => return Ok(()),
            Ok(_) => continue,
            Err(DrainError::FromRead(e)) => return Err(e),
        }
    }
}

async fn send_reset(fragment_writer: &mut FragmentWriter) -> Result<(), Error> {
    for _ in 0..5 {
        fragment_writer.write(FrameType::OvernetHello, vec![]).await?;
    }
    Ok(())
}

async fn link_to_framer(
    link_sender: LinkSender,
    mut fragment_writer: FragmentWriter,
) -> Result<(), Error> {
    let mut buf = [0u8; 1400];
    while let Some(n) = link_sender.next_send(&mut buf).await? {
        fragment_writer.write(FrameType::Overnet, buf[..n].to_vec()).await?;
    }
    Ok(())
}

async fn deframer_to_link<OutputSink: AsyncWrite + Unpin>(
    mut fragment_reader: StreamSplitter<OutputSink>,
    link_receiver: LinkReceiver,
) -> Result<(), Error> {
    loop {
        let (frame_type, mut frame) = fragment_reader.read().await?;
        match frame_type {
            FrameType::Overnet => {
                if let Err(e) = link_receiver.received_packet(frame.as_mut()).await {
                    log::warn!("Error reading packet: {:#?}", e);
                }
            }
            FrameType::OvernetHello => bail!("Hello packet"),
        }
    }
}

async fn main<OutputSink: AsyncWrite + Unpin>(
    role: Role,
    mut fragment_reader: StreamSplitter<OutputSink>,
    mut fragment_writer: FragmentWriter,
    router: &WeakRouter,
    descriptor: Option<String>,
) -> Result<(), Error> {
    let my_node_id = router.get()?.node_id();
    let peer_node_id = match role {
        Role::Client => {
            let key = rand::thread_rng().gen();
            send_greeting(&mut fragment_writer, my_node_id, key).await?;
            let (peer_node_id, read_key) = read_greeting(&mut fragment_reader)
                .on_timeout(Duration::from_secs(10), || Err(ReadGreetingError::Timeout))
                .await?;

            ensure!(key == read_key, "connection key mismatch");
            peer_node_id
        }
        Role::Server => {
            let (peer_node_id, read_key) = read_greeting(&mut fragment_reader).await?;
            send_greeting(&mut fragment_writer, my_node_id, read_key).await?;
            peer_node_id
        }
    };

    log::info!("Established {:?} Overnet serial connection to peer {:?}", role, peer_node_id);

    let (link_sender, link_receiver) = router
        .get()?
        .new_link(
            peer_node_id,
            Box::new(move || {
                descriptor.clone().map(|d| match role {
                    Role::Server => fidl_fuchsia_overnet_protocol::LinkConfig::SerialServer(d),
                    Role::Client => fidl_fuchsia_overnet_protocol::LinkConfig::SerialClient(d),
                })
            }),
        )
        .await?;
    futures::future::try_join(
        link_to_framer(link_sender, fragment_writer),
        deframer_to_link(fragment_reader, link_receiver),
    )
    .map_ok(drop)
    .await
}

#[cfg(test)]
mod test {

    use super::Role;
    use crate::report_skipped::ReportSkipped;
    use crate::test_util::{init, test_security_context, DodgyPipe};
    use anyhow::{format_err, Error};
    use fuchsia_async::{Task, TimeoutExt};
    use futures::prelude::*;
    use overnet_core::{NodeId, Router, RouterOptions};
    use std::sync::Arc;
    use std::time::Duration;

    async fn await_peer(router: Arc<Router>, peer: NodeId) -> Result<(), Error> {
        let lp = router.new_list_peers_context();
        while lp.list_peers().await?.into_iter().find(|p| peer == p.id.into()).is_none() {}
        Ok(())
    }

    async fn end2end(repeat: u64, failures_per_64kib: u16) -> Result<(), Error> {
        init();
        futures::stream::iter(0..repeat)
            .map(Ok)
            .try_for_each_concurrent(10, move |i| async move {
                let rtr_client = Router::new(
                    RouterOptions::new().set_node_id((100 * i + 1).into()),
                    test_security_context(),
                )?;
                let rtr_server = Router::new(
                    RouterOptions::new().set_node_id((100 * i + 2).into()),
                    test_security_context(),
                )?;
                let (c2s_rx, c2s_tx) = DodgyPipe::new(failures_per_64kib).split();
                let (s2c_rx, s2c_tx) = DodgyPipe::new(failures_per_64kib).split();
                let run_client = super::run(
                    Role::Client,
                    s2c_rx,
                    c2s_tx,
                    Arc::downgrade(&rtr_client),
                    ReportSkipped::new("client"),
                    None,
                );
                let run_server = super::run(
                    Role::Server,
                    c2s_rx,
                    s2c_tx,
                    Arc::downgrade(&rtr_server),
                    ReportSkipped::new("server"),
                    None,
                );
                let _fwd = Task::spawn(
                    futures::future::try_join(run_client, run_server)
                        .map_err(|e| panic!(e))
                        .map(drop),
                );
                futures::future::try_join(
                    await_peer(rtr_client.clone(), rtr_server.node_id()),
                    await_peer(rtr_server.clone(), rtr_client.node_id()),
                )
                .map_ok(drop)
                .on_timeout(Duration::from_secs(120), || Err(format_err!("timeout")))
                .await
            })
            .map_ok(drop)
            .await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn reliable() -> Result<(), Error> {
        end2end(1, 0).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn mostly_reliable() -> Result<(), Error> {
        #[cfg(target_os = "fuchsia")]
        const RUN_COUNT: u64 = 1;
        #[cfg(not(target_os = "fuchsia"))]
        const RUN_COUNT: u64 = 100;
        end2end(RUN_COUNT, 1).await
    }
}
