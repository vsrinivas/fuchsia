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
    futures::io::{copy_buf, AsyncBufRead, BufReader},
    futures_lite::io::AsyncBufReadExt,
    futures_lite::stream::StreamExt,
    hoist::OvernetInstance,
    std::fmt,
    std::future::Future,
    std::io,
    std::net::SocketAddr,
    std::process::{Child, Stdio},
    std::rc::Weak,
    std::time::Duration,
};

const BUFFER_SIZE: usize = 65536;

#[derive(Debug, Clone)]
pub struct HostAddr(String);

impl fmt::Display for HostAddr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl From<&str> for HostAddr {
    fn from(s: &str) -> Self {
        HostAddr(s.to_string())
    }
}

impl From<String> for HostAddr {
    fn from(s: String) -> Self {
        HostAddr(s)
    }
}

struct HostPipeChild {
    inner: Child,
    task: Option<Task<()>>,
}

impl HostPipeChild {
    async fn new(addr: SocketAddr, id: u64) -> Result<(Option<HostAddr>, HostPipeChild)> {
        // Before running remote_control_runner, we look up the environment
        // variable for $SSH_CONNECTION. This contains the IP address, including
        // scope_id, of the ssh client from the perspective of the ssh server.
        // This is useful because the target might need to use a specific
        // interface to talk to the host device.
        let mut ssh = build_ssh_command(
            addr,
            vec![
                "echo",
                "++ $SSH_CONNECTION ++",
                "&&",
                "remote_control_runner",
                format!("{}", id).as_str(),
            ],
        )
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

        // Read the first line. This can be either either be an empty string "",
        // which signifies the STDOUT has been closed, or the $SSH_CONNECTION
        // value.
        let mut stdout = BufReader::with_capacity(BUFFER_SIZE, stdout);

        let ssh_host_address =
            match parse_ssh_connection(&mut stdout).await.context("reading ssh connection") {
                Ok(Some(addr)) => Some(HostAddr(addr)),
                Ok(None) => None,
                Err(e) => {
                    log::error!("Failed to read ssh client address: {:?}", e);
                    None
                }
            };

        let copy_in = async move {
            if let Err(e) = copy_buf(stdout, &mut pipe_tx).await {
                log::error!("SSH stdout read failure: {:?}", e);
            }
        };
        let copy_out = async move {
            if let Err(e) =
                copy_buf(BufReader::with_capacity(BUFFER_SIZE, pipe_rx), &mut stdin).await
            {
                log::error!("SSH stdin write failure: {:?}", e);
            }
        };

        let log_stderr = async move {
            let mut stderr_lines = futures_lite::io::BufReader::new(stderr).lines();
            while let Some(result) = stderr_lines.next().await {
                match result {
                    Ok(line) => log::info!("SSH stderr: {}", line),
                    Err(e) => log::error!("SSH stderr read failure: {:?}", e),
                }
            }
        };

        Ok((
            ssh_host_address,
            HostPipeChild {
                inner: ssh,
                task: Some(Task::local(async move {
                    drop(futures::join!(copy_in, copy_out, log_stderr));
                })),
            },
        ))
    }

    fn kill(&mut self) -> io::Result<()> {
        self.inner.kill()
    }

    fn wait(&mut self) -> io::Result<std::process::ExitStatus> {
        self.inner.wait()
    }
}

