uboringssl
=======================================

uboringssl is a subset of [BoringSSL]'s libcrypto.  The source
code under this directory comprises a minimal set needed for selected
cryptographic operations in the kernel.

## Changes

Changes from the upstream files are limited to a single files:
  * [base.h]: BORINGSSL_NO_CXX and OPENSSL_NO_THREADS added to Fuchsia case.

All other code is unchanged from BoringSSL.

## Updating

Use the [roll_boringssl.go] script in Fuchsia's BoringSSL [package].

## License

All code under this directory is covered by the same [license] as BoringSSL.

[BoringSSL]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/README.md
[base.h]: include/openssl/base.h
[package]: https://fuchsia.googlesource.com/garnet/+/master/packages/boringssl
[license]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/LICENSE

[//]: # (UPDATE THE DIGEST WHEN ROLLING BORINGSSL)
[revision]: https://fuchsia.googlesource.com/third_party/boringssl/+/9c3b120b618f3678a807d693b2e6f331aaa54605/
