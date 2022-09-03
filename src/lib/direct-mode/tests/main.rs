// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    direct_mode_vmm::{start, take_direct_vdso},
    fidl::endpoints::{create_proxy, ClientEnd, ServerEnd},
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{HandleBased, Rights, Socket, SocketOpts, Vmo},
    futures::prelude::*,
    process_builder::StartupHandle,
    std::ffi::CString,
    tracing::error,
};

/// Trivial service host that just launches a restricted and unrestricted
/// protocol that both return a trivial string.
#[fasync::run_singlethreaded]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(
            serve_suite(stream).unwrap_or_else(|_| error!("Failed to serve suite")),
        )
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("Failed to serve outgoing dir");
    fs.collect::<()>().await;
}

/// Trivial service that just returns the value "restricted"
async fn serve_suite(mut stream: ftest::SuiteRequestStream) -> Result<()> {
    while let Some(request) = stream.try_next().await? {
        match request {
            ftest::SuiteRequest::GetTests { iterator, .. } => fasync::Task::spawn(
                serve_case_iterator(iterator)
                    .unwrap_or_else(|_| error!("Failed to serve case iterator")),
            )
            .detach(),
            ftest::SuiteRequest::Run { tests, listener, .. } => run_tests(tests, listener).await?,
        }
    }
    Ok(())
}

async fn serve_case_iterator(iterator: ServerEnd<ftest::CaseIteratorMarker>) -> Result<()> {
    let mut stream = iterator.into_stream()?;
    let mut cases = vec![
        ftest::Case {
            name: Some("DirectMode.Kernel".to_string()),
            enabled: Some(true),
            ..ftest::Case::EMPTY
        },
        ftest::Case {
            name: Some("DirectMode.User".to_string()),
            enabled: Some(true),
            ..ftest::Case::EMPTY
        },
    ]
    .into_iter();
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::CaseIteratorRequest::GetNext { responder } => responder.send(&mut cases)?,
        }
    }
    Ok(())
}

async fn run_tests(
    tests: Vec<ftest::Invocation>,
    listener: ClientEnd<ftest::RunListenerMarker>,
) -> Result<()> {
    let vdso_vmo = take_direct_vdso();
    let listener = listener.into_proxy()?;
    for test in tests {
        let (case_listener, case_listener_server) = create_proxy::<ftest::CaseListenerMarker>()?;
        let (stderr, stderr_server) = Socket::create(SocketOpts::STREAM)?;
        let std_handles =
            ftest::StdHandles { err: Some(stderr_server), ..ftest::StdHandles::EMPTY };
        let name = test.name.clone().unwrap_or_default();
        listener.on_test_case_started(test, std_handles, case_listener_server)?;

        let vdso_vmo = vdso_vmo.duplicate_handle(Rights::SAME_RIGHTS)?;
        let handles = vec![StartupHandle {
            handle: stderr.duplicate_handle(Rights::SAME_RIGHTS)?.into_handle(),
            info: HandleInfo::new(HandleType::FileDescriptor, 2),
        }];
        let result = match name.as_str() {
            "DirectMode.Kernel" => kernel_test(vdso_vmo).await,
            "DirectMode.User" => user_test(vdso_vmo, handles).await,
            _ => Err(anyhow!("Unknown test")),
        };
        let status = if let Err(e) = result {
            stderr.write(e.to_string().as_bytes())?;
            ftest::Status::Failed
        } else {
            ftest::Status::Passed
        };

        let result = ftest::Result_ { status: Some(status), ..ftest::Result_::EMPTY };
        case_listener.finished(result)?;
    }
    listener.on_finished()?;
    Ok(())
}

// Test the behaviour of direct mode binary that acts as a kernel.
async fn kernel_test(vdso_vmo: Vmo) -> Result<()> {
    let args = vec![CString::new("/pkg/bin/kernel")?];
    start(vdso_vmo, args, vec![], vec![], vec![])
        .await
        .map_or(Ok(()), |_| Err(anyhow!("Expected failure")))
}

// Test the behaviour of direct mode binary that acts as an application.
async fn user_test(vdso_vmo: Vmo, handles: Vec<StartupHandle>) -> Result<()> {
    let args = vec![CString::new("/pkg/bin/user")?];
    start(vdso_vmo, args, vec![], vec![], handles).await
}
