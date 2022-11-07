
# deepsize
![](https://img.shields.io/crates/v/deepsize.svg) [![](https://img.shields.io/badge/docs-deepsize-blue.svg)](https://docs.rs/deepsize)

A trait and derive macro to recursively find the size of an object
and the size of allocations that it owns.

This should work in `#[no_std]` environments, but requires the `alloc` crate.

## Ownership and Reference Counting

`DeepSizeOf` counts all memory considered "owned" by the structure
that it is finding the size of.  Structures behind `&` and `&mut`
references are not counted towards the total size of the structure;
however, uniquely owned structures such as `Box` and `Vec` are.

Reference counted pointers (`Arc`, and `Rc`) are counted the first
time that they appear, and are tracked to prevent them from being
counted multiple times.  The `Weak` variants of each are treated like
references, and are not counted.

## Features

* `std` (enabled by default): Adds implementations of `DeepSizeOf`
  for types only found in `std` such as `HashMap` and `Mutex`.
* `derive` (enabled by default): Adds support for a derive macro for
  `DeepSizeOf`.

`deepsize` also has optional support for these external crates:

* `slotmap`: (version 0.4)
* `slab`: (version 0.4)
* `indexmap`: (version 1)
* `arrayvec`: (version 0.5)
* `smallvec`: (version 1)
* `hashbrown`: (version 0.9)
* `chrono`: (version 0.4)

## Example Code

```rust
use deepsize::DeepSizeOf;

#[derive(DeepSizeOf)]
struct Test {
    a: u32,
    b: Box<[u8]>,
}

fn main() {
    let object = Test {
        a: 15,
        b: Box::new(b"Hello, Wold!"),
    };
    
    assert_eq!(object.deep_size_of(), size_of::<Test>() + size_of::<u8>() * 12);
}
```

