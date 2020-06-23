// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ssh::build_ssh_command,
    crate::target::Target,
    anyhow::anyhow,
    anyhow::{Context, Error},
    async_std::io::prelude::BufReadExt,
    async_std::prelude::StreamExt,
    ffx_core::constants,
    ffx_core::constants::SOCKET,
    fidl_fuchsia_overnet::MeshControllerProxyInterface,
    futures::channel::oneshot,
    futures::future::FutureExt,
    std::io,
    std::os::unix::io::{FromRawFd, IntoRawFd},
    std::process::{Child, Stdio},
    std::sync::Arc,
    std::time::Duration,
};

fn async_from_sync<S: IntoRawFd>(sync: S) -> async_std::fs::File {
    unsafe { async_std::fs::File::from_raw_fd(sync.into_raw_fd()) }
}

type ThreadHandle = std::thread::JoinHandle<Result<(), Error>>;
type TaskHandle = async_std::task::JoinHandle<Result<(), Error>>;

pub struct HostPipeConnection {
    // Uses a std::thread handle for now as hoist::spawn() only returns a
    // `Future<Output = ()>`, making checking the return value after running
    // `stop()` difficult.
    handle: Option<ThreadHandle>,
    should_close_tx: Option<oneshot::Sender<()>>,
    closed: bool,
}

struct HostPipeChild {
    inner: Child,
    // These are wrapped in Option so drop(&mut self) can consume them
    cancel_tx: Option<oneshot::Sender<()>>,
    writer_handle: Option<TaskHandle>,
    reader_handle: Option<TaskHandle>,
    logger_handle: Option<TaskHandle>,
}

