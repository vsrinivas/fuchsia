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
    futures::io::{AsyncReadExt, AsyncWriteExt},
    std::io::{Read, Write},
    std::process::{Child, Stdio},
    std::sync::Arc,
    std::time::Duration,
};

pub struct HostPipeConnection {
    // Uses a std::thread handle for now as hoist::spawn() only returns a
    // `Future<Output = ()>`, making checking the return value after running
    // `stop()` difficult.
    handle: std::thread::JoinHandle<Result<(), Error>>,
    should_close_tx: oneshot::Sender<()>,
}

impl HostPipeConnection {
    pub async fn new(target: Arc<Target>) -> Result<Self, Error> {
        HostPipeConnection::new_with_cmd(
            target,
            HostPipeConnection::start_child,
            constants::RETRY_DELAY,
            constants::RETRY_DELAY,
        )
        .await
    }

    async fn new_with_cmd<F>(
        target: Arc<Target>,
        cmd_func: impl FnOnce(Arc<Target>) -> F + Send + Copy + 'static,
        cmd_poll_delay: Duration,
        relaunch_command_delay: Duration,
    ) -> Result<Self, Error>
    where
        F: futures::Future<Output = Result<Child, Error>>,
    {
        let (should_close_tx, mut should_close_rx) = oneshot::channel::<()>();
        let handle = std::thread::Builder::new()
            .spawn(move || -> Result<(), Error> {
                loop {
                    log::info!("Spawning new host-pipe instance");
                    let mut cmd = futures::executor::block_on(cmd_func(target.clone()))?;

                    loop {
                        if let Some(_) = should_close_rx
                            .try_recv()
                            .context("host-pipe close channel sender dropped")?
                        {
                            log::info!("host-pipe connection received 'stop' message");
                            cmd.kill().context("killing hostpipe child process")?;
                            log::info!(
                                "host-pipe command exited with code: {:?}",
                                cmd.wait().context("waiting on process to free resources")?,
                            );
                            return Ok(());
                        }

                        if let Some(status) =
                            cmd.try_wait().context("host-pipe error running try-wait")?
                        {
                            log::warn!(
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

    async fn start_child(target: Arc<Target>) -> Result<Child, Error> {
        log::info!("Starting child connection to target: {}", target.nodename);
        let mut process = build_ssh_command(target.addrs().await, vec!["remote_control_runner"])
            .await?
            .stdout(Stdio::piped())
            .stdin(Stdio::piped())
            .spawn()
            .context("running target overnet pipe")?;
        let (pipe_rx, pipe_tx) =
            futures::AsyncReadExt::split(overnet_pipe().context("creating local overnet pipe")?);
        futures::future::try_join(
            copy_target_stdout_to_pipe(
                process.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?,
                pipe_tx,
            ),
            copy_pipe_to_target_stdin(
                pipe_rx,
                process.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?,
            ),
        )
        .await?;

        Ok(process)
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

async fn copy_target_stdout_to_pipe(
    mut stdout_pipe: std::process::ChildStdout,
    mut pipe_tx: futures::io::WriteHalf<fidl::AsyncSocket>,
) -> Result<(), Error> {
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            loop {
                let n = stdout_pipe.read(&mut buf)?;
                if n == 0 {
                    break;
                }
                futures::executor::block_on(pipe_tx.write_all(&buf[..n]))?;
            }

            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(())
}

async fn copy_pipe_to_target_stdin(
    mut pipe_rx: futures::io::ReadHalf<fidl::AsyncSocket>,
    mut stdin_pipe: std::process::ChildStdin,
) -> Result<(), Error> {
    // Spawns new thread to avoid blocking executor on stdin_pipe and stdout_pipe.
    std::thread::Builder::new()
        .spawn(move || -> Result<(), Error> {
            let mut buf = [0u8; 1024];
            loop {
                let n = match futures::executor::block_on(pipe_rx.read(&mut buf))? {
                    0 => break,
                    n => n,
                };
                stdin_pipe.write_all(&buf[..n])?;
            }
            Ok(())
        })
        .context("spawning blocking thread")?;

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    const ERR_CTX: &'static str = "running fake host-pipe command for test";

    async fn start_child_normal_operation(target: Arc<Target>) -> Result<Child, Error> {
        std::process::Command::new("yes").arg(target.nodename.clone()).spawn().context(ERR_CTX)
    }

    async fn start_child_internal_failure(_target: Arc<Target>) -> Result<Child, Error> {
        Err(anyhow!(ERR_CTX))
    }

    async fn start_child_cmd_fails(_target: Arc<Target>) -> Result<Child, Error> {
        std::process::Command::new("cat")
            .arg("/some/file/path/that/is/never/going/to/exist")
            .spawn()
            .context(ERR_CTX)
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
            .await
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
            .await
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
            .await
            .unwrap();
            assert!(conn.stop().is_ok());
        });
    }
}