#[derive(Debug, thiserror::Error)]
enum ParseSshConnectionError {
    #[error(transparent)]
    Io(#[from] std::io::Error),
    #[error("Parse error: {:?}", .0)]
    Parse(String),
}

async fn parse_ssh_connection<R: AsyncBufRead + Unpin>(
    rdr: &mut R,
) -> std::result::Result<Option<String>, ParseSshConnectionError> {
    let mut line = String::new();
    rdr.read_line(&mut line).await.map_err(ParseSshConnectionError::Io)?;

    if line.is_empty() {
        return Ok(None);
    }

    let mut parts = line.split(' ');

    // The first part should be our anchor.
    match parts.next() {
        Some("++") => {}
        Some(_) | None => {
            return Err(ParseSshConnectionError::Parse(line));
        }
    }

    // The next part should be the client address. This is left as a string since
    // std::net::IpAddr does not support string scope_ids.
    let client_address = if let Some(part) = parts.next() {
        part
    } else {
        return Err(ParseSshConnectionError::Parse(line));
    };

    // Followed by the client port.
    let _client_port = if let Some(part) = parts.next() {
        part
    } else {
        return Err(ParseSshConnectionError::Parse(line));
    };

    // Followed by the server address.
    let _server_address = if let Some(part) = parts.next() {
        part
    } else {
        return Err(ParseSshConnectionError::Parse(line));
    };

    // Followed by the server port.
    let _server_port = if let Some(part) = parts.next() {
        part
    } else {
        return Err(ParseSshConnectionError::Parse(line));
    };

    // The last part should be our anchor.
    match parts.next() {
        Some("++\n") => {}
        Some(_) | None => {
            return Err(ParseSshConnectionError::Parse(line));
        }
    }

    // Finally, there should be nothing left.
    if let Some(_) = parts.next() {
        return Err(ParseSshConnectionError::Parse(line));
    }

    Ok(Some(client_address.to_string()))
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
        F: futures::Future<Output = Result<(Option<HostAddr>, HostPipeChild)>>,
    {
        loop {
            let target = target.upgrade().ok_or(anyhow!("Target has gone"))?;
            let target_nodename = target.nodename();
            log::debug!("Spawning new host-pipe instance to target {:?}", target_nodename);

            let ssh_address = target.ssh_address().ok_or_else(|| {
                anyhow!("target {:?} does not yet have an ssh address", target_nodename)
            })?;
            let (host_addr, mut cmd) =
                cmd_func(ssh_address, target.id()).await.with_context(|| {
                    format!("creating host-pipe command to target {:?}", target_nodename)
                })?;

            *target.ssh_host_address.borrow_mut() = host_addr;

            // Attempts to run the command. If it exits successfully (disconnect
            // due to peer dropping) then will set the target to disconnected
            // state. If there was an error running the command for some reason,
            // then continue and attempt to run the command again.
            let res = unblock(move || cmd.wait()).await.map_err(|e| {
                anyhow!(
                    "host-pipe error to target {:?} running try-wait: {}",
                    target_nodename,
                    e.to_string()
                )
            });

            target.ssh_host_address.borrow_mut().take();

            match res {
                Ok(_) => {
                    return Ok(());
                }
                Err(e) => log::debug!("running cmd on {:?}: {:#?}", target_nodename, e),
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
    use addr::TargetAddr;
    use assert_matches::assert_matches;
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

    async fn start_child_normal_operation(
        _addr: SocketAddr,
        _id: u64,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        Ok((
            Some(HostAddr("127.0.0.1".to_string())),
            HostPipeChild::fake_new(
                std::process::Command::new("echo")
                    .arg("127.0.0.1 44315 192.168.1.1 22")
                    .stdout(Stdio::piped())
                    .stdin(Stdio::piped())
                    .spawn()
                    .context(ERR_CTX)?,
            ),
        ))
    }

    async fn start_child_internal_failure(
        _addr: SocketAddr,
        _id: u64,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        Err(anyhow!(ERR_CTX))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_normal_operation() {
        let target = crate::target::Target::new_with_addrs(
            Some("flooooooooberdoober"),
            [TargetAddr::new("192.168.1.1:22").unwrap()].into(),
        );
        let res = HostPipeConnection::new_with_cmd(
            Rc::downgrade(&target),
            start_child_normal_operation,
            Duration::default(),
        )
        .await;
        assert_matches!(res, Ok(_));
        // Shouldn't panic when dropped.
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_host_pipe_start_and_stop_internal_failure() {
        // TODO(awdavies): Verify the error matches.
        let target = crate::target::Target::new_with_addrs(
            Some("flooooooooberdoober"),
            [TargetAddr::new("192.168.1.1:22").unwrap()].into(),
        );
        let res = HostPipeConnection::new_with_cmd(
            Rc::downgrade(&target),
            start_child_internal_failure,
            Duration::default(),
        )
        .await;
        assert!(res.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_parse_ssh_connection_works() {
        for (line, expected) in [
            (&""[..], None),
            (&"++ 192.168.1.1 1234 10.0.0.1 22 ++\n"[..], Some("192.168.1.1".to_string())),
            (
                &"++ fe80::111:2222:3333:444 56671 10.0.0.1 22 ++\n",
                Some("fe80::111:2222:3333:444".to_string()),
            ),
            (
                &"++ fe80::111:2222:3333:444%ethxc2 56671 10.0.0.1 22 ++\n",
                Some("fe80::111:2222:3333:444%ethxc2".to_string()),
            ),
        ] {
            match parse_ssh_connection(&mut line.as_bytes()).await {
                Ok(actual) => assert_eq!(expected, actual),
                res => panic!(
                    "unexpected result for {:?}: expected {:?}, got {:?}",
                    line, expected, res
                ),
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_parse_ssh_connection_errors() {
        for line in [
            // Test for invalid anchors
            &"192.168.1.1 1234 10.0.0.1 22"[..],
            &"++192.168.1.1 1234 10.0.0.1 22++"[..],
            &"++192.168.1.1 1234 10.0.0.1 22 ++"[..],
            &"++ 192.168.1.1 1234 10.0.0.1 22++"[..],
            &"## 192.168.1.1 1234 10.0.0.1 22 ##"[..],
            // Truncation
            &"++"[..],
            &"++ 192.168.1.1"[..],
            &"++ 192.168.1.1 1234"[..],
            &"++ 192.168.1.1 1234 "[..],
            &"++ 192.168.1.1 1234 10.0.0.1"[..],
            &"++ 192.168.1.1 1234 10.0.0.1 22"[..],
            &"++ 192.168.1.1 1234 10.0.0.1 22 "[..],
            &"++ 192.168.1.1 1234 10.0.0.1 22 ++"[..],
        ] {
            match parse_ssh_connection(&mut line.as_bytes()).await {
                Err(ParseSshConnectionError::Parse(actual)) => {
                    assert_eq!(line, actual);
                }
                res => panic!("unexpected result for {:?}: {:?}", line, res),
            }
        }
    }
}
