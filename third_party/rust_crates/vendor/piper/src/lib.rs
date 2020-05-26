//! # Piper
//!
//! Async pipes, channels, mutexes, and more.
//!
//! > **NOTE:** This crate is still a work in progress. Coming soon.
//!
//! - Arc and Mutex - same as std except they implement asyncread/asyncwrite
//! - Event - for notifying async tasks and threads, advanced AtomicWaker
//! - Lock - async lock
//! - chan - Sender and Receiver implement Sink and Stream
//! - pipe - Reader and Writer implement AsyncRead and AsyncWrite
//!
//! ## TODO's
//!
//!  - change w.await to listener.await

#![warn(missing_docs, missing_debug_implementations, rust_2018_idioms)]

mod arc;
mod chan;
mod event;
mod lock;
mod mutex;
mod pipe;

pub use arc::Arc;
pub use chan::{chan, Receiver, Sender};
pub use event::{Event, EventListener};
pub use lock::{Lock, LockGuard};
pub use mutex::{Mutex, MutexGuard};
pub use pipe::{pipe, Reader, Writer};
