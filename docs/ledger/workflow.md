# Developer Workflow

Follow the general [Fuchsia documentation] to obtain a Fuchsia checkout, learn
how to build the code and how to run Fuchsia under QEMU and on Acer 12.

Be sure to configure:

 - [persistent file system]
 - [networking]

See [Testing](testing.md) for how to run Ledger tests locally. Upload the CL for
review using `git push origin HEAD:refs/for/master`.

[Fuchsia documentation]: https://fuchsia.googlesource.com/docs/+/master/README.md
[persistent file system]: https://fuchsia.googlesource.com/zircon/+/master/docs/minfs.md
[networking]: https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Enabling-Network
