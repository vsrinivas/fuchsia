# Hello Touch Colors Example

This directory contains an application which changes background color with every
user touch input. It uses root presenter for its implementation of
BaseView. It tracks input callbacks from Scenic and draws elements using
scenic::Material.

Note that this application can provide a fully intrumented trace from input to
vsync.

## USAGE

```shell
$ run fuchsia-pkg://fuchsia.com/hello_touch_colors#meta/hello_touch_colors.cmx
```
