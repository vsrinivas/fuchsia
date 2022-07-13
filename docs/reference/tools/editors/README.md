# Editors for developing Fuchsia

Fuchsia recommends that you use an IDE (integrated development environment)
to develop Fuchsia and to build software on Fuchsia. An IDE consolidates
multiple tools in a single GUI to help you develop.

There are multiple ways to develop, but Fuchsia has developed multiple VS Code
extensions.

## VS Code {#vs-code}

To get started with VS Code, install [VS Code][vs-code-download]{: .external}.

### Configuration

Once you have installed VS Code, you should configure the IDE. Depending on
your development setup, consider the following guides:

* [Configuring remote workspaces][remote-workspaces]: This guide is recommended
  if you are developing on a virtual machine, container, or an environment with
  a running SSH server.
* [Configuring file reloading][file-reloading]: This guide is recommended if
  you are developing Fuchsia in the source tree. As Fuchsia has a large code
  base, you may want to exclude some directories from being watched for file
  changes.

### Extensions

VS Code supports a large amount of extensions which can help you customize
your IDE. Fuchsia has developed several extensions that are specific for
developing the Fuchsia platform and for developing on Fuchsia with the SDK.

* [Fuchsia developer][fuchsia-dev-ext]: This extension integrates key
  [ffx][ffx-ref] functionality into VS Code such as connecting, debugging,
  analyzing logs for Fuchsia devices, and functionality to help you edit and
  debug code as you develop for Fuchsia.
* [Additional Fuchsia extensions][fuchsia-source-ext]: This guide lists
  additional Fuchsia extensions that may help you as you contribute to
  Fuchsia.

[vs-code-download]: https://code.visualstudio.com/docs/setup/setup-overview
[remote-workspaces]: /docs/reference/tools/editors/vscode/remote-workspaces.md
[file-reloading]: /docs/reference/tools/editors/vscode/file-reloading.md
[sdk-fundamentals]: /docs/get-started/sdk/learn/README.md
[source-fundamentals]: /docs/get-started/learn/README.md
[fuchsia-dev-ext]: /docs/reference/tools/editors/vscode/fuchsia-ext-install.md
[ffx-ref]: https://fuchsia.dev/reference/tools/sdk/ffx
[fuchsia-source-ext]: /docs/reference/tools/editors/vscode/extensions.md