// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::ssh::build_ssh_command,
    crate::target::Target,
    crate::RETRY_DELAY,
    anyhow::{anyhow, Context, Result},
    async_io::Async,
    fuchsia_async::{unblock, Task, Timer},
    futures::io::{copy_buf, BufReader},
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

const BUFFER_SIZE: usize = 65536;

struct HostPipeChild {
    inner: Child,
    task: Option<Task<()>>,
}

impl HostPipeChild {
    pub async fn new(addr: SocketAddr, id: u64) -> Result<HostPipeChild> {
        let mut ssh =
            build_ssh_command(addr, vec!["remote_control_runner", format!("{}", id).as_str()])
                .await?
                .stdout(Stdio::piped())
                .stdin(Stdio::piped())
                .stderr(Stdio::piped())
                .spawn()
                .context("running target overnet pipe")?;

        let (pipe_rx, mut pipe_tx) = futures::AsyncReadExt::split(
            overnet_pipe(hoist::hoist()).context("creating local overnet pipe")?,
        );

        let stdout =
            Async::new(ssh.stdout.take().ok_or(anyhow!("unable to get stdout from target pipe"))?)?;

        let mut stdin =
            Async::new(ssh.stdin.take().ok_or(anyhow!("unable to get stdin from target pipe"))?)?;

        let stderr =
            Async::new(ssh.stderr.take().ok_or(anyhow!("unable to stderr from target pipe"))?)?;

        let copy_in = async move {
            copy_buf(BufReader::with_capacity(BUFFER_SIZE, stdout), &mut pipe_tx).await
        };
        let copy_out = async move {
            copy_buf(BufReader::with_capacity(BUFFER_SIZE, pipe_rx), &mut stdin).await
        };

        let log_stderr = async move {
            let mut stderr_lines = futures_lite::io::BufReader::new(stderr).lines();
            while let Some(result) = stderr_lines.next().await {
                match result {
                    Ok(line) => log::info!("SSH stderr: {}", line),
                    Err(e) => log::info!("SSH stderr read failure: {:?}", e),
                }
            }
        };

        Ok(HostPipeChild {
            inner: ssh,
            task: Some(Task::local(async {
                drop(futures::join!(copy_in, copy_out, log_stderr));
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

        drop(self.task.take());
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
            let target = target.upgrade().ok_or(anyhow!("Target has gone"))?;
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
            Self { inner: child, task: Some(Task::local(async {})) }
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
