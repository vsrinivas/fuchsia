// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_sys::{
        ComponentControllerRequest, LauncherRequest, LauncherRequestStream, TerminationReason,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx},
    futures::prelude::*,
    futures::{select, StreamExt, TryStreamExt},
};

const SPAWN_HELLO_URL: &str = "fuchsia-pkg://fuchsia.com/remote-control#meta/spawn_hello_world.cm";
const SPAWN_AND_KILL_URL: &str = "fuchsia-pkg://fuchsia.com/remote-control#meta/spawn_and_kill.cm";
const NON_EXISTENT_URL: &str = "fuchsia-pkg://fuchsia.com/remote-control#meta/non_existent.cm";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["mock-sys-launcher"])?;
    log::info!("started mock sys launcher");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            run_launcher_service(stream).await.expect("Failed to run mock launcher.")
        });
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

async fn run_launcher_service(mut stream: LauncherRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            LauncherRequest::CreateComponent { launch_info, controller, control_handle: _ } => {
                match launch_info.url.as_str() {
                    SPAWN_HELLO_URL => {
                        let out_sock = zx::Socket::from(launch_info.out.unwrap().handle0.unwrap());
                        out_sock.write("Hello, world!".as_bytes()).unwrap();

                        let err_sock = zx::Socket::from(launch_info.err.unwrap().handle0.unwrap());
                        err_sock.write("Hello, stderr!".as_bytes()).unwrap();

                        let (_, control_handle) =
                            controller.unwrap().into_stream_and_control_handle()?;
                        control_handle.send_on_terminated(0, TerminationReason::Exited).unwrap();
                    }
                    SPAWN_AND_KILL_URL => {
                        let (mut stream, control_handle) =
                            controller.unwrap().into_stream_and_control_handle()?;

                        let timer_fut =
                            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(100)));
                        select! {
                        req = stream.next() => {
                            match req {
                                Some(Ok(ComponentControllerRequest::Kill { control_handle: _})) => {
                                    control_handle
                                        .send_on_terminated(-1024, TerminationReason::Unknown)
                                        .unwrap();
                                    return Ok(());
                                },
                                r => {
                                    panic!(
                                        "got unexpected or invalid call to component controller: {:?}", r
                                    );
                                }
                            }
                        },
                        _ = timer_fut.fuse() => {}
                        };

                        panic!("didn't see kill call");
                    }
                    NON_EXISTENT_URL => {
                        let (_, control_handle) =
                            controller.unwrap().into_stream_and_control_handle()?;
                        control_handle
                            .send_on_terminated(-1, TerminationReason::PackageNotFound)
                            .unwrap();
                    }
                    _ => {
                        panic!(format!("got unrecognized URL: {}", launch_info.url));
                    }
                }
            }
        }
    }
    Ok(())
}
