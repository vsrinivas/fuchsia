// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::ssh::build_ssh_command,
    crate::target::Target,
    crate::RETRY_DELAY,
    anyhow::{anyhow, Context, Result},
    async_io::Async,
    ffx_daemon_core::events,
    ffx_daemon_events::{HostPipeErr, TargetEvent},
    fuchsia_async::{unblock, Task, Timer},
    futures::io::{copy_buf, AsyncBufRead, BufReader},
    futures_lite::io::AsyncBufReadExt,
    futures_lite::stream::StreamExt,
    hoist::OvernetInstance,
    std::cell::RefCell,
    std::collections::VecDeque,
    std::fmt,
    std::future::Future,
    std::io,
    std::net::SocketAddr,
    std::process::{Child, Stdio},
    std::rc::Rc,
    std::rc::Weak,
    std::time::Duration,
};

const BUFFER_SIZE: usize = 65536;

#[derive(Debug)]
pub struct LogBuffer {
    buf: RefCell<VecDeque<String>>,
    capacity: usize,
}

impl LogBuffer {
    pub fn new(capacity: usize) -> Self {
        Self { buf: RefCell::new(VecDeque::with_capacity(capacity)), capacity }
    }

    pub fn push_line(&self, line: String) {
        let mut buf = self.buf.borrow_mut();
        if buf.len() == self.capacity {
            buf.pop_front();
        }

        buf.push_back(line)
    }

    pub fn lines(&self) -> Vec<String> {
        let buf = self.buf.borrow_mut();
        buf.range(..).cloned().collect()
    }

