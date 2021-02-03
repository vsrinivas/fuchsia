// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides a derive macro [`FromEnum`](from_enum_derive::FromEnum) to easily generate
//! conversion impls to extract an enum variant's value from a newtype-style enum.
//!
//! This is most useful when writing generic functions that can operate on any variant's inner type.
//!
//! # Example
//! ```
//! ## #[allow(dead_code)]
//! #[derive(FromEnum)]
//! enum Animal {
//!   Dog(DogParams),
//!   Cat(CatParams),
//! }
//!
//! fn handle_animal<T: FromEnum<Animal> + Debug>(animal: &Animal) {
//!     match FromEnum::from_enum(animal) {
//!         Some(params) => println!("Found my animal {:?}", params),
//!         None => println!("This is not my animal"),
//!     }
//! }
//!
//! ## #[derive(Debug)]
//! ## struct DogParams;
//! ## struct CatParams;
//! ## fn main() {
//! let animal = Animal::Dog(DogParams);
//! handle_animal::<DogParams>(&animal);
//! ## }

pub use from_enum_derive::FromEnum;

/// Attempts to match the enum `E` to find a variant with a single field of type `Self`.
pub trait FromEnum<E> {
    fn from_enum(e: &E) -> Option<&Self>;
}
