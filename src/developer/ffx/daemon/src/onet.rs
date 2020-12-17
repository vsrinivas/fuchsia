// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{get_socket, RETRY_DELAY},
    crate::ssh::build_ssh_command,
    crate::target::{ConnectionState, TargetAddr, WeakTarget},
    anyhow::{anyhow, Context, Result},
    ascendd_lib::Ascendd,
    async_std::io::prelude::BufReadExt,
    async_std::prelude::StreamExt,
    fuchsia_async::{Task, Timer},
    futures::channel::oneshot,
    futures::future::FutureExt,
    futures::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt},
    hoist::OvernetInstance,
    std::future::Future,
    std::io,
    std::os::unix::io::{FromRawFd, IntoRawFd},
    std::process::{Child, Stdio},
    std::sync::Arc,
    std::time::Duration,
};

fn async_from_sync<S: IntoRawFd>(sync: S) -> async_std::fs::File {
    unsafe { async_std::fs::File::from_raw_fd(sync.into_raw_fd()) }
}

type TaskHandle = async_std::task::JoinHandle<Result<()>>;

struct HostPipeChild {
    inner: Child,
    // These are wrapped in Option so drop(&mut self) can consume them
    cancel_tx: Option<oneshot::Sender<()>>,
    writer_handle: Option<TaskHandle>,
    reader_handle: Option<TaskHandle>,
    logger_handle: Option<TaskHandle>,
}