impl HostPipeChild {
    pub async fn new(target: Arc<Target>) -> Result<HostPipeChild, Error> {
        log::info!("Starting child connection to target: {}", target.nodename);
        let mut inner = build_ssh_command(target.addrs().await, vec!["remote_control_runner"])
            .await?
            .stdout(Stdio::piped())
            .stdin(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .context("running target overnet pipe")?;
        let (mut pipe_rx, mut pipe_tx) =
            futures::AsyncReadExt::split(overnet_pipe().context("creating local overnet pipe")?);
        let (cancel_tx, cancel_rx) = oneshot::channel::<()>();

        let stdout_pipe =
            inner.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?;
        let writer_handle = async_std::task::spawn(async move {
            async_std::io::copy(&mut async_from_sync(stdout_pipe), &mut pipe_tx).await?;
            log::info!("exiting onet pipe writer task (client -> ascendd)");
            Ok(())
        });

        let stdin_pipe =
            inner.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?;
        let reader_handle = async_std::task::spawn(async move {
            let mut stdin_pipe = async_from_sync(stdin_pipe);
            let mut cancel_rx = cancel_rx.fuse();
            futures::select! {
                copy_res = async_std::io::copy(&mut pipe_rx, &mut stdin_pipe).fuse() => copy_res?,
                _ = cancel_rx => 0,
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

    pub fn try_wait(&mut self) -> io::Result<Option<std::process::ExitStatus>> {
        self.inner.try_wait()
    }
}

impl Drop for HostPipeChild {
    fn drop(&mut self) {
        // Ignores whether the receiver has been closed, this is just to
        // un-stick it from an attempt at reading from Ascendd.
        self.cancel_tx.take().map(|c| c.send(()));

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

impl HostPipeConnection {
    pub fn new(target: Arc<Target>) -> Result<Self, Error> {
        HostPipeConnection::new_with_cmd(
            target,
            HostPipeChild::new,
            constants::RETRY_DELAY,
            constants::RETRY_DELAY,
        )
    }

    fn new_with_cmd<F>(
        target: Arc<Target>,
        cmd_func: impl FnOnce(Arc<Target>) -> F + Send + Copy + 'static,
        cmd_poll_delay: Duration,
        relaunch_command_delay: Duration,
    ) -> Result<Self, Error>
    where
        F: futures::Future<Output = Result<HostPipeChild, Error>>,
    {
        let (should_close_tx, mut should_close_rx) = oneshot::channel::<()>();
        let handle = std::thread::Builder::new()
            .spawn(move || -> Result<(), Error> {
                loop {
                    log::trace!("Spawning new host-pipe instance");
                    let mut cmd = futures::executor::block_on(cmd_func(target.clone()))?;

                    loop {
                        if let Some(_) = should_close_rx
                            .try_recv()
                            .context("host-pipe close channel sender dropped")?
                        {
                            log::trace!("host-pipe connection received 'stop' message");
                            cmd.kill().context("killing hostpipe child process")?;
                            log::trace!(
                                "host-pipe command exited with code: {:?}",
                                cmd.wait().context("waiting on process to free resources")?,
                            );
                            return Ok(());
                        }

                        if let Some(status) =
                            cmd.try_wait().context("host-pipe error running try-wait")?
                        {
                            log::info!(
                                "host-pipe command exited with status {:?}. Restarting",
                                status
                            );
                            break;
                        }

                        std::thread::sleep(cmd_poll_delay);
                    }

                    // TODO(fxb/52038): Want an exponential backoff that
                    // is sync'd with an explicit "try to start this again
                    // anyway" channel using a select! between the two of them.
                    std::thread::sleep(relaunch_command_delay);
                }
            })
            .context("spawning host pipe blocking thread")?;

        Ok(Self { handle: Some(handle), should_close_tx: Some(should_close_tx), closed: false })
    }

    pub fn stop(&mut self) -> Result<(), Error> {
        if self.closed {
            return Ok(());
        }
        self.closed = true;
        // It's possible the thread has closed and the receiver will be dropped.
        // This is just here to un-stick the thread, so the result doesn't
        // matter.
        let _ = self.should_close_tx.take().expect("invariant violated").send(());
        // TODO(awdavies): Come up with a way to do this that doesn't block the
        // executor, which should be easier to do once there's a spawn function
        // that accepts an `async move` with a return value other than `()`.
        self.handle.take().expect("invariant violated").join().unwrap()?;
        Ok(())
    }
}

impl Drop for HostPipeConnection {
    fn drop(&mut self) {
        let _ = self.stop().map_err(|e| log::warn!("error dropping host-pipe-connection: {:?}", e));
    }
}

pub async fn run_ascendd() -> Result<(), Error> {
    log::info!("Starting ascendd");
    ascendd_lib::run_ascendd(
        ascendd_lib::Opt { sockpath: Some(SOCKET.to_string()), ..Default::default() },
        // TODO: this just prints serial output to stdout - ffx probably wants to take a more
        // nuanced approach here.
        Box::new(async_std::io::stdout()),
    )
    .await
    .map_err(|e| e.context("running ascendd"))
}

fn overnet_pipe() -> Result<fidl::AsyncSocket, Error> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    hoist::connect_as_mesh_controller()?
        .attach_socket_link(remote_socket, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;

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

    async fn start_child_normal_operation(target: Arc<Target>) -> Result<HostPipeChild, Error> {
        Ok(HostPipeChild::fake_new(
            std::process::Command::new("yes")
                .arg(target.nodename.clone())
                .stdout(Stdio::piped())
                .stdin(Stdio::piped())
                .spawn()
                .context(ERR_CTX)?,
        ))
    }

    async fn start_child_internal_failure(_target: Arc<Target>) -> Result<HostPipeChild, Error> {
        Err(anyhow!(ERR_CTX))
    }

    async fn start_child_cmd_fails(_target: Arc<Target>) -> Result<HostPipeChild, Error> {
        Ok(HostPipeChild::fake_new(
            std::process::Command::new("cat")
                .arg("/some/file/path/that/is/never/going/to/exist")
                .spawn()
                .context(ERR_CTX)?,
        ))
    }

    #[test]
    fn test_host_pipe_start_and_stop_normal_operation() {
        hoist::run(async move {
            let target = Arc::new(Target::new("floop"));
            let mut conn = HostPipeConnection::new_with_cmd(
                target,
                start_child_normal_operation,
                Duration::default(),
                Duration::default(),
            )
            .unwrap();
            assert!(conn.stop().is_ok());
            assert!(conn.closed);
            assert!(conn.stop().is_ok());
        });
    }

    #[test]
    fn test_host_pipe_start_and_stop_internal_failure() {
        // TODO(awdavies): Verify the error matches.
        hoist::run(async move {
            let target = Arc::new(Target::new("boop"));
            let mut conn = HostPipeConnection::new_with_cmd(
                target,
                start_child_internal_failure,
                Duration::default(),
                Duration::default(),
            )
            .unwrap();
            assert!(conn.stop().is_err());
            assert!(conn.closed);
            assert!(conn.stop().is_ok());
        });
    }

    #[test]
    fn test_host_pipe_start_and_stop_cmd_fail() {
        hoist::run(async move {
            let target = Arc::new(Target::new("blorp"));
            let mut conn = HostPipeConnection::new_with_cmd(
                target,
                start_child_cmd_fails,
                Duration::default(),
                Duration::default(),
            )
            .unwrap();
            assert!(conn.stop().is_ok());
        });
    }
}
