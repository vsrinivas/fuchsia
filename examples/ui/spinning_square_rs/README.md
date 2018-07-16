# Rust Spinning Square

This crate is a first cut at an Scenic app written in Rust. It is losely based
on //garnet/examples/ui/spinning_square_view.

## Getting Started

1. Check out and build at least the Garnet level of Fuchsia.
1. Pave and boot your build of Fuchsia.
1. If the layer you selected was Topaz, the `Tiles` program is already
running. If not, start it with `run tiles_ctl -d`.
1. Launch the Rust spinning square with `tiles_ctl add spinning_square_rs_pkg`. You can launch
as many copies as you like, as well as `tiles_ctl add spinning_square_view` to compare Rust
and C++.

## Hacking

If you like to hack on this program there is a faster development cycle one can use after
the first build.

As a one-time setup step, run:

    ./scripts/fx gen-cargo //garnet/examples/ui/spinning_square_rs

This step creates a symbolic link from the source directory to the generated Cargo.toml
file found on the out directory. After that, one can use:

    fargo run --release --run-with-tiles

to build, scp to device and tiles_ctl add the spinning square binary.

One can build and browse the documentation for all the crates used by this program
with the following command:

    fargo doc --open

## To Do

Currently input events are not wired up. The steps to do so would be to use the way
the `ViewListenerRequestStream` is monitored and set up the same thing for input services
as described in [//garnet/public/fidl/fuchsia.ui.views_v1/views.fidl](https://fuchsia.googlesource.com/garnet/+/master/public/fidl/fuchsia.ui.views_v1/views.fidl#36).

So much use of `Arc<Mutex<>>` is unfortunate. It would be great to find a way where it is not needed.

Integrating the approach used by Raph's [xi-win](https://github.com/google/xi-win/tree/master/xi-win-ui) for translating widgets to scenic
commands is worth trying.
