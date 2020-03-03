// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon::Status, futures::future::BoxFuture, std::sync::Arc};

pub mod io1;

/// Connection buffer initialization result. It is either a byte buffer with the file content, or
/// an error code.
pub type InitBufferResult = Result<Vec<u8>, Status>;

/// Result of the [`FileWithPerConnectionBuffer::init_buffer()`] call.
pub enum AsyncInitBuffer {
    /// Buffer was allocated and initialized or an error occurred.
    Immediate(InitBufferResult),

    /// Buffer initialization requires asynchronous operation(s). Connection will run this future
    /// to completion and will then use the generated buffer or will close itself with the
    /// specified error.
    Future(BoxFuture<'static, InitBufferResult>),
}

pub type UpdateResult = Result<(), Status>;

pub enum AsyncUpdate {
    Immediate(UpdateResult),
    Future(BoxFuture<'static, UpdateResult>),
}

/// [`FileConnection`] needs to be able to initialize a per-connection buffer when new connection
/// is established. This is the API that file entries need to implement to provide this
/// functionality to the file connections.
pub trait FileWithPerConnectionBuffer: Send + Sync {
    /// This method is called when a new connection is first established.
    fn init_buffer(self: Arc<Self>) -> AsyncInitBuffer;

    /// This method is called when a connection is closed and file content was modified.
    fn update(self: Arc<Self>, buffer: Vec<u8>) -> AsyncUpdate;
}
