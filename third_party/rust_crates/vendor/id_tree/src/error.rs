use std::error::Error;
use std::fmt;

///
/// Enum for all of the possible `NodeId` errors that could occur.
///
#[derive(Debug, Eq, PartialEq)]
pub enum NodeIdError {
    /// Occurs when a `NodeId` is used on a `Tree` from which it did not originate.
    InvalidNodeIdForTree,
    /// Occurs when a `NodeId` is used on a `Tree` after the corresponding `Node` has been removed.
    NodeIdNoLongerValid,
}

impl NodeIdError {
    fn to_string(&self) -> &str {
        match *self {
            NodeIdError::InvalidNodeIdForTree => "The given NodeId belongs to a different Tree.",
            NodeIdError::NodeIdNoLongerValid => {
                "The given NodeId is no longer valid. The Node in question has been \
                 removed."
            }
        }
    }
}

impl fmt::Display for NodeIdError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "NodeIdError: {}", self.to_string())
    }
}

impl Error for NodeIdError {
    fn description(&self) -> &str {
        self.to_string()
    }
}
