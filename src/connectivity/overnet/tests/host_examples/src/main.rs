// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use argh::FromArgs;
use parking_lot::{Condvar, Mutex};
use rand::Rng;
use std::collections::HashMap;
use std::env::current_exe;
use std::io::{Read, Write};
use std::process::{Child, Command, Stdio};
use std::sync::Arc;
use std::time::Duration;
use tempfile::{NamedTempFile, TempPath};

struct Semaphore {
    inner: Arc<(Mutex<usize>, Condvar)>,
}

struct SemaphoreGuard {
    inner: Arc<(Mutex<usize>, Condvar)>,
}

impl Semaphore {
    pub fn new(size: usize) -> Self {
        Self { inner: Arc::new((Mutex::new(size), Condvar::new())) }
    }

    pub fn access(&self) -> SemaphoreGuard {
        let inner = self.inner.clone();
        let (mutex, condvar) = &*self.inner;
        let mut n = mutex.lock();
        while *n == 0 {
            condvar.wait(&mut n);
        }
        *n -= 1;
        SemaphoreGuard { inner }
    }
}

impl Drop for SemaphoreGuard {
    fn drop(&mut self) {
        let (mutex, condvar) = &*self.inner;
        *mutex.lock() += 1;
        condvar.notify_one();
    }
}

fn cmd(name: &str) -> Command {
    let mut path = current_exe().unwrap();
    path.pop();
    path.push("overnet_host_examples_test_commands");
    path.push(name);
    let mut cmd = Command::new(path);
    cmd.env("RUST_BACKTRACE", "1");
    cmd
}

fn copy_output(mut out: impl std::io::Read + Send + 'static) -> TempPath {
    let (mut file, path) = NamedTempFile::new().unwrap().into_parts();
    std::thread::spawn(move || {
        std::io::copy(&mut out, &mut file).unwrap();
    });
    path
}

struct ChildInfo {
    name: String,
    stdout: Option<TempPath>,
    stderr: Option<TempPath>,
}

impl ChildInfo {
    fn new(name: String, child: &mut Child) -> Self {
        Self {
            name,
            stdout: child.stdout.take().map(copy_output),
            stderr: child.stderr.take().map(copy_output),
        }
    }

    fn show(&self) {
        println!("*******************************************************************************");
        println!("** {}", self.name);
        if let Some(f) = self.stdout.as_ref() {
            println!("** STDOUT:");
            println!("{}", std::fs::read_to_string(f).unwrap());
        }
        if let Some(f) = self.stderr.as_ref() {
            println!("** STDERR:");
            println!("{}", std::fs::read_to_string(f).unwrap());
        }
    }
}

struct Daemon {
    child: Child,
    details: ChildInfo,
}

impl Daemon {
    fn new_from_child(name: String, mut child: Child) -> Daemon {
        Daemon { details: ChildInfo::new(name, &mut child), child }
    }

    fn new(mut command: Command, args: &TestArgs) -> Result<Daemon, Error> {
        let name = format!("{:?}", command);
        if args.capture_output {
            command.stdout(Stdio::piped());
            command.stderr(Stdio::piped());
        }
        let child = command.spawn().context(format!("spawning command {}", name))?;
        Ok(Self::new_from_child(name, child))
    }
}

impl Drop for Daemon {
    fn drop(&mut self) {
        self.child.kill().expect(&format!("'{}' wasn't running", self.details.name));
        let _ = self.child.wait();
    }
}

struct TestContextInner {
    daemons: Vec<Daemon>,
    run_things: Vec<ChildInfo>,
}

struct TestContext {
    state: Mutex<TestContextInner>,
    args: TestArgs,
}

lazy_static::lazy_static! {
    static ref REPORT_MUTEX: Mutex<()> = Mutex::new(());
}

impl TestContext {
    fn new(args: TestArgs) -> Arc<Self> {
        Arc::new(Self {
            state: Mutex::new(TestContextInner { daemons: vec![], run_things: vec![] }),
            args,
        })
    }

    fn run_client(&self, mut cmd: Command) -> Result<String, Error> {
        let name = format!("{:?}", cmd);
        cmd.stdout(Stdio::piped());
        if self.args.capture_output {
            cmd.stderr(Stdio::piped());
        }
        let mut child = cmd.spawn().context("spawning client")?;
        let child_info = ChildInfo::new(name, &mut child);
        let stdout_path: std::path::PathBuf =
            child_info.stdout.as_ref().unwrap().to_owned().to_path_buf();
        self.state.lock().run_things.push(child_info);
        assert!(child.wait().expect("client should succeed").success());
        Ok(std::fs::read_to_string(stdout_path)?)
    }

    fn new_daemon(&self, command: Command) -> Result<(), Error> {
        let d = Daemon::new(command, &self.args)?;
        self.state.lock().daemons.push(d);
        Ok(())
    }

