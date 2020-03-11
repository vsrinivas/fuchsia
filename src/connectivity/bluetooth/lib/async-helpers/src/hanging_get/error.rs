use {
    futures::channel::{mpsc, oneshot},
    thiserror::Error,
};

#[derive(Error, Debug, PartialEq)]
pub enum HangingGetServerError {
    #[error("The HangingGetBroker associated with this handle has been dropped.")]
    NoBroker,
    #[error("This handle is sending messages faster than the broker can process them.")]
    RateLimit,
    #[error("An unknown error condition was encountered.")]
    Unknown,
    #[error("Cannot have multiple concurrent observers for a single client")]
    MultipleObservers,
}

impl From<mpsc::SendError> for HangingGetServerError {
    fn from(error: mpsc::SendError) -> Self {
        if error.is_disconnected() {
            HangingGetServerError::NoBroker
        } else if error.is_full() {
            HangingGetServerError::RateLimit
        } else {
            HangingGetServerError::Unknown
        }
    }
}

impl From<oneshot::Canceled> for HangingGetServerError {
    fn from(_: oneshot::Canceled) -> Self {
        HangingGetServerError::Unknown
    }
}
