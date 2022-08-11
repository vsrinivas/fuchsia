// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_test_components as ftest, fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    std::io,
};

/// Contains all the information required to verify that a particular path contains some set of
/// rights.
struct RightsTestCase {
    path: &'static str,
    rights: fio::OpenFlags,
}

impl RightsTestCase {
    /// Constructs a new rights test case.
    fn new(path: &'static str, rights: fio::OpenFlags) -> RightsTestCase {
        RightsTestCase { path: path, rights: rights }
    }

    /// Returns every right not available for this path.
    fn unavailable_rights(&self) -> Vec<fio::OpenFlags> {
        let all_rights = [
            fio::OpenFlags::RIGHT_READABLE,
            fio::OpenFlags::RIGHT_WRITABLE,
            fio::OpenFlags::RIGHT_EXECUTABLE,
        ];

        let mut unavailable_rights: Vec<fio::OpenFlags> = vec![];
        for right_to_check in all_rights.iter() {
            if *right_to_check | self.rights != self.rights {
                unavailable_rights.push(*right_to_check);
            }
        }
        unavailable_rights
    }

    /// Verifies that the rights are correctly implemented for the type.
    fn verify(&self) -> io::Result<()> {
        fdio::open_fd(self.path, self.rights)?;
        for invalid_right in self.unavailable_rights() {
            if let Ok(_) = fdio::open_fd(self.path, invalid_right) {
                return Err(io::Error::new(
                    io::ErrorKind::Other,
                    format!(
                        "Path: {} with rights: {:?} was opened with invalid rights {:?}",
                        self.path, self.rights, invalid_right
                    ),
                ));
            }
        }
        return Ok(());
    }
}

#[fuchsia::main]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_trigger_service(stream).await.expect("failed to run trigger service");
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_trigger_service(mut stream: ftest::TriggerRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        handle_trigger(event).await;
    }
    Ok(())
}

async fn handle_trigger(event: ftest::TriggerRequest) {
    let ftest::TriggerRequest::Run { responder } = event;
    let tests = [
        RightsTestCase::new("/read_only", fio::OpenFlags::RIGHT_READABLE),
        RightsTestCase::new(
            "/read_write",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        ),
        RightsTestCase::new(
            "/read_write_dup",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        ),
        RightsTestCase::new(
            "/read_exec",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        ),
        RightsTestCase::new("/read_only_after_scoped", fio::OpenFlags::RIGHT_READABLE),
    ];
    for test_case in tests.iter() {
        if let Err(test_failure) = test_case.verify() {
            responder
                .send(&format!(
                    "Directory rights test failed: {} - {}",
                    test_case.path, test_failure,
                ))
                .expect("failed to send trigger response");
            return;
        }
    }
    responder.send("All tests passed").expect("failed to send trigger response");
}
