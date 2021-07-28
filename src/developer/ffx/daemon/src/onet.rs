// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::RETRY_DELAY,
    crate::ssh::build_ssh_command,
    crate::target::Target,
    anyhow::{anyhow, Context, Result},
    async_io::Async,
    ffx_daemon_events::TargetConnectionState,
    fuchsia_async::{unblock, Task, Timer},
    futures::channel::oneshot,
    futures::future::FutureExt,
    futures::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt},
    futures_lite::io::AsyncBufReadExt,
    futures_lite::stream::StreamExt,
    hoist::OvernetInstance,
    std::future::Future,
    std::io,
    std::net::SocketAddr,
    std::process::{Child, Stdio},
    std::rc::Weak,
    std::time::Duration,
};

struct HostPipeChild {
    inner: Child,
    // These are wrapped in Option so drop(&mut self) can consume them
    cancel_tx: Option<oneshot::Sender<()>>,
    task: Option<Task<()>>,
}

async fn latency_sensitive_copy(
    reader: &mut (impl AsyncRead + Unpin),
    writer: &mut (impl AsyncWrite + Unpin),
) -> std::io::Result<()> {
    let mut buf = [0u8; 16384];
    loop {
        let n = reader.read(&mut buf).await?;
        if n == 0 {
            return Ok(());
        }
        writer.write_all(&buf[..n]).await?;
        writer.flush().await?;
    }
}

impl HostPipeChild {
    pub async fn new(addr: SocketAddr, id: u64) -> Result<HostPipeChild> {
        let mut inner =
            build_ssh_command(addr, vec!["remote_control_runner", format!("{}", id).as_str()])
                .await?
                .stdout(Stdio::piped())
                .stdin(Stdio::piped())
                .stderr(Stdio::piped())
                .spawn()
                .context("running target overnet pipe")?;

        let (mut pipe_rx, mut pipe_tx) = futures::AsyncReadExt::split(
            overnet_pipe(hoist::hoist()).context("creating local overnet pipe")?,
        );
        let (cancel_tx, cancel_rx) = oneshot::channel::<()>();

        let stdout_pipe =
            inner.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?;
        let mut stdout_pipe = Async::new(stdout_pipe)?;
        let writer = async move {
            match latency_sensitive_copy(&mut stdout_pipe, &mut pipe_tx).await {
                Ok(()) => log::info!("exiting onet pipe writer task (client -> ascendd)"),
                Err(e) => log::info!("exiting onet pipe writer (client -> ascendd) due to {:?}", e),
            }
        };

        let stdin_pipe =
            inner.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?;
        let mut stdin_pipe = Async::new(stdin_pipe)?;
        let reader = async move {
            let mut cancel_rx = cancel_rx.fuse();
            futures::select! {
                copy_res = latency_sensitive_copy(&mut pipe_rx, &mut stdin_pipe).fuse() => match copy_res {
                    Ok(()) => log::info!("exiting onet pipe reader task (ascendd -> client)"),
                    Err(e) => log::info!("exiting onet pipe reader task (ascendd -> client) due to {:?}", e),
                },
                _ = cancel_rx => (),
            };
        };

        let stderr_pipe =
            inner.stderr.take().ok_or(anyhow!("unable to stderr from target pipe"))?;
        let stderr_pipe = Async::new(stderr_pipe)?;
        let logger = async move {
            let mut stderr_lines = futures_lite::io::BufReader::new(stderr_pipe).lines();
            while let Some(result) = stderr_lines.next().await {
                match result {
                    Ok(line) => log::info!("SSH stderr: {}", line),
                    Err(e) => log::info!("SSH stderr read failure: {:?}", e),
                }
            }
        };

        Ok(HostPipeChild {
            inner,
            cancel_tx: Some(cancel_tx),
            task: Some(Task::local(async move {
                futures::join!(reader, writer, logger);
            })),
        })
    }

    pub fn kill(&mut self) -> io::Result<()> {
        self.inner.kill()
    }

    pub fn wait(&mut self) -> io::Result<std::process::ExitStatus> {
        self.inner.wait()
    }
}

