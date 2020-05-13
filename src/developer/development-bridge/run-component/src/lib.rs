// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    ffx_run_component_args::RunComponentCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol::{
        ComponentControllerEvent, ComponentControllerMarker, RemoteControlProxy,
    },
    futures::StreamExt,
    signal_hook,
    std::sync::{Arc, Mutex},
};

///TODO(fxb/51594): use an attribute and proc macro to generate this
pub async fn ffx_plugin(
    remote_proxy: RemoteControlProxy,
    cmd: RunComponentCommand,
) -> Result<(), Error> {
    run_component(remote_proxy, cmd).await
}

pub async fn run_component(
    remote_proxy: RemoteControlProxy,
    run: RunComponentCommand,
) -> Result<(), Error> {
    let (proxy, server_end) = create_proxy::<ComponentControllerMarker>()?;
    let (sout, cout) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
    let (serr, cerr) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

    // This is only necessary until Overnet correctly handle setup for passed channels.
    // TODO(jwing) remove this once that is finished.
    let _ = proxy.ping();

    // TODO(fxb/49063): Can't use the writer in these threads due to static lifetime.
    let _out_thread = std::thread::spawn(move || loop {
        let mut buf = [0u8; 128];
        let n = cout.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
        if n > 0 {
            print!("{}", String::from_utf8_lossy(&buf));
        }
    });

    let _err_thread = std::thread::spawn(move || loop {
        let mut buf = [0u8; 128];
        let n = cerr.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
        if n > 0 {
            eprint!("{}", String::from_utf8_lossy(&buf));
        }
    });

    let event_stream = proxy.take_event_stream();
    let term_thread = std::thread::spawn(move || {
        let mut e = event_stream.take(1usize);
        while let Some(result) = futures::executor::block_on(e.next()) {
            match result {
                Ok(ComponentControllerEvent::OnTerminated { exit_code }) => {
                    println!("Component exited with exit code: {}", exit_code);
                    match exit_code {
                        -1 => println!("This exit code may mean that the specified package doesn't exist.\
                                    \nCheck that the package is in your universe (`fx set --with ...`) and that `fx serve` is running."),
                        _ => {},
                    };
                    break;
                }
                Err(err) => {
                    eprintln!("error reading component controller events. Component termination may not be detected correctly. {} ", err);
                }
            }
        }
    });

    let kill_arc = Arc::new(Mutex::new(false));
    let arc_mut = kill_arc.clone();
    unsafe {
        signal_hook::register(signal_hook::SIGINT, move || {
            let mut kill_started = arc_mut.lock().unwrap();
            if !*kill_started {
                println!("\nCaught interrupt, killing remote component.");
                let _ = proxy.kill();
                *kill_started = true;
            } else {
                // If for some reason the kill signal hangs, we want to give the user
                // a way to exit ffx.
                println!("Received second interrupt. Forcing exit...");
                std::process::exit(0);
            }
        })?;
    }

    let f = remote_proxy.start_component(
        &run.url,
        &mut run.args.iter().map(|s| s.as_str()),
        sout,
        serr,
        server_end,
    );
    match f.await? {
        Ok(_) => {}
        Err(_) => {
            return Err(anyhow!(
                "Error starting component. Ensure there is a target connected with `ffx list`"
            ));
        }
    };
    term_thread.join().unwrap();

    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlRequest},
        futures::TryStreamExt,
    };

    fn setup_fake_remote_server() -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(RemoteControlRequest::StartComponent {
                        component_url: _,
                        args: _,
                        component_stdout: _,
                        component_stderr: _,
                        controller: _,
                        responder,
                    }) => {
                        let _ = responder.send(&mut Ok(()));
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_run_component() -> Result<(), Error> {
        let url = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx".to_string();
        let args = vec!["test1".to_string(), "test2".to_string()];
        let run_cmd = RunComponentCommand { url, args };
        hoist::run(async move {
            // There isn't a lot we can test here right now since this method has an empty response.
            // We just check for an Ok(()) and leave it to a real integration to test behavior.
            let remote_proxy = setup_fake_remote_server();
            let _response =
                run_component(remote_proxy, run_cmd).await.expect("getting tests should not fail");
        });
        Ok(())
    }
}
