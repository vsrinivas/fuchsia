# Fxfs Golden Images

This directory contains a collection of "golden" filesystem images that have
been compressed with zstd.

These images are used to ensure that the latest version of Fxfs can continue to
read filesystem images written by previous versions of the code.

The images themselves are generated with `fx fxfs create_golden` by developers
whenever the on-disk Fxfs version is changed. A unit test ensures this step is
not forgotten and provides instructions if it is.

## images.gni

This is an auto-generated file. It is rewritten by `fx fxfs create_golden` and includes
all images found in the output directory.

It is required because our build system (GN) doesn't support wildcard dependencies but
we need a way to ensure all images are included in tests run on CQ.

## House keeping

We have a hard requirement that an image exists for the *current* version of
Fxfs but beyond that, engineers are free to manually remove old images as they see fit.

