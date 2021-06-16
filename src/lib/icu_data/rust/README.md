# Rust ICU data loader library

## Introduction

This directory contains the Rust bindings for the ICU data loader library.  The library is made
available through a low-level binding crate `icu_data-sys` and the higher-level API in `icu_data`.

## Usage tips

The [unit tests](icu_data/src/lib.rs) in `icu_data` show how one can use the library.  There is only
one object, `Loader` made available.  The developer's task is to instantiate a `Loader` any time
they want to have the system load ICU data, and keep the `Loader` in scope until data are no longer
needed.

The underlying machinery will make sure that the ICU data are loaded before first use, and that they
are unloaded once there is no interest in keeping the data around.

```rust
#[cfg(test)]
mod tests {
    use icu_data;

    #[test]
    fn you_can_also_clone_loaders() {
        let _loader1 = icu_data::Loader::new().expect("loader is constructed with success");
        let _loader2 = icu_data::Loader::new().expect("loader is just fine with a second initialization");
        let _loader3 = _loader2.clone();
        // The ICU data will be unloaded once all loaders go out of scope.
    }
}
```

### Linking

See the [build file](icu_data/BUILD.gn) for details on how to link the library and data.  A caveat
is that all transitively used non-rust libraries have to be mentioned in the `non_rust_deps` stanza
of any target that uses the `icu_data` crate.

### Testing

To test on your target device, the following sequence of commands should work.  The example `fx set`
is for a Chromebook device, but changing to `x64` would work if your workflow is based on QEMU, for
example.

```bash
fx set workstation.chromebook-x64 --with=//src/lib/icu_data/rust:tests
fx build
fx serve
fx test src/lib/icu_data/rust:tests
```

Run tests like so:

```console
fx test icu_data_rust_tests
```

