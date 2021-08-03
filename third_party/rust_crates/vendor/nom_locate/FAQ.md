# FAQ

## How to use LocatedSpan with my own input type?

LocatedSpan has been designed to wrap any input type. By default it wraps `&str` and `&[u8]` but it should work with any other types.

To do so, all you need is to ensure that your input type implements these traits:
 - `nom::InputLength`
 - `nom::Slice`
 - `nom::InputIter`
 - `nom::Compare`
 - `nom::Offset`
 - `nom::CompareResult`
 - `nom::FindSubstring`
 - `nom::ParseTo`
 - `nom::AsBytes`

And ensure that what represents a char in your input type implements `nom::FindToken`.

Then you may use all the `impl_*` macros exposed by the library (see the [crate documentation](https://docs.rs/nom_locate/)).

## `get_column` is not accurate

Your input probably doesn't have ASCII characters only. You'd probably better use `get_column_utf8` when your input is contains UTF-8 extensions, having in mind that it is much slower.