    fn new_daemon_from_child(&self, name: String, child: Child) {
        self.state.lock().daemons.push(Daemon::new_from_child(name, child));
    }

    fn show_reports_if_failed(
        &self,
        f: impl FnOnce() -> Result<(), Error> + Send + 'static,
    ) -> Result<(), Error> {
        let (tx, rx) = std::sync::mpsc::channel();
        let j = std::thread::spawn(move || tx.send(f()));
        let r = (|| -> Result<(), Error> {
            rx.recv_timeout(Duration::from_secs(120))
                .context("receiving completion notificiation")?
                .context("running test code")?;
            j.join()
                .map_err(|_| format_err!("Failed to join thread"))?
                .context("sending test completion notification")?;
            Ok(())
        })();

        if let Err(ref e) = &r {
            let rm = REPORT_MUTEX.lock();
            let this = &*self.state.lock();

            println!(
                "*******************************************************************************"
            );
            println!("** ERROR: {:?}", e);

            for thing in this.run_things.iter() {
                thing.show();
            }

            for thing in this.daemons.iter() {
                thing.details.show();
            }

            println!(
                "*******************************************************************************"
            );
            eprintln!("WROTE LOG FOR ERROR: {:?}", e);
            drop(rm);
        }

        r
    }
}

pub struct SocketPassingArgs {
    pub client_sends: Option<String>,
    pub server_sends: Option<String>,
}

struct Ascendd {
    socket: String,
    ctx: Arc<TestContext>,
}

impl Ascendd {
    fn new(ctx: &Arc<TestContext>) -> Result<Ascendd, Error> {
        let socket = format!("/tmp/ascendd.{}.sock", rand::thread_rng().gen::<u128>());
        let mut cmd = cmd("ascendd");
        cmd.arg("--sockpath").arg(&socket);
        ctx.new_daemon(cmd)?;
        Ok(Ascendd { ctx: ctx.clone(), socket })
    }

    fn cmd(&self, name: &str) -> Command {
        let mut c = cmd(name);
        c.env("ASCENDD", &self.socket);
        c
    }

    fn labelled_cmd(&self, name: &str, label: &str) -> Command {
        let mut c = self.cmd(name);
        c.env("OVERNET_CONNECTION_LABEL", label);
        c
    }

    fn echo_cmd(&self, kind: &str) -> Command {
        let mut c = self.labelled_cmd("overnet_echo", kind);
        c.arg(kind);
        c
    }

    fn echo_client(&self) -> Command {
        let mut c = self.echo_cmd("client");
        c.arg("AUTOMATED_TEST");
        c
    }

    fn add_echo_server(&mut self) -> Result<(), Error> {
        self.ctx.new_daemon(self.echo_cmd("server"))?;
        Ok(())
    }

    fn interface_passing_cmd(&self, kind: &str) -> Command {
        let mut c = self.labelled_cmd("overnet_interface_passing", kind);
        c.arg(kind);
        c
    }

    fn interface_passing_client(&self) -> Command {
        let mut c = self.interface_passing_cmd("client");
        c.arg("AUTOMATED_TEST");
        c
    }

    fn add_interface_passing_server(&mut self) -> Result<(), Error> {
        self.ctx.new_daemon(self.interface_passing_cmd("server"))?;
        Ok(())
    }

    fn socket_passing_cmd(&self, kind: &str) -> Command {
        let mut c = self.labelled_cmd("overnet_socket_passing", kind);
        c.arg(kind);
        c
    }

    fn socket_passing_client(&self, args: &SocketPassingArgs) -> Command {
        let mut c = self.socket_passing_cmd("client");
        if let Some(send) = &args.client_sends {
            c.arg("--send");
            c.arg(send);
        }
        if let Some(expect) = &args.server_sends {
            c.arg("--expect");
            c.arg(expect);
        }
        c
    }

    fn add_socket_passing_server(&mut self, args: &SocketPassingArgs) -> Result<(), Error> {
        let mut c = self.socket_passing_cmd("server");
        if let Some(send) = &args.server_sends {
            c.arg("--send");
            c.arg(send);
        }
        if let Some(expect) = &args.client_sends {
            c.arg("--expect");
            c.arg(expect);
        }
        self.ctx.new_daemon(c)
    }

    fn add_onet_host_pipe(
        &mut self,
        label: &str,
    ) -> Result<(Box<dyn Read + Send>, Box<dyn Write + Send>), Error> {
        let mut cmd = self.labelled_cmd("onet", label);
        cmd.arg("host-pipe").stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        let name = format!("{:?}", cmd);
        let mut child = cmd.spawn().context(format!("spawning command {}", name))?;
        let input = Box::new(child.stdout.take().ok_or(anyhow::format_err!("no stdout"))?);
        let output = Box::new(child.stdin.take().ok_or(anyhow::format_err!("no stdin"))?);
        self.ctx.new_daemon_from_child(name, child);
        Ok((input, output))
    }