    pub fn clear(&self) {
        let mut buf = self.buf.borrow_mut();
        buf.truncate(0);
    }
}

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
    async fn new(
        addr: SocketAddr,
        id: u64,
        stderr_buf: Rc<LogBuffer>,
        event_queue: events::Queue<TargetEvent>,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        Self::new_inner(addr, id, stderr_buf, event_queue, false).await
    }

    #[cfg(feature = "circuit")]
    async fn new_circuit(
        addr: SocketAddr,
        id: u64,
        stderr_buf: Rc<LogBuffer>,
        event_queue: events::Queue<TargetEvent>,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        Self::new_inner(addr, id, stderr_buf, event_queue, true).await
    }

    async fn new_inner(
        addr: SocketAddr,
        id: u64,
        stderr_buf: Rc<LogBuffer>,
        event_queue: events::Queue<TargetEvent>,
        circuit: bool,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        let id_string = format!("{}", id);
        let mut args = vec!["echo", "++ $SSH_CONNECTION ++", "&&", "remote_control_runner"];

        if circuit {
            args.push("--circuit");
        }

        args.push(id_string.as_str());

        // Before running remote_control_runner, we look up the environment
        // variable for $SSH_CONNECTION. This contains the IP address, including
        // scope_id, of the ssh client from the perspective of the ssh server.
        // This is useful because the target might need to use a specific
        // interface to talk to the host device.
        let mut ssh = build_ssh_command(addr, args).await?;

        tracing::debug!("Spawning new ssh instance: {:?}", ssh);

        let mut ssh = ssh
            .stdout(Stdio::piped())
            .stdin(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .context("running target overnet pipe")?;

        // todo(fxb/108692) remove this use of the global hoist when we put the main one in the environment context
        // instead -- this one is very deeply embedded, but isn't used by tests (that I've found).
        let (pipe_rx, mut pipe_tx) = futures::AsyncReadExt::split(
            overnet_pipe(hoist::hoist(), circuit).context("creating local overnet pipe")?,
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
                    tracing::error!("Failed to read ssh client address: {:?}", e);
                    None
                }
            };

        let copy_in = async move {
            if let Err(e) = copy_buf(stdout, &mut pipe_tx).await {
                tracing::error!("SSH stdout read failure: {:?}", e);
            }
        };
        let copy_out = async move {
            if let Err(e) =
                copy_buf(BufReader::with_capacity(BUFFER_SIZE, pipe_rx), &mut stdin).await
            {
                tracing::error!("SSH stdin write failure: {:?}", e);
            }
        };

        let log_stderr = async move {
            let mut stderr_lines = futures_lite::io::BufReader::new(stderr).lines();
            while let Some(result) = stderr_lines.next().await {
                match result {
                    Ok(line) => {
                        tracing::info!("SSH stderr: {}", line);
                        stderr_buf.push_line(line.clone());
                        event_queue
                            .push(TargetEvent::SshHostPipeErr(HostPipeErr::from(line)))
                            .unwrap_or_else(|e| {
                                tracing::warn!("queueing host pipe err event: {:?}", e)
                            });
                    }
                    Err(e) => tracing::error!("SSH stderr read failure: {:?}", e),
                }
            }
        };

        Ok((
            ssh_host_address,
            HostPipeChild {
                inner: ssh,
                task: Some(Task::local(async move {
                    futures::join!(copy_in, copy_out, log_stderr);
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
                tracing::info!("HostPipeChild exited with {}", result);
            }
            Ok(None) => {
                let _ = self
                    .kill()
                    .map_err(|e| tracing::warn!("failed to kill HostPipeChild: {:?}", e));
                let _ = self
                    .wait()
                    .map_err(|e| tracing::warn!("failed to clean up HostPipeChild: {:?}", e));
            }
            Err(e) => {
                tracing::warn!("failed to soft-wait HostPipeChild: {:?}", e);
                // defensive kill & wait, both may fail.
                let _ = self
                    .kill()
                    .map_err(|e| tracing::warn!("failed to kill HostPipeChild: {:?}", e));
                let _ = self
                    .wait()
                    .map_err(|e| tracing::warn!("failed to clean up HostPipeChild: {:?}", e));
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

    #[cfg(feature = "circuit")]
    pub fn new_circuit(target: Weak<Target>) -> impl Future<Output = Result<()>> {
        HostPipeConnection::new_with_cmd(target, HostPipeChild::new_circuit, RETRY_DELAY)
    }

    async fn new_with_cmd<F>(
        target: Weak<Target>,
        cmd_func: impl FnOnce(SocketAddr, u64, Rc<LogBuffer>, events::Queue<TargetEvent>) -> F
            + Copy
            + 'static,
        relaunch_command_delay: Duration,
    ) -> Result<()>
    where
        F: futures::Future<Output = Result<(Option<HostAddr>, HostPipeChild)>>,
    {
        loop {
            let target = target.upgrade().ok_or(anyhow!("Target has gone"))?;
            let target_nodename = target.nodename();
            tracing::debug!("Spawning new host-pipe instance to target {:?}", target_nodename);
            let log_buf = target.host_pipe_log_buffer();
            log_buf.clear();

            let ssh_address = target.ssh_address().ok_or_else(|| {
                anyhow!("target {:?} does not yet have an ssh address", target_nodename)
            })?;
            let (host_addr, mut cmd) =
                cmd_func(ssh_address, target.id(), log_buf.clone(), target.events.clone())
                    .await
                    .with_context(|| {
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
            tracing::debug!("host-pipe command res: {:?}", res);

            target.ssh_host_address.borrow_mut().take();

            match res {
                Ok(_) => {
                    return Ok(());
                }
                Err(e) => tracing::debug!("running cmd on {:?}: {:#?}", target_nodename, e),
            }

            // TODO(fxbug.dev/52038): Want an exponential backoff that
            // is sync'd with an explicit "try to start this again
            // anyway" channel using a select! between the two of them.
            Timer::new(relaunch_command_delay).await;
        }
    }
}

fn overnet_pipe(overnet_instance: &hoist::Hoist, circuit: bool) -> Result<fidl::AsyncSocket> {
    let (local_socket, remote_socket) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let local_socket = fidl::AsyncSocket::from_socket(local_socket)?;
    if circuit {
        #[cfg(feature = "circuit")]
        overnet_instance.start_client_socket(remote_socket).detach();
        #[cfg(not(feature = "circuit"))]
        unreachable!("Circuit-switched overnet should be disabled")
    } else {
        overnet_instance.connect_as_mesh_controller()?.attach_socket_link(remote_socket)?;
    }

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
        _buf: Rc<LogBuffer>,
        _events: events::Queue<TargetEvent>,
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
        _buf: Rc<LogBuffer>,
        _events: events::Queue<TargetEvent>,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        Err(anyhow!(ERR_CTX))
    }

    async fn start_child_ssh_failure(
        _addr: SocketAddr,
        _id: u64,
        _buf: Rc<LogBuffer>,
        events: events::Queue<TargetEvent>,
    ) -> Result<(Option<HostAddr>, HostPipeChild)> {
        events.push(TargetEvent::SshHostPipeErr(HostPipeErr::Unknown("foo".to_string()))).unwrap();
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
    async fn test_host_pipe_start_and_stop_ssh_failure() {
        let target = crate::target::Target::new_with_addrs(
            Some("flooooooooberdoober"),
            [TargetAddr::new("192.168.1.1:22").unwrap()].into(),
        );
        let events = target.events.clone();
        let task = Task::local(async move {
            events
                .wait_for(None, |e| {
                    assert_matches!(e, TargetEvent::SshHostPipeErr(_));
                    true
                })
                .await
                .unwrap();
        });
        // This is here to allow for the above task to get polled so that the `wait_for` can be
        // placed on at the appropriate time (before the failure occurs in the function below).
        futures_lite::future::yield_now().await;
        let res = HostPipeConnection::new_with_cmd(
            Rc::downgrade(&target),
            start_child_ssh_failure,
            Duration::default(),
        )
        .await;
        assert_matches!(res, Ok(_));
        // If things are not setup correctly this will hang forever.
        task.await;
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

    #[test]
    fn test_log_buffer_empty() {
        let buf = LogBuffer::new(2);
        assert!(buf.lines().is_empty());
    }

    #[test]
    fn test_log_buffer() {
        let buf = LogBuffer::new(2);

        buf.push_line(String::from("1"));
        buf.push_line(String::from("2"));
        buf.push_line(String::from("3"));

        assert_eq!(buf.lines(), vec![String::from("2"), String::from("3")]);
    }

    #[test]
    fn test_clear_log_buffer() {
        let buf = LogBuffer::new(2);

        buf.push_line(String::from("1"));
        buf.push_line(String::from("2"));

        buf.clear();

        assert!(buf.lines().is_empty());
    }
}