impl Drop for HostPipeChild {
    fn drop(&mut self) {
        // Ignores whether the receiver has been closed, this is just to
        // un-stick it from an attempt at reading from Ascendd.
        self.cancel_tx.take().map(|c| c.send(()));

        match self.inner.try_wait() {
            Ok(Some(result)) => {
                log::info!("HostPipeChild exited with {}", result);
            }
            Ok(None) => {
                let _ =
                    self.kill().map_err(|e| log::warn!("failed to kill HostPipeChild: {:?}", e));
                let _ = self
                    .wait()
                    .map_err(|e| log::warn!("failed to clean up HostPipeChild: {:?}", e));
            }
            Err(e) => {
                log::warn!("failed to soft-wait HostPipeChild: {:?}", e);
                // defensive kill & wait, both may fail.
                let _ =
                    self.kill().map_err(|e| log::warn!("failed to kill HostPipeChild: {:?}", e));
                let _ = self
                    .wait()
                    .map_err(|e| log::warn!("failed to clean up HostPipeChild: {:?}", e));
            }
        };

        self.task.take().map(|t| t.detach());
    }
}

pub struct HostPipeConnection {}

impl HostPipeConnection {
    pub fn new(target: Weak<Target>) -> impl Future<Output = Result<()>> {
        HostPipeConnection::new_with_cmd(target, HostPipeChild::new, RETRY_DELAY)
    }

    async fn new_with_cmd<F>(
        target: Weak<Target>,
        cmd_func: impl FnOnce(SocketAddr, u64) -> F + Copy + 'static,
        relaunch_command_delay: Duration,
    ) -> Result<()>
    where
        F: futures::Future<Output = Result<HostPipeChild>>,
    {
        loop {
            log::debug!("Spawning new host-pipe instance");
            let target = target.upgrade().ok_or(anyhow!("parent Arc<> lost. exiting"))?;
            let ssh_address =
                target.ssh_address().ok_or(anyhow!("target does not yet have an ssh address"))?;
            let mut cmd = cmd_func(ssh_address, target.id()).await?;

            // Attempts to run the command. If it exits successfully (disconnect due to peer
            // dropping) then will set the target to disconnected state. If
            // there was an error running the command for some reason, then
            // continue and attempt to run the command again.
            match unblock(move || cmd.wait())
                .await
                .map_err(|e| anyhow!("host-pipe error running try-wait: {}", e.to_string()))
            {
                Ok(_) => {
                    target.update_connection_state(|s| match s {
                        TargetConnectionState::Rcs(_) => TargetConnectionState::Disconnected,
                        _ => s,
                    });
                    log::debug!("rcs disconnected, exiting");
                    return Ok(());
                }
                Err(e) => log::debug!("running cmd: {:#?}", e),
            }

            // TODO(fxbug.dev/52038): Want an exponential backoff that
            // is sync'd with an explicit "try to start this again
            // anyway" channel using a select! between the two of them.
            Timer::new(relaunch_command_delay).await;
        }
    }
}

fn overnet_pipe(overnet_instance: &dyn OvernetInstance) -> Result<fidl::AsyncSocket> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    overnet_instance.connect_as_mesh_controller()?.attach_socket_link(remote_socket)?;

    Ok(local_socket)
}

#[cfg(test)]
mod test {
    use super::*;
    use std::rc::Rc;

    const ERR_CTX: &'static str = "running fake host-pipe command for test";

    impl HostPipeChild {
        /// Implements some fake join handles that wait on a join command before
        /// closing. The reader and writer handles don't do anything other than
        /// spin until they receive a message to stop.
        pub fn fake_new(child: Child) -> Self {
            let (cancel_tx, cancel_rx) = oneshot::channel::<()>();
            Self {
                inner: child,
                cancel_tx: Some(cancel_tx),
                task: Some(Task::local(async move {
                    cancel_rx.await.expect("host-pipe fake test writer");
                })),
            }
        }
    }

    async fn start_child_normal_operation(_addr: SocketAddr, _id: u64) -> Result<HostPipeChild> {
        Ok(HostPipeChild::fake_new(
            std::process::Command::new("yes")
                .arg("test-command")
                .stdout(Stdio::piped())
                .stdin(Stdio::piped())
                .spawn()
                .context(ERR_CTX)?,
        ))
    }

    async fn start_child_internal_failure(_addr: SocketAddr, _id: u64) -> Result<HostPipeChild> {
        Err(anyhow!(ERR_CTX))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_normal_operation() {
        let target = crate::target::Target::new_named("flooooooooberdoober");
        let _conn = HostPipeConnection::new_with_cmd(
            Rc::downgrade(&target),
            start_child_normal_operation,
            Duration::default(),
        );
        // Shouldn't panic when dropped.
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_internal_failure() {
        // TODO(awdavies): Verify the error matches.
        let target = crate::target::Target::new_named("flooooooooberdoober");
        let conn = HostPipeConnection::new_with_cmd(
            Rc::downgrade(&target),
            start_child_internal_failure,
            Duration::default(),
        );
        assert!(conn.await.is_err());
    }
}
