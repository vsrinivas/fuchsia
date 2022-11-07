# **Humansize** ![travis badge](https://travis-ci.org/LeopoldArkham/humansize.svg?branch=master)

[Documentation](https://docs.rs/humansize/0.1.0/humansize/)


## Features


Humansize lets you easily represent file sizes in a human-friendly format.
You can specify your own formatting style or pick among the three defaults provided
by the library:

* Decimal (Multiples of 1000, `KB` units)
* Binary (Multiples of 1024, `KiB` units)
* Windows/Conventional (Multiples of 1024, `KB` units)

## How to use it

Cargo.Toml:
```
[dependencies]
humansize = "1.1.1"
```

Simply import the `FileSize` trait and the options module and call the
file_size method on any positive integer, using one of the three standards
provided by the options module.

```rust,no_run
extern crate humansize;
use humansize::{FileSize, file_size_opts as options};

fn main() {
	let size = 1000;
	println!("Size is {}", size.file_size(options::DECIMAL).unwrap());

	println!("Size is {}", size.file_size(options::BINARY).unwrap());

	println!("Size is {}", size.file_size(options::CONVENTIONAL).unwrap());
}
```

If you wish to customize the way sizes are displayed, you may create your own custom `FileSizeOpts` struct
and pass that to the method. See the `custom_options.rs` file in the example folder.


## License

This project is licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or
   http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or
   http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in humansize by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.