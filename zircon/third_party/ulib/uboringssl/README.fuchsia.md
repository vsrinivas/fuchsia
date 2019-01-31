uboringssl
=======================================

uboringssl is a subset of [BoringSSL]'s libcrypto.  The source
code under this directory comprises a minimal set needed for selected
cryptographic operations in the kernel.  The code itself is unchanged
and matches this [revision].

## Updating

Use the [roll_boringssl.go] script in Fuchsia's BoringSSL [package].

## License

All code under this directory is covered by the same [license] as BoringSSL.

[BoringSSL]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/README.md
[package]: https://fuchsia.googlesource.com/garnet/+/master/packages/boringssl
[license]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/LICENSE

[//]: # (UPDATE THE DIGEST WHEN ROLLING BORINGSSL)
[revision]: https://fuchsia.googlesource.com/third_party/boringssl/+/4b968339e3ced2d498f4182cd725243bb6cca81b/
