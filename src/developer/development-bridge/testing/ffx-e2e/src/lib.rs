// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod test {
    use {
        std::process::{Child, Command, Stdio},
        std::str::from_utf8,
        std::thread::sleep,
        std::time::Duration,
    };

    const DAEMON_LIST_RETRY_COUNT: usize = 30;
    const DAEMON_LIST_RETRY_DELAY: Duration = Duration::from_secs(1);

    fn build_ffx_command(subcommand: &str, args: Vec<&str>) -> Command {
        // Our build config drops the ffx binary in ./test_data/ffx-e2e/ffx
        let mut ffx_dir = std::env::current_exe().unwrap();
        ffx_dir.pop();

        ffx_dir.push("test_data");
        ffx_dir.push("ffx-e2e");
        ffx_dir.push("ffx");
        let mut c = Command::new(ffx_dir);
        c.arg(subcommand).args(args);
        return c;
    }

    fn setup_daemon_and_wait_for_device() -> Child {
        let daemon = build_ffx_command("daemon", vec![]).stdout(Stdio::piped()).spawn().unwrap();
        log::info!("looking for daemon");

        // Unfortunately we have no better polling mechanism right now that won't
        // have a side-effect on the test.
        // TODO(jwing) replace this with `ffx wait` once available: fxb/49657.
        for _ in 0..DAEMON_LIST_RETRY_COUNT {
            let list = build_ffx_command("list", vec![]).output().unwrap();

            let out = from_utf8(&list.stdout).unwrap();

            if out.starts_with("step-atom-yard-juicy") {
                log::info!("device found");
            }

            if out.contains("overnet_started: true") {
                log::info!("overnet started successfully");
                return daemon;
            }

            log::info!("overnet not yet started on device");
            sleep(DAEMON_LIST_RETRY_DELAY);
        }

        panic!("failed to find device");
    }

    fn exec_run_component_test(args: Vec<&str>) -> String {
        let mut c = build_ffx_command("run-component", args);

        let child = c.stdout(Stdio::piped()).output().unwrap();
        let out = from_utf8(&child.stdout).unwrap();
        println!("COMPONENT EXECUTION OUTPUT:");
        println!("{}", out);
        println!("END COMPONENT OUTPUT");

        return out.to_owned();
    }

    // Note this needs to be called *before assertions*.
    // Otherwise the test will leave a stale daemon behind if it fails.
    fn teardown() {
        let s = build_ffx_command("quit", vec![]).status().unwrap();
        assert!(s.success());
    }

    #[test]
    fn test_run_hello_world() {
        let _ = setup_daemon_and_wait_for_device();

        let out = exec_run_component_test(vec![
            "fuchsia-pkg://fuchsia.com/hello-world-e2e#meta/hello-world-e2e.cm",
        ]);

        teardown();

        assert!(out.contains("Hello, world!"));
        assert!(out.contains("Component exited with exit code: 0\n"));
    }
}
