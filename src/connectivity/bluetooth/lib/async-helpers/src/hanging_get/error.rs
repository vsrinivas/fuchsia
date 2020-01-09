use {futures::channel::mpsc, thiserror::Error};

#[derive(Error, Debug)]
pub enum HangingGetServerError {
    #[error("The HangingGetBroker associated with this handle has been dropped.")]
    NoBroker,
    #[error("This handle is sending messages faster than the broker can process them.")]
    RateLimit,
    #[error("An unknown error condition was encountered.")]
    Unknown,
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
