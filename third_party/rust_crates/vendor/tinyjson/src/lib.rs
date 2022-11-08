// Suppress warning which prefers `matches!` macro to `match` statement since the macro was
// introduced in recent Rust 1.42. This library should support older Rust.
#![allow(clippy::match_like_matches_macro)]

mod generator;
mod json_value;
mod parser;

pub use generator::*;
pub use json_value::{InnerAsRef, InnerAsRefMut, JsonValue, UnexpectedValue};
pub use parser::*;
