# 2.2.0

> Mon Oct 17th 2019

- Add `Interpolation::StrokeBezier`.

# 2.1.1

> Mon Oct 17th 2019

- Licensing support in the crate.

# 2.1

> Mon Sep 30th 2019

- Add `Spline::sample_with_key` and `Spline::clamped_sample_with_key`. Those methods allow one to
  perform the regular `Spline::sample` and `Spline::clamped_sample` but also retreive the base
  key that was used to perform the interpolation. The key can be inspected to get the base time,
  interpolation, etc. The next key is also returned, if present.

# 2.0.1

> Tue Sep 24th 2019

- Fix the cubic Bézier curve interpolation. The “output” tangent is now taken by mirroring the
  next key’s tangent around its control point.

# 2.0.0

> Mon Sep 23rd 2019

## Major changes

- Add support for [Bézier curves](https://en.wikipedia.org/wiki/B%C3%A9zier_curve).
- Because of Bézier curves, the `Interpolation` type now has one more type variable to know how we
  should interpolate with Bézier.

## Minor changes

- Add `Spline::get`, `Spline::get_mut` and `Spline::replace`.

# 1.0

> Sun Sep 22nd 2019

## Major changes

- Make `Spline::clamped_sample` failible via `Option` instead of panicking.
- Add support for polymorphic sampling type.

## Minor changes

- Add the `std` feature (and hence support for `no_std`).
- Add `impl-nalgebra` feature.
- Add `impl-cgmath` feature.
- Add support for adding keys to splines.
- Add support for removing keys from splines.

## Patch changes

- Migrate to Rust 2018.
- Documentation typo fixes.

# 0.2.3

> Sat 13th October 2018

- Add the `"impl-nalgebra"` feature gate. It gives access to some implementors for the `nalgebra`
  crate.
- Enhance the documentation.

# 0.2.2

> Sun 30th September 2018

- Bump version numbers (`splines-0.2`) in examples.
- Fix several typos in the documentation.

# 0.2.1

> Thu 20th September 2018

- Enhance the features documentation.

# 0.2

> Thu 6th September 2018

- Add the `"std"` feature gate, that can be used to compile with the standard library.
- Add the `"impl-cgmath"` feature gate in order to make optional, if wanted, the `cgmath`
  dependency.
- Enhance the documentation.

# 0.1.1

> Wed 8th August 2018

- Add a feature gate, `"serialization"`, that can be used to automatically derive `Serialize` and
  `Deserialize` from the [serde](https://crates.io/crates/serde) crate.
- Enhance the documentation.

# 0.1

> Sunday 5th August 2018

- Initial revision.
