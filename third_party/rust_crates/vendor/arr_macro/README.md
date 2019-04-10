# Array Macro [![Build Status](https://api.travis-ci.org/JoshMcguigan/arr_macro.svg?branch=master)](https://travis-ci.org/JoshMcguigan/arr_macro) [![crates.io badge](https://img.shields.io/crates/v/arr_macro.svg)](https://crates.io/crates/arr_macro)

Array macro helps initialize arrays. It is useful when initializing large arrays (greater than 32 elements), or arrays of types which do not implement the copy or default traits. 

Array macro is implemented in 100% safe Rust.

For further background on the motivation behind this crate, check out [this blog post](https://www.joshmcguigan.com/blog/array-initialization-rust/).

## Usage

```rust
#![feature(proc_macro_hygiene)]

use arr_macro::arr;

fn main() {
    let x: [Option<String>; 3] = arr![None; 3];
    assert_eq!(
        [None, None, None],
        x
    );

    // works with all enum types (and impl copy is not required)
    #[allow(dead_code)]
    enum MyEnum {
        A,
        B
    }
    let _: [MyEnum; 33] = arr![MyEnum::A; 33];

    // Vec::new()
    let _: [Vec<String>; 33] = arr![Vec::new(); 33];

    // or your own struct type
    // and you can even use a counter to behave differently based on the array index
    #[derive(Debug)]
    struct MyStruct {
        member: u16,
    }
    impl MyStruct {
        fn new(member: u16) -> Self {
            MyStruct { member }
        }
    }
    let mut i = 0u16;
    let x: [MyStruct; 33] = arr![MyStruct::new({i += 1; i - 1}); 33];

    assert_eq!(0, x[0].member);
    assert_eq!(1, x[1].member);
    assert_eq!(2, x[2].member);
}
```

## License

Licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
