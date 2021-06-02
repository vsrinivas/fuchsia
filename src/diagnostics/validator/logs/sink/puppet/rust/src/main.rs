// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_validate_logs::{
    LogSinkPuppetRequest, LogSinkPuppetRequestStream, PuppetInfo, RecordSpec,
};
use fuchsia_async::Task;
use fuchsia_component::server::ServiceFs;
use fuchsia_runtime as rt;
use fuchsia_zircon::AsHandleRef;
use futures::prelude::*;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    diagnostics_log::init!();
    tracing::info!("Puppet started.");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|r: LogSinkPuppetRequestStream| r);
    fs.take_and_serve_directory_handle().unwrap();

    while let Some(incoming) = fs.next().await {
        Task::spawn(run_puppet(incoming)).detach();
    }
}

async fn run_puppet(mut requests: LogSinkPuppetRequestStream) {
    while let Some(next) = requests.try_next().await.unwrap() {
        match next {
            LogSinkPuppetRequest::StopInterestListener { responder } => {
                // TODO (https://fxbug.dev/77781): Rust should support StopInterestListener.
                responder.send().unwrap();
            }
            LogSinkPuppetRequest::GetInfo { responder } => {
                let mut info = PuppetInfo {
                    tag: None,
                    pid: rt::process_self().get_koid().unwrap().raw_koid(),
                    tid: rt::thread_self().get_koid().unwrap().raw_koid(),
                };
                responder.send(&mut info).unwrap();
            }
            LogSinkPuppetRequest::EmitLog {
                responder,
                spec: RecordSpec { file, line, record },
            } => {
                // tracing 0.2 will let us to emit non-'static events directly, no downcasting
                tracing::dispatcher::get_default(|dispatcher| {
                    let publisher: &diagnostics_log::Publisher = dispatcher.downcast_ref().unwrap();
                    publisher.event_for_testing(&file, line, record.clone());
                });
                responder.send().unwrap();
            }
            LogSinkPuppetRequest::EmitPrintfLog { spec: _, responder } => {
                responder.send().unwrap();
            }
        }
    }
}
