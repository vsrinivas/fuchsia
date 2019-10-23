//! # Spline interpolation made easy.
//!
//! This crate exposes splines for which each sections can be interpolated independently of each
//! other – i.e. it’s possible to interpolate with a linear interpolator on one section and then
//! switch to a cubic Hermite interpolator for the next section.
//!
//! Most of the crate consists of three types:
//!
//!   - [`Key`], which represents the control points by which the spline must pass.
//!   - [`Interpolation`], the type of possible interpolation for each segment.
//!   - [`Spline`], a spline from which you can *sample* points by interpolation.
//!
//! When adding control points, you add new sections. Two control points define a section – i.e.
//! it’s not possible to define a spline without at least two control points. Every time you add a
//! new control point, a new section is created. Each section is assigned an interpolation mode that
//! is picked from its lower control point.
//!
//! # Quickly create splines
//!
//! ```
//! use splines::{Interpolation, Key, Spline};
//!
//! let start = Key::new(0., 0., Interpolation::Linear);
//! let end = Key::new(1., 10., Interpolation::default());
//! let spline = Spline::from_vec(vec![start, end]);
//! ```
//!
//! You will notice that we used `Interpolation::Linear` for the first key. The first key `start`’s
//! interpolation will be used for the whole segment defined by those two keys. The `end`’s
//! interpolation won’t be used. You can in theory use any [`Interpolation`] you want for the last
//! key. We use the default one because we don’t care.
//!
//! # Interpolate values
//!
//! The whole purpose of splines is to interpolate discrete values to yield continuous ones. This is
//! usually done with the [`Spline::sample`] method. This method expects the sampling parameter
//! (often, this will be the time of your simulation) as argument and will yield an interpolated
//! value.
//!
//! If you try to sample in out-of-bounds sampling parameter, you’ll get no value.
//!
//! ```
//! # use splines::{Interpolation, Key, Spline};
//! # let start = Key::new(0., 0., Interpolation::Linear);
//! # let end = Key::new(1., 10., Interpolation::Linear);
//! # let spline = Spline::from_vec(vec![start, end]);
//! assert_eq!(spline.sample(0.), Some(0.));
//! assert_eq!(spline.clamped_sample(1.), Some(10.));
//! assert_eq!(spline.sample(1.1), None);
//! ```
//!
//! It’s possible that you want to get a value even if you’re out-of-bounds. This is especially
//! important for simulations / animations. Feel free to use the `Spline::clamped_interpolation` for
//! that purpose.
//!
//! ```
//! # use splines::{Interpolation, Key, Spline};
//! # let start = Key::new(0., 0., Interpolation::Linear);
//! # let end = Key::new(1., 10., Interpolation::Linear);
//! # let spline = Spline::from_vec(vec![start, end]);
//! assert_eq!(spline.clamped_sample(-0.9), Some(0.)); // clamped to the first key
//! assert_eq!(spline.clamped_sample(1.1), Some(10.)); // clamped to the last key
//! ```
//!
//! # Polymorphic sampling types
//!
//! [`Spline`] curves are parametered both by the carried value (being interpolated) but also the
//! sampling type. It’s very typical to use `f32` or `f64` but really, you can in theory use any
//! kind of type; that type must, however, implement a contract defined by a set of traits to
//! implement. See [the documentation of this module](crate::interpolate) for further details.
//!
//! # Features and customization
//!
//! This crate was written with features baked in and hidden behind feature-gates. The idea is that
//! the default configuration (i.e. you just add `"splines = …"` to your `Cargo.toml`) will always
//! give you the minimal, core and raw concepts of what splines, keys / knots and interpolation
//! modes are. However, you might want more. Instead of letting other people do the extra work to
//! add implementations for very famous and useful traits – and do it in less efficient way, because
//! they wouldn’t have access to the internals of this crate, it’s possible to enable features in an
//! ad hoc way.
//!
//! This mechanism is not final and this is currently an experiment to see how people like it or
//! not. It’s especially important to see how it copes with the documentation.
//!
//! So here’s a list of currently supported features and how to enable them:
//!
//!   - **Serialization / deserialization.**
//!     - This feature implements both the `Serialize` and `Deserialize` traits from `serde` for all
//!       types exported by this crate.
//!     - Enable with the `"serialization"` feature.
//!   - **[cgmath](https://crates.io/crates/cgmath) implementors.**
//!     - Adds some useful implementations of `Interpolate` for some cgmath types.
//!     - Enable with the `"impl-cgmath"` feature.
//!   - **[nalgebra](https://crates.io/crates/nalgebra) implementors.**
//!     - Adds some useful implementations of `Interpolate` for some nalgebra types.
//!     - Enable with the `"impl-nalgebra"` feature.
//!   - **Standard library / no standard library.**
//!     - It’s possible to compile against the standard library or go on your own without it.
//!     - Compiling with the standard library is enabled by default.
//!     - Use `default-features = []` in your `Cargo.toml` to disable.
//!     - Enable explicitly with the `"std"` feature.
//!
//! [`Interpolation`]: crate::interpolation::Interpolation

#![cfg_attr(not(feature = "std"), no_std)]
#![cfg_attr(not(feature = "std"), feature(alloc))]
#![cfg_attr(not(feature = "std"), feature(core_intrinsics))]

#[cfg(not(feature = "std"))] extern crate alloc;

#[cfg(feature = "impl-cgmath")] mod cgmath;
pub mod interpolate;
pub mod interpolation;
pub mod iter;
pub mod key;
#[cfg(feature = "impl-nalgebra")] mod nalgebra;
pub mod spline;

pub use crate::interpolate::Interpolate;
pub use crate::interpolation::Interpolation;
pub use crate::key::Key;
pub use crate::spline::Spline;
