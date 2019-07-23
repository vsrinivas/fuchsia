// Copyright 2016 Steven Allen
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(test, feature(test))]
//! A crate for quickly generating unique IDs with guaranteed properties.
//!
//! This crate currently includes guaranteed process unique IDs but may include new ID types in the
//! future.

#[cfg(feature = "serde_support")]
#[macro_use] extern crate serde_derive;

mod process_unique_id;

pub use process_unique_id::ProcessUniqueId;
