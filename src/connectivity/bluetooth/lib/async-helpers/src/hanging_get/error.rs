// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    futures::channel::{mpsc, oneshot},
    thiserror::Error,
};

#[derive(Error, Debug)]
pub enum HangingGetServerError {
    #[error("Cannot have multiple concurrent observers for a single client")]
    MultipleObservers,
    #[error("The HangingGetBroker associated with this handle has been dropped.")]
    NoBroker,
    #[error("This handle is sending messages faster than the broker can process them.")]
    RateLimit,
    #[error("Error: {}", .0)]
    Generic(anyhow::Error),
}

impl PartialEq for HangingGetServerError {
    fn eq(&self, other: &Self) -> bool {
        use HangingGetServerError::*;
        match (self, other) {
            (MultipleObservers, MultipleObservers)
            | (NoBroker, NoBroker)
            | (RateLimit, RateLimit) => true,
            _ => false,
        }
    }
}

impl From<mpsc::SendError> for HangingGetServerError {
    fn from(error: mpsc::SendError) -> Self {
        if error.is_disconnected() {
            HangingGetServerError::NoBroker
        } else if error.is_full() {
            HangingGetServerError::RateLimit
        } else {
            HangingGetServerError::Generic(format_err!(
                "Unknown SendError error condition: {}",
                error
            ))
        }
    }
}

impl From<oneshot::Canceled> for HangingGetServerError {
    fn from(e: oneshot::Canceled) -> Self {
        HangingGetServerError::Generic(e.into())
    }
}

impl From<anyhow::Error> for HangingGetServerError {
    /// Try downcasting to more specific error types, falling back to `Generic` if that fails.
    fn from(e: anyhow::Error) -> Self {
        e.downcast::<mpsc::SendError>().map_or_else(HangingGetServerError::Generic, Self::from)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn error_partial_eq_impl() {
        use HangingGetServerError::*;
        let variants = [MultipleObservers, NoBroker, RateLimit, Generic(format_err!("err"))];
        for (i, a) in variants.iter().enumerate() {
            for (j, b) in variants.iter().enumerate() {
                // variants with the same index are equal except `Generic` errors are _never_ equal.
                if i == j && i != 3 && j != 3 {
                    assert_eq!(a, b);
                } else {
                    assert_ne!(a, b);
                }
            }
        }
    }
}
