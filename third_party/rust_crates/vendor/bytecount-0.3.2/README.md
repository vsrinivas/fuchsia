# bytecount

Counting bytes really fast

[![Build Status](https://travis-ci.org/llogiq/bytecount.svg?branch=master)](https://travis-ci.org/llogiq/bytecount)
[![Windows build status](https://ci.appveyor.com/api/projects/status/github/llogiq/bytecount?svg=true)](https://ci.appveyor.com/project/llogiq/bytecount)
[![Current Version](http://meritbadge.herokuapp.com/bytecount)](https://crates.io/crates/bytecount)
[![License: Apache 2.0/MIT](https://img.shields.io/crates/l/bytecount.svg)](#license)

This uses the "hyperscreamingcount" algorithm by Joshua Landau to count bytes faster than anything else. The
[newlinebench](https://github.com/llogiq/newlinebench) repository has further benchmarks.

To use bytecount in your crate, if you have [cargo-edit](https://github.com/killercup/cargo-edit), just type
`cargo add bytecount` in a terminal with the crate root as the current path. Otherwise you can manually edit your
`Cargo.toml` to add `bytecount = 0.3.2` to your `[dependencies]` section.

In your crate root (`lib.rs` or `main.rs`, depending on if you are writing a
library or application), add `extern crate bytecount;`. Now you can simply use
`bytecount::count` as follows:

```Rust
extern crate bytecount;

fn main() {
    let mytext = "some potentially large text, perhaps read from disk?";
    let spaces = bytecount::count(mytext.as_bytes(), b' ');
    ..
}
```

bytecount supports two features to make use of modern CPU's features to speed up counting considerably. To allow your
users to use them, add the following to your `Cargo.toml`:

```
[features]
avx-accel = ["bytecount/avx-accel"]
simd-accel = ["bytecount/simd-accel"]
```

Now your users can compile with SSE support (available on most modern x86_64 processors) using:

```
cargo build --release --features simd-accel
```

Or even with AVX support (which likely requires compiling for the native target CPU):

```
RUSTFLAGS="-C target-cpu=native" cargo build --release --features "simd-accel avx-accel"
```

The algorithm is explained in depth
[here](https://llogiq.github.io/2016/09/27/count.html).

Note that for very short slices, the data parallelism will likely not win much performance gains. In those cases, a naive
count with a 32-bit counter may be a superior solution, unless counting *really* large byte slices.

## License

Licensed under either of at your discretion:

- [Apache 2.0](LICENSE.Apache2)
- [MIT](LICENSE.MIT)
