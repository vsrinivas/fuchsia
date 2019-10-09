// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error types

use std::{error::Error, fmt};

/// An error encountered while processing a work item in a [`super::WorkQueue`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TaskError<E> {
    /// The [`super::WorkQueue`] was dropped before processing this request.
    Canceled,

    /// The task failed with the given error.
    Inner(E),
}

impl<E> TaskError<E> {
    /// Returns this error's inner error.
    ///
    /// # Panics
    ///
    /// Panics if self is not an inner error. Callers must guarantee that the associated
    /// [`super::WorkQueue`] will not be dropped before all work items are processed.
    pub fn unwrap_inner(self) -> E {
        match self {
            TaskError::Canceled => panic!("TaskError is not inner error type"),
            TaskError::Inner(e) => e,
        }
    }
}

impl<E> fmt::Display for TaskError<E>
where
    E: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TaskError::Canceled => write!(f, "queue dropped before processing this task"),
            TaskError::Inner(e) => write!(f, "error running task: {}", e),
        }
    }
}

impl<E> Error for TaskError<E>
where
    E: Error + 'static,
{
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            TaskError::Canceled => None,
            TaskError::Inner(e) => Some(e),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_unwrap_inner_with_inner_unwraps() {
        assert_eq!(TaskError::Inner("foo").unwrap_inner(), "foo");
    }

    #[test]
    #[should_panic(expected = "not inner")]
    fn test_unwrap_inner_with_canceled_panics() {
        TaskError::<()>::Canceled.unwrap_inner();
    }
}
