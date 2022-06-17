# Introduction

Carnelian is a light-weight user-interface framework for Fuchsia, written in Rust. It's name is inspired by
a mineral colored by impurities of iron oxide and was chosen during a period
when all Fuchsia names had to be gems.

Carnelian is designed to operate in Fuchsia configurations that are too limited to run Flutter,
such as the [Core](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/products/#core)
product. In that mode is used to provide the UI for the [factory data reset
application](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/recovery/system/).

Carnelian apps can also run on more full-featured products like
[Workstation](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/products/#workstation).
The [Terminal](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/ui/bin/terminal/)
application is a Carnelian app.

A single Carnelian application binary can run in either mode.

In order to provide this flexibility, Carnelian provides a 2D rendering model that is backed by
[Spinel](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/graphics/lib/compute/spinel/)
when Vulkan compute resources are available and
[Forma](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/graphics/lib/compute/forma/)
when they are not. It also provides certain services for the Core product, like reading from mouse
and keyboard, which are not otherwise available.
