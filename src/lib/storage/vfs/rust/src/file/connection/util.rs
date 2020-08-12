// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{execution_scope::ExecutionScope, file::File},
    fuchsia_zircon::Status,
    std::{ops::Deref, sync::Arc},
};

/// This struct is a RAII wrapper around a file that will call close() on it unless the
/// succeed() function is called.
pub struct OpenFile<T: 'static + File> {
    file: Option<Arc<T>>,
    scope: ExecutionScope,
}

impl<T: 'static + File> OpenFile<T> {
    pub fn new(file: Arc<T>, scope: ExecutionScope) -> Self {
        Self { file: Some(file), scope }
    }

    /// Explicitly close the file.
    pub async fn close(&mut self) -> Result<(), Status> {
        let file = self.file.take().ok_or(Status::BAD_HANDLE)?;
        file.close().await
    }
}

impl<T: 'static + File> Drop for OpenFile<T> {
    fn drop(&mut self) {
        if let Some(file) = self.file.take() {
            let _ = self.scope.spawn(async move {
                let _ = file.close().await;
            });
        }
    }
}

impl<T: 'static + File> Deref for OpenFile<T> {
    type Target = Arc<T>;

    fn deref(&self) -> &Self::Target {
        self.file.as_ref().unwrap()
    }
}
