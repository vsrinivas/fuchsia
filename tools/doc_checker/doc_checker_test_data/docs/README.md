# This is the README for the test docs data

It is used to serve as the root for the graph of docs.

Some of the files in this directory intentionally contains errors.

If there are specific things to test, feel free to add them.

This is a missing file [does not exist](/docs/missing.md)

This link should be a [path](https://fuchsia.dev/fuchsia-src/path.md), not a link to fuchsia.dev
This is the correct way to [path](/docs/path.md), not a link to fuchsia.dev or  [path](path.md), locally.

Link to fuchsia source

This link goes to the experiences project [experiences](https://fuchsia.googlesource.com/experiences/+/refs/heads/main/README.md)

This link is OK, just missing This link should be a [path](https://fuchsia.dev/missing/path.md), not a link to fuchsia.dev

This link goes to the old garnet project, which is an error [garnet](https://fuchsia.googlesource.com/garnet/+/refs/heads/main/README.md)

A link that is relative, but goes past the root [great-grand-parent-link](../../README.md)

A weird relative, but legal link: [up-and-down](../docs/README.md)

Mailto links should be OK
mail to [someone](mailto:someone@google.com)