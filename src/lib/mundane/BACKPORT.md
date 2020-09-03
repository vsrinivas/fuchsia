# Instructions to Backport from Upstream

Mundane is maintained in its own repository. Backporting from [the upstream
repository][upstream] into Fuchsia is done manually. To backport, follow these
steps:

- Checkout the `master` branch from [the upstream][upstream] and ensure it is
  passing tests via `cargo test --all-features`.

- Change the copyright headers in source files from the Google copyright to the
  Fuchsia copyright. This can be done using a file containing the Fuchsia
  license via [`fd`] (or `find`) and `sed` from the repository root:

  ```shell
  fd . src/ -e rs -x sed -i '1,5d' {}
  fd . src/ -e rs -x sed -i -e '1rfuchsia-license.txt' -e '1{h;d}' -e '2{x;G}' {}
  ```

  Be mindful of files that may be missing the Google copyright in [the
  upstream][upstream] repository; the above use of `sed` blindly removes the
  first five lines of every file found by `fd`.

- Replace the `boringssl::ffi` module with `boringssl_sys`. This requires an
  `extern crate boringssl_sys` import in `lib.rs` and that references to
  `boringssl::ffi` are replaced with `boringssl_sys`. For unqualified references
  to the `ffi` module, it should be possible to create a binding via `use
  boringssl_sys as ffi`.

- Copy the `src` directory from the edited upstream into a Fuchsia checkout at
  `src/lib/mundane`.

- Format the code via `fx format-code`.

Note that it may be necessary to integrate with changes submitted to the Fuchsia
repository but not upstream. See [previous][ex1] [backport][ex2] commits for
some examples.

# Difference between Upstream and Fuchsia

See the diffs in `0001-Mundane-backport.patch`.

[upstream]: https://fuchsia.googlesource.com/mundane
[ex1]: https://fuchsia-review.googlesource.com/c/fuchsia/+/318507
[ex2]: https://fuchsia-review.googlesource.com/c/fuchsia/+/323813
[`fd`]: https://github.com/sharkdp/fd
