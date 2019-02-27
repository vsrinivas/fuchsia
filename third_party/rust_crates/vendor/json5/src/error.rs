use pest;
use serde::{de, ser};
use std::fmt::{self, Display};

use crate::de::Rule;

/// Alias for a `Result` with error type `json5::Error`
pub type Result<T> = std::result::Result<T, Error>;

/// A bare bones error type which currently just collapses all the underlying errors in to a single
/// string... This is fine for displaying to the user, but not very useful otherwise. Work to be
/// done here.
#[derive(Clone, Debug, PartialEq)]
pub enum Error {
    /// Just shove everything in a single variant for now.
    Message(String),
}

impl From<pest::error::Error<Rule>> for Error {
    fn from(err: pest::error::Error<Rule>) -> Self {
        Error::Message(err.to_string())
    }
}

impl ser::Error for Error {
    fn custom<T: Display>(msg: T) -> Self {
        Error::Message(msg.to_string())
    }
}

impl de::Error for Error {
    fn custom<T: Display>(msg: T) -> Self {
        Error::Message(msg.to_string())
    }
}

impl Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(std::error::Error::description(self))
    }
}

impl std::error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::Message(ref msg) => msg,
        }
    }
}
