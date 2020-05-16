// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    ffx_core::ffx_plugin,
    ffx_run_component_args::RunComponentCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol::{
        ComponentControllerEvent, ComponentControllerMarker, RemoteControlProxy,
    },
    futures::StreamExt,
    signal_hook,
    std::sync::{Arc, Mutex},
};

#[ffx_plugin()]
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

    let mut stdout = async_std::io::stdout();
    let mut stderr = async_std::io::stderr();
    let copy_futures = futures::future::try_join(
        futures::io::copy(fidl::AsyncSocket::from_socket(cout)?, &mut stdout),
        futures::io::copy(fidl::AsyncSocket::from_socket(cerr)?, &mut stderr),
    );

    let event_stream = proxy.take_event_stream();
    let term_thread = std::thread::spawn(move || {
        let mut e = event_stream.take(1usize);
        while let Some(result) = futures::executor::block_on(e.next()) {
            match result {
                Ok(ComponentControllerEvent::OnTerminated { exit_code }) => {
                    if exit_code == -1 {
                        eprintln!("This exit code may mean that the specified package doesn't exist.\
                                                                                                            \nCheck that the package is in your universe (`fx set --with ...`) and that `fx serve` is running.");
                    }
                    std::process::exit(exit_code as i32);
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
    let (copy_res, f) = futures::join!(copy_futures, f);
    copy_res?;
    match f? {
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
