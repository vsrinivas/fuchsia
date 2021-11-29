1 &lt;&lt; iota
===============

[<img alt="github" src="https://img.shields.io/badge/github-dtolnay/iota-8da0cb?style=for-the-badge&labelColor=555555&logo=github" height="20">](https://github.com/dtolnay/iota)
[<img alt="crates.io" src="https://img.shields.io/crates/v/iota.svg?style=for-the-badge&color=fc8d62&logo=rust" height="20">](https://crates.io/crates/iota)
[<img alt="docs.rs" src="https://img.shields.io/badge/docs.rs-iota-66c2a5?style=for-the-badge&labelColor=555555&logoColor=white&logo=data:image/svg+xml;base64,PHN2ZyByb2xlPSJpbWciIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgdmlld0JveD0iMCAwIDUxMiA1MTIiPjxwYXRoIGZpbGw9IiNmNWY1ZjUiIGQ9Ik00ODguNiAyNTAuMkwzOTIgMjE0VjEwNS41YzAtMTUtOS4zLTI4LjQtMjMuNC0zMy43bC0xMDAtMzcuNWMtOC4xLTMuMS0xNy4xLTMuMS0yNS4zIDBsLTEwMCAzNy41Yy0xNC4xIDUuMy0yMy40IDE4LjctMjMuNCAzMy43VjIxNGwtOTYuNiAzNi4yQzkuMyAyNTUuNSAwIDI2OC45IDAgMjgzLjlWMzk0YzAgMTMuNiA3LjcgMjYuMSAxOS45IDMyLjJsMTAwIDUwYzEwLjEgNS4xIDIyLjEgNS4xIDMyLjIgMGwxMDMuOS01MiAxMDMuOSA1MmMxMC4xIDUuMSAyMi4xIDUuMSAzMi4yIDBsMTAwLTUwYzEyLjItNi4xIDE5LjktMTguNiAxOS45LTMyLjJWMjgzLjljMC0xNS05LjMtMjguNC0yMy40LTMzLjd6TTM1OCAyMTQuOGwtODUgMzEuOXYtNjguMmw4NS0zN3Y3My4zek0xNTQgMTA0LjFsMTAyLTM4LjIgMTAyIDM4LjJ2LjZsLTEwMiA0MS40LTEwMi00MS40di0uNnptODQgMjkxLjFsLTg1IDQyLjV2LTc5LjFsODUtMzguOHY3NS40em0wLTExMmwtMTAyIDQxLjQtMTAyLTQxLjR2LS42bDEwMi0zOC4yIDEwMiAzOC4ydi42em0yNDAgMTEybC04NSA0Mi41di03OS4xbDg1LTM4Ljh2NzUuNHptMC0xMTJsLTEwMiA0MS40LTEwMi00MS40di0uNmwxMDItMzguMiAxMDIgMzguMnYuNnoiPjwvcGF0aD48L3N2Zz4K" height="20">](https://docs.rs/iota)
[<img alt="build status" src="https://img.shields.io/github/workflow/status/dtolnay/iota/CI/master?style=for-the-badge" height="20">](https://github.com/dtolnay/iota/actions?query=branch%3Amaster)

The `iota!` macro constructs a set of related constants.

```toml
[dependencies]
iota = "0.2"
```

```rust
use iota::iota;

iota! {
    const A: u8 = 1 << iota;
        , B
        , C
        , D
}

fn main() {
    assert_eq!(A, 1);
    assert_eq!(B, 2);
    assert_eq!(C, 4);
    assert_eq!(D, 8);
}
```

Within an `iota!` block, the `iota` variable is an untyped integer constant
whose value begins at 0 and increments by 1 for every constant declared in
the block.

```rust
use iota::iota;

iota! {
    const A: u8 = 1 << iota;
        , B

    const C: i32 = -1; // iota is not used but still incremented

    pub const D: u8 = iota * 2;
        , E
        , F
}

// `iota` begins again from 0 in this block
iota! {
    const G: usize = 1 << (iota + 10);
        , H
}

fn main() {
    assert_eq!(A, 1 << 0);
    assert_eq!(B, 1 << 1);

    assert_eq!(C, -1);

    assert_eq!(D, 3 * 2);
    assert_eq!(E, 4 * 2);
    assert_eq!(F, 5 * 2);

    assert_eq!(G, 1 << (0 + 10));
    assert_eq!(H, 1 << (1 + 10));
}
```

<br>

#### License

<sup>
Licensed under either of <a href="LICENSE-APACHE">Apache License, Version
2.0</a> or <a href="LICENSE-MIT">MIT license</a> at your option.
</sup>

<br>

<sub>
Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in this crate by you, as defined in the Apache-2.0 license, shall
be dual licensed as above, without any additional terms or conditions.
</sub>
