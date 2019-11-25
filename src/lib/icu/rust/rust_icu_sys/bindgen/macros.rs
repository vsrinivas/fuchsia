// Macros for changing function names.


// This library was build with version renaming, so rewrite every function name
// with its name with version number appended.

// The macro below will rename a symbol `foo::bar` to `foo::bar_64` (where "64")
// may be some other number depending on the ICU library in use.
#[macro_export]
macro_rules! versioned_function {
    ($i:ident) => {
        paste::expr! {
          [< $i _64 >]
        }
    };
}
