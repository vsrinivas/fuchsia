# Introduction

Carnelian is a prototype framework for writing Fuchsia modules in Rust.

To build the included samples, use the fx set line below to build the
core version of Fuchsia with the necessary additional packages to run either
directly on the frame buffer or with scenic using the tiles tool. See
[these configuration](https://fuchsia.dev/fuchsia-src/getting_started#configure-and-build-fuchsia)
instructions for more details.

        fx set core.x64 \
        --with //src/lib/ui/carnelian:examples \
        --with //src/lib/ui/carnelian:carnelian-integration-test \
        --with //src/lib/ui/carnelian:carnelian-fb-integration-test \
        --with //src/lib/ui/carnelian:carnelian-tests \
        --with //garnet/bin/terminal:tiles_config \
        --with //src/ui \
        --with //src/ui/bin/root_presenter \
        --with //src/ui/bin/root_presenter:configs \
        --with //src/ui/scenic \
        --with //src/ui/tools/tiles \
        --release \
        --auto-dir \
        --args=rust_cap_lints='"warn"' \
        --cargo-toml-gen


# Tentative Roadmap

1. Flutter-style flex-box layout

## Flutter-style flex-box layout

Implement the basics of flex box layout, similar to the way it is done in
[Druid](https://docs.rs/druid/0.1.1/druid/).

# Future Areas

## Command Handling

Mature application frameworks usually have some mechanism for commands that might apply to
multiple items in the view hierarchy to be handled by the most specific first and proceeding
to less specific items. This command handling structure can also be used to show/enable menu
items if Fuchsia ever has such a menu.

## Animation

Design and implement a simple animation facility.

# Frequently Asked Questions
