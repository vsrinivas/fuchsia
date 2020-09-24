// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_sys::{
        ComponentControllerEvent, ComponentControllerMarker, FileDescriptor, LaunchInfo,
        LauncherProxy, TerminationReason::*,
    },
    futures::StreamExt,
    signal_hook,
    std::sync::{Arc, Mutex},
};

// TODO(fxbug.dev/53159): refactor fuchsia-runtime so we can use the constant from there on the host,
// rather than redefining it here.
const HANDLE_TYPE_FILE_DESCRIPTOR: i32 = 0x30;

#[ffx_plugin(LauncherProxy = "core/appmgr:out:fuchsia.sys.Launcher")]
pub async fn run_component(launcher_proxy: LauncherProxy, run: RunComponentCommand) -> Result<()> {
    let (control_proxy, control_server_end) = create_proxy::<ComponentControllerMarker>()?;
    let (sout, cout) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
    let (serr, cerr) =
        fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

    let mut stdout = async_std::io::stdout();
    let mut stderr = async_std::io::stderr();
    let copy_futures = futures::future::try_join(
        futures::io::copy(fidl::AsyncSocket::from_socket(cout)?, &mut stdout),
        futures::io::copy(fidl::AsyncSocket::from_socket(cerr)?, &mut stderr),
    );

    // This is only necessary until Overnet correctly handle setup for passed channels.
    // TODO(jwing) remove this once that is finished.
    control_proxy.detach().unwrap();

    let mut event_stream = control_proxy.take_event_stream();
    let term_event_future = async move {
        while let Some(result) = event_stream.next().await {
            match result? {
                ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                    return Ok((return_code, termination_reason));
                }
                _ => {}
            }
        }
        Err(anyhow!("no termination event received"))
    };

    let kill_arc = Arc::new(Mutex::new(false));
    let arc_mut = kill_arc.clone();
    unsafe {
        signal_hook::register(signal_hook::SIGINT, move || {
            let mut kill_started = arc_mut.lock().unwrap();
            if !*kill_started {
                println!("\nCaught interrupt, killing remote component.");
                let _ = control_proxy.kill();
                *kill_started = true;
            } else {
                // If for some reason the kill signal hangs, we want to give the user
                // a way to exit ffx.
                println!("Received second interrupt. Forcing exit...");
                std::process::exit(0);
            }
        })?;
    }

    let out_fd = FileDescriptor {
        type0: HANDLE_TYPE_FILE_DESCRIPTOR,
        type1: 0,
        type2: 0,
        handle0: Some(sout.into()),
        handle1: None,
        handle2: None,
    };

    let err_fd = FileDescriptor {
        type0: HANDLE_TYPE_FILE_DESCRIPTOR,
        type1: 0,
        type2: 0,
        handle0: Some(serr.into()),
        handle1: None,
        handle2: None,
    };

    let mut info = LaunchInfo {
        url: run.url,
        arguments: Some(run.args),
        out: Some(Box::new(out_fd)),
        err: Some(Box::new(err_fd)),
        additional_services: None,
        directory_request: None,
        flat_namespace: None,
    };

    launcher_proxy.create_component(&mut info, Some(control_server_end)).map_err(|_| {
        anyhow!(
            "Error starting component: {:?}. Ensure there is a target connected with `ffx list`"
        )
    })?;
    let (copy_res, term_event) = futures::join!(copy_futures, term_event_future);
    copy_res?;

    let (exit_code, termination_reason) = term_event?;
    if termination_reason != Exited {
        let message = match termination_reason {
            Unknown => "Unknown",
            UrlInvalid => "Component URL is invalid",
            PackageNotFound => "Package could not be found. Ensure `fx serve` is running",
            InternalError => "Internal error",
            ProcessCreationError => "Process creation error",
            RunnerFailed => "Runner failed to start",
            RunnerTerminated => "Runner crashed",
            Unsupported => "Component uses unsupported feature",
            RealmShuttingDown => "Realm is shutting down. Can't create component",
            Exited => unreachable!(),
        };
        eprintln!("Error: {}. \nThere may be a more detailed error in the system logs.", message);
    }
    std::process::exit(exit_code as i32);
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_sys::LauncherRequest};

    fn setup_fake_launcher_service() -> LauncherProxy {
        setup_oneshot_fake_launcher_proxy(|req| {
            match req {
                LauncherRequest::CreateComponent {
                    launch_info:
                        LaunchInfo {
                            url: _,
                            arguments: _,
                            out: _,
                            err: _,
                            additional_services: _,
                            directory_request: _,
                            flat_namespace: _,
                        },
                    controller,
                    control_handle: _,
                } => {
                    let (_, handle) = controller.unwrap().into_stream_and_control_handle().unwrap();
                    handle.send_on_terminated(0, Exited).unwrap();
                    // TODO: Add test coverage for FE behavior once fxbug.dev/49063 is resolved.
                }
            }
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_component() -> Result<()> {
        let url = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx".to_string();
        let args = vec!["test1".to_string(), "test2".to_string()];
        let run_cmd = RunComponentCommand { url, args };
        let launcher_proxy = setup_fake_launcher_service();
        let _response =
            run_component(launcher_proxy, run_cmd).await.expect("getting tests should not fail");
        Ok(())
    }
}
