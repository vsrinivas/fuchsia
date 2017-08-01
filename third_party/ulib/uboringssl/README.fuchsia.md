uboringssl
=======================================

uboringssl is a subset of [BoringSSL's][boringssl] libcrypto.  The source
code under this directory comprises a minimal set needed for selected
cryptographic operations in the kernel.

## Changes

Changes to the upstream files are easily identifiable as falling into one of
three categories:
<ul>
<li>Code which should be disabled when in used in kernel has
<code>#ifdef _KERNEL</code> guards added.</li>
<li>Code which is added for Magenta is guarded by
<code>#ifdef \__Fuchsia\__</code>.</li>
<li>New source files are named with a *'-magenta.cpp'* suffix.</li>
</ul>

All other code is unchanged from BoringSSL.

## Updating

A [check-boringssl.go][script] script recognizes these changes and will
identify any other changes introduce when updating BoringSSL.  Maintainers
should port these differences until the script exits cleanly.

Care should be taken to minimize the differences between files in boring-crypto
and BoringSSL.  Only the minimum number of changes necessary to limit pulling in
excessive dependencies should be added.

This subset does not include tests from BoringSSL.  When updating from
[BoringSSL][boringssl], maintainers should first ensure the unit tests in that
package pass on Fuchsia before updating the subset for Magenta.

Finally, maintainers should update this file with the new [revision][revision]
of BoringSSL to provide an easy way to confirm boring-crypto was checked.

## License

All code under this directory is covered by the same [license][license] as
BoringSSL.

[boringssl]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/README.md "BoringSSL"
[script]: https://fuchsia.googlesource.com/magenta/+/master/third_party/boring-crypto/scripts/check-boringssl.go "check-boringssl.go"
[license]: https://fuchsia.googlesource.com/third_party/boringssl/+/master/LICENSE "BoringSSL license"

[//]: # (UPDATE THE DIGEST WHEN ROLLING BORINGSSL)
[revision]: https://fuchsia.googlesource.com/third_party/boringssl/+/27e377ec65d57589499c1dbabc6b74f6464f5d6d/
