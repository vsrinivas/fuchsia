[![Build Status](https://travis-ci.org/Stebalien/snowflake.svg?branch=master)](https://travis-ci.org/Stebalien/snowflake)

A crate for quickly generating unique IDs with guaranteed properties. This
library is completely unrelated to twitter's snowflake library.

This crate currently includes guaranteed process unique IDs but may include new
ID types in the future.

API Docs: https://stebalien.github.io/snowflake/snowflake/

## Usage

Add this to your Cargo.toml:

```toml
[dependencies]
snowflake = "1.2"
```

and this to your create root:

```rust
extern create snowflake;
```

To add support for serialization and deserialization using
[serde](https://serde.rs/), add this to your Cargo.toml:

```toml
[dependences]
snowflake = { version = "1.2", features = ["serde_support"] }
```

Warning: there is a risk of non-unique IDs if (de)serialization is used to
persist IDs, i.e. reading and writing IDs to and from a file.

## License

Licensed under either of

 * Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall
be dual licensed as above, without any additional terms or conditions.