    fn onet_client(&self, cmd: &str, exclude_self: Option<bool>) -> Command {
        let mut c = self.labelled_cmd("onet", cmd);
        c.arg(cmd);
        if let Some(exclude_self) = exclude_self {
            c.arg("--exclude-self");
            c.arg(format!("{}", exclude_self));
        }
        c
    }
}

fn bridge(a: &mut Ascendd, b: &mut Ascendd) -> Result<(), Error> {
    let (i1, o1) = a.add_onet_host_pipe("onet1").context("adding host-pipe onet1")?;
    let (i2, o2) = b.add_onet_host_pipe("onet2").context("adding host-pipe onet2")?;
    let (i1, o1, i2, o2) = (
        Arc::new(Mutex::new(i1)),
        Arc::new(Mutex::new(o1)),
        Arc::new(Mutex::new(i2)),
        Arc::new(Mutex::new(o2)),
    );
    std::thread::Builder::new()
        .spawn(move || std::io::copy(&mut *i1.lock(), &mut *o2.lock()))
        .context("spawning copy thread 1")?;
    std::thread::Builder::new()
        .spawn(move || std::io::copy(&mut *i2.lock(), &mut *o1.lock()))
        .context("spawning copy thread 2")?;
    Ok(())
}

mod tests {
    use super::*;

    #[test]
    fn echo() -> Result<(), Error> {
        echo_test(Default::default())
    }

    pub fn echo_test(args: TestArgs) -> Result<(), Error> {
        let ctx = TestContext::new(args);
        let mut ascendd = Ascendd::new(&ctx).context("creating ascendd")?;
        ctx.clone().show_reports_if_failed(move || {
            ascendd.add_echo_server().context("starting server")?;
            ctx.run_client(ascendd.echo_client()).context("running client")?;
            ctx.run_client(ascendd.onet_client("full-map", Some(true)))
                .context("running onet full-map")?;
            Ok(())
        })
    }

    #[test]
    fn multiple_ascendd_echo() -> Result<(), Error> {
        multiple_ascendd_echo_test(Default::default())
    }

    pub fn multiple_ascendd_echo_test(args: TestArgs) -> Result<(), Error> {
        let ctx = TestContext::new(args);
        let mut ascendd1 = Ascendd::new(&ctx).context("creating ascendd 1")?;
        let mut ascendd2 = Ascendd::new(&ctx).context("creating ascendd 2")?;
        ctx.clone().show_reports_if_failed(move || {
            bridge(&mut ascendd1, &mut ascendd2).context("bridging ascendds")?;
            ascendd1.add_echo_server().context("starting server")?;
            ctx.run_client(ascendd1.echo_client()).context("running client")?;
            let output = ctx
                .run_client(ascendd1.onet_client("list-peers", None))
                .context("running onet list-peers")?;
            // The following should be running: 2 ascendd's, 2 bridging onet's,
            // the query onet, and the server
            assert_eq!(output.lines().count(), 6);
            assert_eq!(output.matches("Ascendd").count(), 2);
            #[cfg(target_os = "linux")]
            assert_eq!(output.matches("Linux").count(), 6);
            #[cfg(target_os = "macos")]
            assert_eq!(output.matches("Mac").count(), 6);
            Ok(())
        })
    }

    #[test]
    fn interface_passing() -> Result<(), Error> {
        interface_passing_test(Default::default())
    }

    pub fn interface_passing_test(args: TestArgs) -> Result<(), Error> {
        let ctx = TestContext::new(args);
        let mut ascendd = Ascendd::new(&ctx).context("creating ascendd")?;
        ctx.clone().show_reports_if_failed(move || {
            ascendd.add_interface_passing_server().context("starting server")?;
            ctx.run_client(ascendd.interface_passing_client()).context("running client")?;
            ctx.run_client(ascendd.onet_client("full-map", Some(false)))
                .context("running onet full-map")?;
            Ok(())
        })
    }

    #[test]
    fn socket_passing_both_ways() -> Result<(), Error> {
        socket_passing_test(
            Default::default(),
            SocketPassingArgs {
                client_sends: Some("AAAAAAAAAAAAAAAAAAA".to_string()),
                server_sends: Some("BBBBBBBBBBBBBBBB".to_string()),
            },
        )
    }

    #[test]
    fn socket_passing_client_to_server_send() -> Result<(), Error> {
        socket_passing_test(
            Default::default(),
            SocketPassingArgs {
                client_sends: Some("ABCDEFGHIJKLMNOPQRSTUVWXYZ".to_string()),
                server_sends: None,
            },
        )
    }

