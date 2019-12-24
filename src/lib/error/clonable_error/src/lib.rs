// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    std::{fmt, sync::Arc},
    thiserror::Error,
};

/// A wrapper for `Error` that implements `Clone`.
#[derive(Clone, Error)]
pub struct ClonableError {
    err: Arc<Error>,
}

impl From<Error> for ClonableError {
    fn from(err: Error) -> Self {
        Self { err: Arc::new(err) }
    }
}

impl fmt::Display for ClonableError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.err.fmt(f)
    }
}

impl fmt::Debug for ClonableError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.err.fmt(f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;
    use thiserror::Error;

    #[derive(Debug, Error, Clone)]
    #[error("\"{}\" happened: {}", msg, cause)]
    struct FooError {
        msg: String,
        #[source]
        cause: ClonableError,
    }

    #[test]
    fn clone() {
        let cause = format_err!("root cause").into();
        let err = FooError { msg: "something bad happened".to_string(), cause };
        let cloned_err = err.clone();
        assert_eq!(format!("{:?}", err), format!("{:?}", cloned_err));
    }
}
