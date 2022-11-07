# CHANGELOG

## v2.1.0

This release mostly brings some new trait implementations for convenience.

* [Change tests text for copyright reasons](https://github.com/fflorent/nom_locate/pull/56)
* [Implement `From<T>` for `LocatedSpan`](https://github.com/fflorent/nom_locate/pull/57)
* [Implement `Deref` for `LocatedSpan`, returning the fragment](https://github.com/fflorent/nom_locate/pull/58)
* [Optionally implement `StableDeref` as well](https://github.com/fflorent/nom_locate/pull/65), if the `stable-deref-trait` feature is enabled.
* [Generalize `Compare`](https://github.com/fflorent/nom_locate/pull/58)
* [Generalize `HexDisplay`, and deprecated the `impl_hex_display!` macro which no longer does anything](https://github.com/fflorent/nom_locate/pull/58)
* [Add `LocatedSpan::get_line_beginning`](https://github.com/fflorent/nom_locate/pull/66), which returns the beginning of a line up to the end of the LocatedSpan. Useful to display human-friendly errors.


## v2.0.0

This release brings several breaking changes:

* [Error type for "position" is made generic](https://github.com/fflorent/nom_locate/pull/37)
* [`extra` property is now ignored when comparing LocatedSpan](https://github.com/fflorent/nom_locate/pull/46)
* [Dependency on nom now uses with `default-features = false`](https://github.com/fflorent/nom_locate/pull/47)
* [`offset`/`line`/`fragment` are now private attributes of the `LocatedSpan` structure](https://github.com/fflorent/nom_locate/pull/50),
  to fix an undefined behavior is they are modified. You now have to use the `location_offset()`, `location_line()`, and `fragment()` getters instead.
* [`LocatedSpanEx` is removed in favour of adding a generic type parameter to `LocatedSpan` which defaults to to `()`](https://github.com/fflorent/nom_locate/pull/51)


Additionally, there are a few documentation improvements:

* LocatedSpan should not be constructed in the middle of a parser.
* Fix typo in extra property docs for LocatedSpan

Finally, [`LocatedSpan` now implements `Display`](https://github.com/fflorent/nom_locate/pull/40)


## v1.0.0

We decided that the crate was mature enough to release the version 1.0.0. It doesn't bring much new things, still we are proud of this big move! :tada:

 - [Implement AsByte](https://github.com/fflorent/nom_locate/pull/33)

## v0.4.0

 - [Support for Nom v5](https://github.com/fflorent/nom_locate/pull/23)
 - [Add support for extra information to LocatedSpan](https://github.com/fflorent/nom_locate/pull/28)

Thanks to the people who made this release: @ProgVal, @peckpeck, @wycats, @dalance

## v0.3.1

Patch version:
 - [Support no_std](https://github.com/fflorent/nom_locate/pull/16)
 - [Fix compilation with verbose-errors](https://github.com/fflorent/nom_locate/issues/17)

## v0.3

 - [Support for Nom v4](https://github.com/fflorent/nom_locate/pull/10)
 - [Better performance for columns calculation](https://github.com/fflorent/nom_locate/issues/4)
 - [Speed up slices](https://github.com/fflorent/nom_locate/pull/15)
