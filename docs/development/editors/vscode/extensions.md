{% include "docs/development/editors/vscode/_common/_vscode_header.md" %}

# Installing extensions

The following VS Code extensions may provide a productive development environment for Fuchsia:

## Fuchsia-specific extensions

Fuchsia-specific extensions provide support with custom Fuchsia files.

### FIDL Language Support

[FIDL Language Support](https://marketplace.visualstudio.com/items?itemName=fuchsia-authors.language-fidl){: .external}
provides syntax support and LSP-based language features in [FIDL][fidl].

Note: You need to configure Fuchsia environment variables to run this extension. For more information, see [Set up environment variables][set-up-env].

<img class="vscode-image vscode-image-center"
     alt="This figure shows syntax highlighting for FIDL files in VS Code."
     src="images/extensions/fidl-pack.png"/>

### Fuchsia.git Helper

[Fuchsia.git Helper](https://marketplace.visualstudio.com/items?itemName=jwing.fuchsia-git-helper){: .external}
adds an "Open in...", which allows you to open a file in OSS Code Search.

To use this extension:

1. Right click a file in the file editor.
1. Select **Open in OSS Code Search**.

<img class="vscode-image vscode-image-center"
     alt="This figure shows the VS Code menu to open a file in OSS code search."
     src="images/extensions/fuchsia-git-helper.png"/>

### FuchsiAware

[FuchsiAware](https://marketplace.visualstudio.com/items?itemName=RichKadel.fuchsiaware){: .external}
assists with browsing Fuchsia artifacts, such as by linking from component URLs to component manifests.

<img class="vscode-image vscode-image-center"
     alt="This figure shows hyperlinks to fuchsia-pkg urls in VS Code."
     src="images/extensions/fuchsiaware.png"/>

## General workflow extensions

General workflow extensions provide an overall productive workflow when working with Fuchsia.

### GitLens

[GitLens](https://marketplace.visualstudio.com/items?itemName=eamodio.gitlens){: .external}
provides highly customizable insights of git history, which allows you to see code evolution.

<img class="vscode-image vscode-image-center"
     alt="This figure shows an overlay of git commit history in VS Code."
     src="images/extensions/gitlens.png"/>

### GN

[GN](https://marketplace.visualstudio.com/items?itemName=npclaudiu.vscode-gn){: .external}
adds syntax highlighting for GN files.

<img class="vscode-image vscode-image-center"
     alt="This figure shows syntax highlighting for GN files in VS Code."
     src="images/extensions/gn.png"/>

### GNFormat

[GNFormat](https://marketplace.visualstudio.com/items?itemName=persidskiy.vscode-gnformat){: .external}
provides GN file formatting.

You may need to configure GNFormat with the file path to your GN binary.
Do the following:

1. In VS Code, launch **Quick Open** by running `CMD/CTRL + P`.
1. Type `settings` in the search field.
1. Click **Preferences: Open Settings (JSON)**.
1. Add the following configuration: `”gnformat.path”: “<FILE_PATH>”` and restart VS Code.

### JSON5

[JSON5](https://marketplace.visualstudio.com/items?itemName=mrmlnc.vscode-json5){: .external}
adds syntax highlighting for JSON5 files.

<img class="vscode-image vscode-image-center"
     alt="This figure shows syntax highlighting for JSON5 files in VS Code."
     src="images/extensions/json5.png"/>

### C/C++

[C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools){: .external}
provides language support for C/C++ files, including features such as IntelliSense and debugging.

<img class="vscode-image vscode-image-center"
     alt="This figure shows the C/C++ language support and IntelliSense in VS Code."
     src="images/extensions/c-cpp.png"/>

<!-- Reference links -->

[set-up-env]: /docs/get-started/get_fuchsia_source.md#set-up-environment-variables
[fidl]: /docs/development/languages/fidl/README.md