    #[test]
    fn socket_passing_server_to_client_send() -> Result<(), Error> {
        socket_passing_test(
            Default::default(),
            SocketPassingArgs { client_sends: None, server_sends: Some("0123456789".to_string()) },
        )
    }

    pub fn socket_passing_test(args: TestArgs, config: SocketPassingArgs) -> Result<(), Error> {
        let ctx = TestContext::new(args);
        let mut ascendd = Ascendd::new(&ctx).context("creating ascendd")?;
        ctx.clone().show_reports_if_failed(move || {
            ascendd.add_socket_passing_server(&config).context("starting server")?;
            ctx.run_client(ascendd.socket_passing_client(&config)).context("running client")?;
            ctx.run_client(ascendd.onet_client("full-map", Some(true)))
                .context("running onet full-map")?;
            Ok(())
        })
    }
}

pub struct TestArgs {
    capture_output: bool,
}

impl Default for TestArgs {
    fn default() -> Self {
        TestArgs { capture_output: true }
    }
}

#[derive(FromArgs, Debug)]
/// Arguments
struct Args {
    #[argh(option)]
    /// test to run
    test: String,

    #[argh(option)]
    /// number of instances to run concurrently
    concurrency: Option<usize>,

    #[argh(option)]
    /// number of instances to run total
    jobs: Option<usize>,

    #[argh(option)]
    /// enable log capture (default true)
    capture_output: Option<bool>,
}

impl Args {
    fn test_args(&self) -> TestArgs {
        TestArgs { capture_output: self.capture_output.unwrap_or(true) }
    }
}

fn box_test<F: 'static + Send + Sync + Fn(TestArgs) -> Result<(), Error>>(
    f: F,
) -> Arc<dyn 'static + Send + Sync + Fn(TestArgs) -> Result<(), Error>> {
    Arc::new(f)
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let tests: HashMap<String, Arc<dyn 'static + Send + Sync + Fn(TestArgs) -> Result<(), Error>>> =
        vec![
            ("echo".to_string(), box_test(tests::echo_test)),
            ("multiple_ascendd_echo".to_string(), box_test(tests::multiple_ascendd_echo_test)),
            ("interface_passing".to_string(), box_test(tests::interface_passing_test)),
            (
                "socket_passing_both_ways".to_string(),
                box_test(|a| {
                    tests::socket_passing_test(
                        a,
                        SocketPassingArgs {
                            client_sends: Some("AAAAAAAAAAAAAAAAAAA".to_string()),
                            server_sends: Some("BBBBBBBBBBBBBBBB".to_string()),
                        },
                    )
                }),
            ),
            (
                "socket_passing_client_to_server_send".to_string(),
                box_test(|a| {
                    tests::socket_passing_test(
                        a,
                        SocketPassingArgs {
                            client_sends: Some("ABCDEFGHIJKLMNOPQRSTUVWXYZ".to_string()),
                            server_sends: None,
                        },
                    )
                }),
            ),
            (
                "socket_passing_server_to_client_send".to_string(),
                box_test(|a| {
                    tests::socket_passing_test(
                        a,
                        SocketPassingArgs {
                            client_sends: None,
                            server_sends: Some("0123456789".to_string()),
                        },
                    )
                }),
            ),
        ]
        .into_iter()
        .collect();
    let test_selector = args.test.as_str();
    let test_fns: Vec<Arc<dyn 'static + Send + Sync + Fn(TestArgs) -> Result<(), Error>>> =
        match test_selector {
            "*" => tests.iter().map(|(_, test)| test.clone()).collect(),
            _ => vec![tests
                .get(test_selector)
                .ok_or(anyhow::format_err!("Unknown test {}", test_selector))?
                .clone()],
        };
    let errors = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let concurrency = args.concurrency.unwrap_or(1);
    let sema = Semaphore::new(concurrency);
    let mut test_it = test_fns.iter().cycle();
    for i in 0..args.jobs.unwrap_or(1) {
        let guard = Arc::new(sema.access());
        {
            let rm = REPORT_MUTEX.lock();
            eprintln!("START JOB: {}", i);
            drop(rm);
        }
        let errors = errors.clone();
        let args = args.test_args();
        let test_fn = test_it.next().unwrap().clone();
        std::thread::Builder::new()
            .spawn(move || {
                if let Err(e) = test_fn(args) {
                    eprintln!("ERROR: {:?}", e);
                    errors.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                }
                drop(guard);
            })
            .context(format!("spawning test thread {}", i))?;
    }
    // force waiting for all jobs to finish
    let mut end = Vec::new();
    for _ in 0..concurrency {
        end.push(sema.access());
    }
    drop(end);
    let errors = errors.load(std::sync::atomic::Ordering::SeqCst);
    eprintln!("error count = {}", errors);
    assert_eq!(errors, 0);

    Ok(())
}
