//! A general purpose crate for working with timeouts and delays with futures.
//!
//! # Examples
//!
//! ```no_run
//! # #[async_std::main]
//! # async fn main() {
//! use std::time::Duration;
//! use futures_timer::Delay;
//!
//! let now = Delay::new(Duration::from_secs(3)).await;
//! println!("waited for 3 secs");
//! # }
//! ```

#![deny(missing_docs)]
#![warn(missing_debug_implementations)]

mod arc_list;
mod atomic_waker;
mod delay;
mod global;
mod heap;
mod heap_timer;
mod timer;

use arc_list::{ArcList, Node};
use atomic_waker::AtomicWaker;
use heap::{Heap, Slot};
use heap_timer::HeapTimer;
use timer::{ScheduledTimer, Timer, TimerHandle};

pub use self::delay::Delay;
