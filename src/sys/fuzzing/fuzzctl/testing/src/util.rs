// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fuchsia_async as fasync,
    fuchsia_fuzzctl::{OutputSink, Writer},
    futures::Future,
};

/// Wraps a given `future` to display any returned errors using the given `writer`.
pub fn create_task<F, O>(future: F, writer: &Writer<O>) -> fasync::Task<()>
where
    F: Future<Output = Result<()>> + 'static,
    O: OutputSink,
{
    let writer = writer.clone();
    let wrapped = || async move {
        if let Err(e) = future.await {
            writer.error(format!("task failed: {:?}", e));
        }
    };
    fasync::Task::local(wrapped())
}
