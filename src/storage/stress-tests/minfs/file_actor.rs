// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_WRITABLE},
    fuchsia_zircon::Status,
    log::info,
    storage_stress_test_utils::{data::FileFactory, io::Directory},
    stress_test::actor::{Actor, ActorError},
};

// TODO(fxbug.dev/67497): This actor is very basic. At the moment, this is fine, since this is a
// v0 implementation of minfs stress tests. Eventually, we should write stress tests that exercise
// minfs as a true POSIX filesystem.
pub struct FileActor {
    pub factory: FileFactory,
    pub home_dir: Directory,
}

impl FileActor {
    pub fn new(factory: FileFactory, home_dir: Directory) -> Self {
        Self { factory, home_dir }
    }

    async fn create_file(&mut self) -> Result<(), Status> {
        let filename = self.factory.generate_filename();
        let data_bytes = self.factory.generate_bytes();

        // Write the file to disk
        let file = self
            .home_dir
            .open_file(
                &filename,
                OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
            )
            .await?;
        file.write(&data_bytes).await?;
        file.close().await
    }
}

#[async_trait]
impl Actor for FileActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        match self.create_file().await {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            // Any other error is assumed to come from an intentional crash.
            // The environment verifies that an intentional crash occurred
            // and will panic if that is not the case.
            Err(s) => {
                info!("File actor got status: {}", s);
                Err(ActorError::ResetEnvironment)
            }
        }
    }
}
