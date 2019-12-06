// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use failure::{Error, ResultExt};
use parking_lot::{Condvar, Mutex};
use rand::Rng;
use std::env::current_exe;
use std::io::{Read, Write};
use std::process::{Child, Command, Stdio};
use std::sync::Arc;

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

struct Daemon {
    child: Child,
    name: String,
}

impl Daemon {
    fn new_from_child(name: String, child: Child) -> Daemon {
        Daemon { name, child }
    }

    fn new(mut command: Command) -> Result<Daemon, Error> {
        let name = format!("{:?}", command);
        let child = command.spawn().context(format!("spawning command {}", name))?;
        Ok(Daemon { name, child })
    }
}

impl Drop for Daemon {
    fn drop(&mut self) {
        self.child.kill().expect(&format!("'{}' wasn't running", self.name));
        let _ = self.child.wait();
    }
}

struct Ascendd {
    daemons: Vec<Daemon>,
    socket: String,
}

impl Ascendd {
    fn new() -> Result<Ascendd, Error> {
        let socket = format!("/tmp/ascendd.{}.sock", rand::thread_rng().gen::<u128>());
        let mut cmd = cmd("ascendd");
        cmd.arg("--sockpath").arg(&socket);
        Ok(Ascendd { daemons: vec![Daemon::new(cmd)?], socket })
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
        self.daemons.push(Daemon::new(self.echo_cmd("server"))?);
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
        self.daemons.push(Daemon::new(self.interface_passing_cmd("server"))?);
        Ok(())
    }

    fn add_onet_host_pipe(
        &mut self,
        label: &str,
    ) -> Result<(Box<dyn Read + Send>, Box<dyn Write + Send>), Error> {
        let mut cmd = self.labelled_cmd("onet", label);
        cmd.arg("host-pipe").stdin(Stdio::piped()).stdout(Stdio::piped());
        let name = format!("{:?}", cmd);
        let mut child = cmd.spawn().context(format!("spawning command {}", name))?;
        let input = Box::new(child.stdout.take().ok_or(failure::format_err!("no stdout"))?);
        let output = Box::new(child.stdin.take().ok_or(failure::format_err!("no stdin"))?);
        self.daemons.push(Daemon::new_from_child(name, child));
        Ok((input, output))
    }

    fn onet_client(&self, cmd: &str) -> Command {
        let mut c = self.labelled_cmd("onet", cmd);
        c.arg(cmd);
        c.arg("--exclude_self");
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

fn run_client(mut cmd: Command) -> Result<(), Error> {
    let mut child = cmd.spawn().context("spawning client")?;
    assert!(child.wait().expect("client should succeed").success());
    Ok(())
}

mod tests {
    use super::*;

    #[test]
    fn echo() -> Result<(), Error> {
        echo_test()
    }

    pub fn echo_test() -> Result<(), Error> {
        let mut ascendd = Ascendd::new().context("creating ascendd")?;
        ascendd.add_echo_server().context("starting server")?;
        run_client(ascendd.echo_client()).context("running client")?;
        run_client(ascendd.onet_client("full-map")).context("running onet full-map")?;
        Ok(())
    }

    #[test]
    fn multiple_ascendd_echo() -> Result<(), Error> {
        multiple_ascendd_echo_test()
    }

    pub fn multiple_ascendd_echo_test() -> Result<(), Error> {
        let mut ascendd1 = Ascendd::new().context("creating ascendd 1")?;
        let mut ascendd2 = Ascendd::new().context("creating ascendd 2")?;
        bridge(&mut ascendd1, &mut ascendd2).context("bridging ascendds")?;
        ascendd1.add_echo_server().context("starting server")?;
        run_client(ascendd1.echo_client()).context("running client")?;
        //run_client(ascendd1.onet_client("full-map")).context("running onet full-map")?;
        Ok(())
    }

    #[test]
    fn interface_passing() -> Result<(), Error> {
        interface_passing_test()
    }

    pub fn interface_passing_test() -> Result<(), Error> {
        let mut ascendd = Ascendd::new().context("creating ascendd")?;
        ascendd.add_interface_passing_server().context("starting server")?;
        run_client(ascendd.interface_passing_client()).context("running client")?;
        run_client(ascendd.onet_client("full-map")).context("running onet full-map")?;
        Ok(())
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
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let test_fn = match args.test.as_str() {
        "echo" => tests::echo_test,
        "multiple_ascendd_echo" => tests::multiple_ascendd_echo_test,
        "interface_passing" => tests::interface_passing_test,
        x => failure::bail!("Unknown test {}", x),
    };
    let errors = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let concurrency = args.concurrency.unwrap_or(1);
    let sema = Semaphore::new(concurrency);
    for i in 0..args.jobs.unwrap_or(1) {
        let guard = Arc::new(sema.access());
        eprintln!("START JOB: {}", i);
        let errors = errors.clone();
        std::thread::Builder::new()
            .spawn(move || {
                if let Err(e) = test_fn() {
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
