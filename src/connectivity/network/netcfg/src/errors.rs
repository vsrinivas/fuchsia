// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Display;

#[derive(Debug)]
pub(super) enum Error {
    NonFatal(anyhow::Error),
    Fatal(anyhow::Error),
}

/// Extension trait similar to [`anyhow::Context`] used for internal types.
pub(super) trait ContextExt {
    /// Wrap the error value with additional context.
    fn context<C>(self, context: C) -> Self
    where
        C: Display + Send + Sync + 'static;

    /// Wrap the error value with additional context that is evaluated
    /// only once an error does occur.
    fn with_context<C, F>(self, f: F) -> Self
    where
        C: Display + Send + Sync + 'static,
        F: FnOnce() -> C;
}

impl ContextExt for Error {
    fn context<C>(self, context: C) -> Error
    where
        C: Display + Send + Sync + 'static,
    {
        match self {
            Error::NonFatal(e) => Error::NonFatal(e.context(context)),
            Error::Fatal(e) => Error::Fatal(e.context(context)),
        }
    }

    fn with_context<C, F>(self, f: F) -> Error
    where
        C: Display + Send + Sync + 'static,
        F: FnOnce() -> C,
    {
        self.context(f())
    }
}

impl<T, E: ContextExt> ContextExt for Result<T, E> {
    fn context<C>(self, context: C) -> Result<T, E>
    where
        C: Display + Send + Sync + 'static,
    {
        self.map_err(|e| e.context(context))
    }

    fn with_context<C, F>(self, f: F) -> Self
    where
        C: Display + Send + Sync + 'static,
        F: FnOnce() -> C,
    {
        self.map_err(|e| e.with_context(f))
    }
}
