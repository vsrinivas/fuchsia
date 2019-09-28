# International Components for Unicode (ICU) use in Fuchsia

Fuchsia code uses [ICU library](http://site.icu-project.org/) for the common
internationalization services such as date, time, timezone, locale and language
handling.

# Use in C and C++

The ICU library is imported through a third-party dependency
`//third_party/icu`. As an example use of the library, one can look at the [C++
wisdom example][wisdomcpp].  This is a sample client-server collaboration that
requests, serves and prints on screen date and time information using several
different languages, calendars and scripts.

# Use in Rust

The ICU library is available in rust programs as well, through a binding of the
ICU4C library into Rust.

The library is subdivided into [several
crates](https://fuchsia-docs.firebaseapp.com/rust/?search=rust_icu), each one
corresponding to a specific part of the ICU4C headers, and named after the
corresponding one.  Today, the functionality is partial, and is constructed to
serve Fuchsia's Unicode needs.

As a demonstration of the rust bindings for ICU4C, we made a rust equivalent of
the wisdom server.  This example is available as the [rust wisdom
example][wisdomrust].

[wisdomcpp]: /garnet/examples/intl/wisdom/cpp/ 
[wisdomrust]: /garnet/examples/intl/wisdom/rust/
