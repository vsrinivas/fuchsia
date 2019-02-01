# Editor setup

[TOC]

## Visual Studio Code (vscode)

There are multiple vscode setups known to work to different degrees. The
sections below describe the different setups (pick one).

### clangd

Install
[vscode-clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).
Disable the default C/C++ extension if you have it installed.

In settings, add:

```
"clangd.path": "<absolute path to Fuchsia checkout>/buildtools/linux-x64/clang/bin/clangd",
```

*** note
Note that the path to clangd does need to be absolute.
***

Finally, make sure to generate compdb via `fx compdb` (or `fx compdb -z` if you
work in Zircon) and reload vscode to enjoy the results.

### default vscode C++ extension

Install the default [C/C++ extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
suggested by vscode.

You can use [tasks](https://code.visualstudio.com/docs/editor/tasks) to
configure a compilation step.