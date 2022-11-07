# rand_pcg

[![Build Status](https://travis-ci.org/rust-random/small-rngs.svg?branch=master)](https://travis-ci.org/rust-random/small-rngs)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/rust-random/small-rngs?svg=true)](https://ci.appveyor.com/project/rust-random/small-rngs)
[![Latest version](https://img.shields.io/crates/v/rand_pcg.svg)](https://crates.io/crates/rand_pcg)
[![Documentation](https://docs.rs/rand_pcg/badge.svg)](https://docs.rs/rand_pcg)
[![Minimum rustc version](https://img.shields.io/badge/rustc-1.22+-yellow.svg)](https://github.com/rust-random/rand#rust-version-requirements)
[![License](https://img.shields.io/crates/l/rand_pcg.svg)](https://github.com/rust-random/small-rngs/tree/master/rand_pcg#license)

Implements a selection of PCG random number generators.

> PCG is a family of simple fast space-efficient statistically good algorithms
> for random number generation. [Melissa O'Neill, Harvey Mudd College, 2014].

The PCG algorithms are not suitable for cryptographic uses, but perform well
in statistical tests, use little memory and are fairly fast.
See the [pcg-random website](http://www.pcg-random.org/).

This crate depends on [rand_core](https://crates.io/crates/rand_core) and is
part of the [Rand project](https://github.com/rust-random/rand).

Documentation:
[master branch](https://rust-random.github.io/small-rngs/rand_pcg/index.html),
[by release](https://docs.rs/rand_pcg)

[Changelog](CHANGELOG.md)


## Crate Features

`rand_pcg` is `no_std` compatible. It does not require any functionality
outside of the `core` lib, thus there are no features to configure.

The `serde1` feature includes implementations of `Serialize` and `Deserialize`
for the included RNGs.


## License

`rand_pcg` is distributed under the terms of both the MIT license and the
Apache License (Version 2.0).

See [LICENSE-APACHE](LICENSE-APACHE) and [LICENSE-MIT](LICENSE-MIT), and
[COPYRIGHT](COPYRIGHT) for details.
