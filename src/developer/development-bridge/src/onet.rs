// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ssh::build_ssh_command,
    crate::target::Target,
    anyhow::anyhow,
    anyhow::{Context, Error},
    ffx_core::constants,
    ffx_core::constants::SOCKET,
    fidl_fuchsia_overnet::MeshControllerProxyInterface,
    futures::channel::oneshot,
    futures::future::FutureExt,
    futures::io::{AsyncReadExt, AsyncWriteExt},
    std::io,
    std::io::{Read, Write},
    std::process::{Child, Stdio},
    std::sync::Arc,
    std::time::Duration,
};

type ThreadHandle = std::thread::JoinHandle<Result<(), Error>>;

pub struct HostPipeConnection {
    // Uses a std::thread handle for now as hoist::spawn() only returns a
    // `Future<Output = ()>`, making checking the return value after running
    // `stop()` difficult.
    handle: ThreadHandle,
    should_close_tx: oneshot::Sender<()>,
}

struct HostPipeChild {
    inner: Child,
    cancel_tx: Option<oneshot::Sender<()>>,
    writer_handle: Option<ThreadHandle>,
    reader_handle: Option<ThreadHandle>,
}

impl HostPipeChild {
    pub async fn new(target: Arc<Target>) -> Result<HostPipeChild, Error> {
        log::info!("Starting child connection to target: {}", target.nodename);
        let mut inner = build_ssh_command(target.addrs().await, vec!["remote_control_runner"])
            .await?
            .stdout(Stdio::piped())
            .stdin(Stdio::piped())
            .spawn()
            .context("running target overnet pipe")?;
        let (pipe_rx, pipe_tx) =
            futures::AsyncReadExt::split(overnet_pipe().context("creating local overnet pipe")?);
        let (cancel_tx, cancel_rx) = oneshot::channel::<()>();
        let writer_handle = Some(spawn_copy_target_stdout_to_pipe(
            inner.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?,
            pipe_tx,
        )?);
        let reader_handle = Some(spawn_copy_pipe_to_target_stdin(
            pipe_rx,
            cancel_rx,
            inner.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?,
        )?);
        let cancel_tx = Some(cancel_tx);
        Ok(HostPipeChild { inner, cancel_tx, writer_handle, reader_handle })
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
        let _ = self
            .cancel_tx
            .take()
            .expect("invariant violated, child should always be some before drop")
            .send(());
        let reader_result = self
            .reader_handle
            .take()
            .expect("invariant violated, child should always have reader handle")
            .join()
            .unwrap();
        let writer_result = self
            .writer_handle
            .take()
            .expect("invariant violated, child should always have writer handle")
            .join()
            .unwrap();
        log::trace!(
            "Dropped HostPipeChild. Writer result: '{:?}'; reader result: '{:?}'",
            writer_result,
            reader_result
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

        Ok(Self { handle, should_close_tx })
    }

    pub fn stop(self) -> Result<(), Error> {
        // It's possible the thread has closed and the receiver will be dropped.
        // This is just here to un-stick the thread, so the result doesn't
        // matter.
        let _ = self.should_close_tx.send(());
        // TODO(awdavies): Come up with a way to do this that doesn't block the
        // executor, which should be easier to do once there's a spawn function
        // that accepts an `async move` with a return value other than `()`.
        self.handle.join().unwrap()?;
        Ok(())
    }
}

pub async fn start_ascendd() {
    log::info!("Starting ascendd");
    hoist::spawn(async move {
        ascendd_lib::run_ascendd(ascendd_lib::Opt {
            sockpath: Some(SOCKET.to_string()),
            ..Default::default()
        })
        .await
        .unwrap();
    });
}

fn overnet_pipe() -> Result<fidl::AsyncSocket, Error> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    hoist::connect_as_mesh_controller()?
        .attach_socket_link(remote_socket, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;

    Ok(local_socket)
}

fn spawn_copy_target_stdout_to_pipe(
    mut stdout_pipe: std::process::ChildStdout,
    mut pipe_tx: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<ThreadHandle, Error> {
    let handle = std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 4096];
            loop {
                let n = stdout_pipe.read(&mut buf)?;
                if n == 0 {
                    break;
                }
                futures::executor::block_on(pipe_tx.write_all(&buf[..n]))?;
            }

            log::info!("exiting onet pipe thread writer (client -> ascendd)");
            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(handle)
}

// This attr is here because `cancel_rx` (below) needs to be mut, but the
// compiler is not able to figure this out how the variable is used when placed
// in a `select!` invocation. If the `mut` is removed then the compiler will
// complain that `cancel_rx` needs to be mut, but once the `mut` is added the
// compiler will complain that `cancel_rx` does not need to be `mut`.
#[allow(unused_mut)]
fn spawn_copy_pipe_to_target_stdin(
    mut pipe_rx: futures::io::ReadHalf<fidl::AsyncSocket>,
    mut cancel_rx: oneshot::Receiver<()>,
    mut stdin_pipe: std::process::ChildStdin,
) -> Result<ThreadHandle, Error> {
    // Spawns new thread to avoid blocking executor on stdin_pipe and stdout_pipe.
    let handle = std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 4096];
            let mut cancel_rx = cancel_rx.fuse();
            loop {
                let n = match futures::executor::block_on(async {
                    futures::select! {
                        n = pipe_rx.read(&mut buf).fuse() => n.context("host-pipe reading from ascendd"),
                        res = cancel_rx => match res {
                            Ok(_) => Ok(0),
                            Err(e) => Err(e).context("host-pipe reading from shutdown channel"),
                        },
                    }
                })? {
                    0 => break,
                    n => n,
                };
                match stdin_pipe.write_all(&buf[..n]) {
                    Ok(_) => (),
                    Err(_) => break,
                };
            }
            log::info!("exiting onet pipe thread reader (ascendd -> client)");
            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(handle)
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
            let (cancel_tx, mut cancel_rx) = oneshot::channel::<()>();
            let (other_cancel_tx, mut other_cancel_rx) = oneshot::channel::<()>();
            Self {
                inner: child,
                cancel_tx: Some(cancel_tx),
                writer_handle: Some(std::thread::spawn(move || {
                    while cancel_rx.try_recv().context("host-pipe fake test writer")?.is_none() {}
                    other_cancel_tx.send(()).unwrap();
                    Result::<(), Error>::Ok(())
                })),
                reader_handle: Some(std::thread::spawn(move || {
                    while other_cancel_rx
                        .try_recv()
                        .context("host-pipe fake test reader")?
                        .is_none()
                    {}
                    Result::<(), Error>::Ok(())
                })),
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
            let conn = HostPipeConnection::new_with_cmd(
                target,
                start_child_normal_operation,
                Duration::default(),
                Duration::default(),
            )
            .unwrap();
            assert!(conn.stop().is_ok());
        });
    }

    #[test]
    fn test_host_pipe_start_and_stop_internal_failure() {
        // TODO(awdavies): Verify the error matches.
        hoist::run(async move {
            let target = Arc::new(Target::new("boop"));
            let conn = HostPipeConnection::new_with_cmd(
                target,
                start_child_internal_failure,
                Duration::default(),
                Duration::default(),
            )
            .unwrap();
            assert!(conn.stop().is_err());
        });
    }

    #[test]
    fn test_host_pipe_start_and_stop_cmd_fail() {
        hoist::run(async move {
            let target = Arc::new(Target::new("blorp"));
            let conn = HostPipeConnection::new_with_cmd(
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
