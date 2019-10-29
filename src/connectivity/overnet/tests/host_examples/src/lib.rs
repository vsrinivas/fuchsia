// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {

    use rand::Rng;

    use std::env::current_exe;
    use std::io::{Read, Write};
    use std::process::{Child, Command, Stdio};

    use timebomb::timeout_ms;

    const TEST_TIMEOUT_MS: u32 = 60_000;

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

        fn new(mut command: Command) -> Daemon {
            let name = format!("{:?}", command);
            let child = command.spawn().unwrap();
            Daemon { name, child }
        }
    }

    impl Drop for Daemon {
        fn drop(&mut self) {
            self.child.kill().expect(&format!("'{}' wasn't running", self.name));
        }
    }

    struct Ascendd {
        daemons: Vec<Daemon>,
        socket: String,
    }

    impl Ascendd {
        fn new() -> Ascendd {
            let socket = format!("/tmp/ascendd.{}.sock", rand::thread_rng().gen::<u128>());
            let mut cmd = cmd("ascendd");
            cmd.arg("--sockpath").arg(&socket);
            Ascendd { daemons: vec![Daemon::new(cmd)], socket }
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

        fn add_echo_server(&mut self) {
            self.daemons.push(Daemon::new(self.echo_cmd("server")))
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

        fn add_interface_passing_server(&mut self) {
            self.daemons.push(Daemon::new(self.interface_passing_cmd("server")))
        }

        fn add_onet_host_pipe(
            &mut self,
            label: &str,
        ) -> (Box<dyn Read + Send>, Box<dyn Write + Send>) {
            let mut cmd = self.labelled_cmd("onet", label);
            cmd.arg("host-pipe").stdin(Stdio::piped()).stdout(Stdio::piped());
            let name = format!("{:?}", cmd);
            let mut child = cmd.spawn().unwrap();
            let input = Box::new(child.stdout.take().unwrap());
            let output = Box::new(child.stdin.take().unwrap());
            self.daemons.push(Daemon::new_from_child(name, child));
            (input, output)
        }

        fn onet_client(&self, cmd: &str) -> Command {
            let mut c = self.labelled_cmd("onet", cmd);
            c.arg(cmd);
            c.arg("--exclude_self");
            c
        }
    }

    fn bridge(a: &mut Ascendd, b: &mut Ascendd) {
        let (mut i1, mut o1) = a.add_onet_host_pipe("onet1");
        let (mut i2, mut o2) = b.add_onet_host_pipe("onet2");
        std::thread::spawn(move || std::io::copy(&mut i1, &mut o2));
        std::thread::spawn(move || std::io::copy(&mut i2, &mut o1));
    }

    fn run_client(mut cmd: Command) {
        timeout_ms(
            move || assert!(cmd.spawn().unwrap().wait().expect("client should succeed").success()),
            TEST_TIMEOUT_MS,
        );
    }

    #[test]
    fn echo_test() {
        let mut ascendd = Ascendd::new();
        ascendd.add_echo_server();
        run_client(ascendd.echo_client());
        run_client(ascendd.onet_client("full-map"));
    }

    #[test]
    fn multiple_ascendd_echo_test() {
        let mut ascendd1 = Ascendd::new();
        let mut ascendd2 = Ascendd::new();
        bridge(&mut ascendd1, &mut ascendd2);
        ascendd1.add_echo_server();
        run_client(ascendd1.echo_client());
        run_client(ascendd1.onet_client("full-map"));
    }

    #[test]
    fn interface_passing_test() {
        let mut ascendd = Ascendd::new();
        ascendd.add_interface_passing_server();
        run_client(ascendd.interface_passing_client());
        run_client(ascendd.onet_client("full-map"));
    }
}
