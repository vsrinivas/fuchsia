// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::constants::SSH_PRIV,
    anyhow::{anyhow, Result},
    ffx_config::file,
    std::net::SocketAddr,
    std::process::Command,
};

static DEFAULT_SSH_OPTIONS: &'static [&str] = &[
    "-o CheckHostIP=no",
    "-o StrictHostKeyChecking=no",
    "-o UserKnownHostsFile=/dev/null",
    "-o ServerAliveInterval=1",
    "-o ServerAliveCountMax=10",
    "-o LogLevel=ERROR",
    // FIXME(http://fxbug.dev/77015): Temporarily create a reverse tunnel for port 8084, which will
    // be used by `ffx repository` to serve packages to a target device.
    "-R 8084:localhost:8084",
];

pub async fn build_ssh_command(addr: SocketAddr, command: Vec<&str>) -> Result<Command> {
    if command.is_empty() {
        return Err(anyhow!("missing SSH command"));
    }

    let key: String = file(SSH_PRIV).await?;

    let mut c = Command::new("ssh");

    let mut addr_str = format!("{}", addr);
    let colon_port = addr_str.split_off(addr_str.rfind(':').expect("socket format includes port"));

    c.arg("-p").arg(&colon_port[1..]);

    c.arg("-i").arg(key);

    c.args(DEFAULT_SSH_OPTIONS).arg(addr_str).args(&command);

    return Ok(c);
}

#[cfg(test)]
mod test {
    use {super::*, serial_test::serial};

    #[fuchsia_async::run_singlethreaded(test)]
    #[serial]
    async fn test_build_ssh_command() {
        ffx_config::init_config_test().unwrap();
        let key_path = std::env::current_exe().unwrap();
        let key_path = key_path.to_str().take().unwrap();
        std::env::set_var("FUCHSIA_SSH_KEY", key_path);

        let addr = "192.168.0.1:22".parse().unwrap();

        let result = build_ssh_command(addr, vec!["ls"]).await.unwrap();
        let dbgstr = format!("{:?}", result);
        let search_string = &format!("\"-p\" \"22\" \"-i\" \"{}\"", key_path);

        assert!(dbgstr.contains(search_string), "`{}` not found in `{}`", search_string, dbgstr);
        assert!(dbgstr.contains(&addr.ip().to_string()), "{}", dbgstr);
        for option in DEFAULT_SSH_OPTIONS {
            assert!(dbgstr.contains(option));
        }
    }
}
