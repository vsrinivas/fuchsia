uboringssl
=======================================

uboringssl is a subset of [BoringSSL]'s libcrypto.  The source
code under this directory comprises a minimal set needed for selected
cryptographic operations in the kernel.

## Changes

Changes from the upstream files are limited to just three files:
  * [cpu-aarch64-zircon.cpp]: Stubs out ARM 64 capability detection. See [ZX-1357].
  * [err_data.c]: Auto-generated error data.
  * [base.h]: BORINGSSL_NO_CXX and OPENSSL_NO_THREADS added to Fuchsia case.

All other code is unchanged from BoringSSL.

## Updating

1. This subset does not include tests.  Before updating from Fuchsia's [BoringSSL], maintainers
should first ensure the unit tests in that [package] pass before updating the subset for Zircon.

2. A [check-boringssl.go] script recognizes these changes and will identify any other changes
introduce when updating BoringSSL.  Maintainers should port these differences until the script exits
cleanly.

3. Additionally, the changed files listed above should be manually inspected and copied or
edited as needed.  Care should be taken to minimize the differences between files in uboringssl and
BoringSSL.  Only the minimum number of changes necessary to limit pulling in excessive dependencies
should be added.

4. A [perlasm.sh] script produces the assembly files from the source tree's Perl scripts.
This script should be re-run after the source tree has been updated.

5. Finally, maintainers should update this file with the new [revision] of BoringSSL to
provide an easy way to confirm uboringssl was checked.

## License

All code under this directory is covered by the same [license] as BoringSSL.

[BoringSSL]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/README.md
[cpu-aarch64-zircon.cpp]: crypto/cpu-aarch64-zircon.cpp
[ZX-1357]: https://fuchsia.atlassian.net/browse/ZX-1357
[err_data.c]: crypto/err/err_data.c
[base.h]: include/openssl/base.h
[package]: https://fuchsia.googlesource.com/garnet/+/master/packages/boringssl
[check-boringssl.go]: scripts/check-boringssl.go
[perlasm.sh]: scripts/perlasm.sh
[license]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/LICENSE

[//]: # (UPDATE THE DIGEST WHEN ROLLING BORINGSSL)
[revision]: https://fuchsia.googlesource.com/third_party/boringssl/+/a62dbf88d8a3c04446db833a1eb80a620cb1514d/