async fn latency_sensitive_copy(
    reader: &mut (impl AsyncRead + Unpin),
    writer: &mut (impl AsyncWrite + Unpin),
) -> std::io::Result<()> {
    let mut buf = [0u8; 2048];
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
    pub async fn new(ascendd: Arc<Ascendd>, addrs: Vec<TargetAddr>) -> Result<HostPipeChild> {
        let mut inner = build_ssh_command(addrs, vec!["remote_control_runner"])
            .await?
            .stdout(Stdio::piped())
            .stdin(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .context("running target overnet pipe")?;
        let (mut pipe_rx, mut pipe_tx) = futures::AsyncReadExt::split(
            overnet_pipe(&*ascendd).context("creating local overnet pipe")?,
        );
        let (cancel_tx, cancel_rx) = oneshot::channel::<()>();

        let stdout_pipe =
            inner.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?;
        let writer_handle = async_std::task::spawn(async move {
            latency_sensitive_copy(&mut async_from_sync(stdout_pipe), &mut pipe_tx).await?;
            log::info!("exiting onet pipe writer task (client -> ascendd)");
            Ok(())
        });

        let stdin_pipe =
            inner.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?;
        let reader_handle = async_std::task::spawn(async move {
            let mut stdin_pipe = async_from_sync(stdin_pipe);
            let mut cancel_rx = cancel_rx.fuse();
            futures::select! {
                copy_res = latency_sensitive_copy(&mut pipe_rx, &mut stdin_pipe).fuse() => copy_res?,
                _ = cancel_rx => (),
            };
            log::info!("exiting onet pipe reader task (ascendd -> client)");
            Ok(())
        });

        let stderr_pipe =
            inner.stderr.take().ok_or(anyhow!("unable to stderr from target pipe"))?;
        let logger_handle = async_std::task::spawn(async move {
            let stderr_pipe = async_from_sync(stderr_pipe);
            let mut stderr_lines = async_std::io::BufReader::new(stderr_pipe).lines();
            while let Some(line) = stderr_lines.next().await {
                log::info!("SSH stderr: {}", line?);
            }
            Ok(())
        });

        Ok(HostPipeChild {
            inner,
            cancel_tx: Some(cancel_tx),
            writer_handle: Some(writer_handle),
            reader_handle: Some(reader_handle),
            logger_handle: Some(logger_handle),
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
        let _ = self.kill().map_err(|e| log::warn!("failed to kill HostPipeChild: {:?}", e));
        let _ = self.wait().map_err(|e| log::warn!("failed to clean up HostPipeChild: {:?}", e));

        let reader_result = self.reader_handle.take().map(|rh| futures::executor::block_on(rh));
        let writer_result = self.writer_handle.take().map(|wh| futures::executor::block_on(wh));
        let logger_result = self.logger_handle.take().map(|lh| futures::executor::block_on(lh));

        log::trace!(
            "Dropped HostPipeChild. Writer result: '{:?}'; reader result: '{:?}'; logger result: '{:?}'",
            writer_result,
            reader_result,
            logger_result,
        );
    }
}

pub struct HostPipeConnection {}

impl HostPipeConnection {
    pub fn new(target: WeakTarget) -> impl Future<Output = Result<(), String>> + Send {
        HostPipeConnection::new_with_cmd(target, HostPipeChild::new, RETRY_DELAY)
    }

    fn new_with_cmd<F>(
        target: WeakTarget,
        cmd_func: impl FnOnce(Arc<Ascendd>, Vec<TargetAddr>) -> F + Send + Copy + 'static,
        relaunch_command_delay: Duration,
    ) -> impl Future<Output = Result<(), String>> + Send
    where
        F: futures::Future<Output = Result<HostPipeChild>> + Send,
    {
        Task::blocking(async move {
            loop {
                log::debug!("Spawning new host-pipe instance");
                let target = target.upgrade().ok_or("parent Arc<> lost. exiting".to_owned())?;
                let addrs = target.addrs().await;
                let mut cmd =
                    cmd_func(target.ascendd().clone(), addrs).await.map_err(|e| e.to_string())?;

                // Attempts to run the command. If it exits successfully (disconnect due to peer
                // dropping) then will set the target to disconnected state. If
                // there was an error running the command for some reason, then
                // continue and attempt to run the command again.
                match cmd
                    .wait()
                    .map_err(|e| format!("host-pipe error running try-wait: {}", e.to_string()))
                {
                    Ok(_) => {
                        target
                            .update_connection_state(|s| match s {
                                ConnectionState::Rcs(_) => ConnectionState::Disconnected,
                                _ => s,
                            })
                            .await;
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
        })
    }
}

pub async fn create_ascendd() -> Result<Ascendd> {
    log::info!("Starting ascendd");
    Ascendd::new(
        ascendd_lib::Opt { sockpath: Some(get_socket().await), ..Default::default() },
        // TODO: this just prints serial output to stdout - ffx probably wants to take a more
        // nuanced approach here.
        Box::new(async_std::io::stdout()),
    )
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

    const ERR_CTX: &'static str = "running fake host-pipe command for test";

    impl HostPipeChild {
        /// Implements some fake join handles that wait on a join command before
        /// closing. The reader and writer handles don't do anything other than
        /// spin until they receive a message to stop.
        pub fn fake_new(child: Child) -> Self {
            let (cancel_tx, cancel_rx) = oneshot::channel::<()>();
            let (other_cancel_tx, other_cancel_rx) = oneshot::channel::<()>();
            Self {
                inner: child,
                cancel_tx: Some(cancel_tx),
                writer_handle: Some(async_std::task::spawn(async move {
                    cancel_rx.await.context("host-pipe fake test writer")?;
                    other_cancel_tx.send(()).unwrap();
                    Ok(())
                })),
                reader_handle: Some(async_std::task::spawn(async move {
                    other_cancel_rx.await.context("host-pipe fake test writer")?;
                    Ok(())
                })),
                logger_handle: Some(async_std::task::spawn(async { Ok(()) })),
            }
        }
    }

    async fn start_child_normal_operation(
        _ascendd: Arc<Ascendd>,
        _t: Vec<TargetAddr>,
    ) -> Result<HostPipeChild> {
        Ok(HostPipeChild::fake_new(
            std::process::Command::new("yes")
                .arg("test-command")
                .stdout(Stdio::piped())
                .stdin(Stdio::piped())
                .spawn()
                .context(ERR_CTX)?,
        ))
    }

    async fn start_child_internal_failure(
        _ascendd: Arc<Ascendd>,
        _t: Vec<TargetAddr>,
    ) -> Result<HostPipeChild> {
        Err(anyhow!(ERR_CTX))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_normal_operation() {
        let ascendd = Arc::new(create_ascendd().await.unwrap());
        let target = crate::target::Target::new(ascendd, "flooooooooberdoober");
        let _conn = HostPipeConnection::new_with_cmd(
            target.downgrade(),
            start_child_normal_operation,
            Duration::default(),
        );
        // Shouldn't panic when dropped.
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_internal_failure() {
        // TODO(awdavies): Verify the error matches.
        let ascendd = Arc::new(create_ascendd().await.unwrap());
        let target = crate::target::Target::new(ascendd, "flooooooooberdoober");
        let conn = HostPipeConnection::new_with_cmd(
            target.downgrade(),
            start_child_internal_failure,
            Duration::default(),
        );
        assert!(conn.await.is_err());
    }
}
