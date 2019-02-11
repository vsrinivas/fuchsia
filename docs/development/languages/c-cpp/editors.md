# Editor/IDE Setup

[TOC]

## CLion

Follow either the **CMake** (recommended) or **Compilation Database**
instructions below to create the appropriate project description file in
the `fuchsia` directory.

Then in CLion choose *Import Project from Sources* and select the
`fuchsia` directory.

### Performance

To improve performance you probably want to exclude directories you
are not working with. You can do that in the Project View by
right-clicking each directory and choosing
*Mark directory as->Excluded*.

## VIM

See [Helpful Vim tools for Fuchsia development](https://fuchsia.googlesource.com/scripts/+/master/vim/README.md).

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

Finally, follow the **Compilation Database** instructions below to
generate the `compile_commands.json` in the fuchsia directory. Then
reload vscode to enjoy the results.

### default vscode C++ extension

Install the default [C/C++ extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
suggested by vscode.

You can use [tasks](https://code.visualstudio.com/docs/editor/tasks) to
configure a compilation step.

## Project Description Files

There are two ways of describing a project's source files that
can be used with fuchsia - *CMake* or *Compilation Database*. They are
described below.

Note these approaches are only intended to help the IDE find and parse
the source files. Building should still be done with `fx full-build`.

## CMake

The [fuchsia.cmake](./fuchsia.cmake) file located in this directory can
be used with IDEs that support CMake to include most of the fuchsia
source files.

To use, create a CMakeLists.txt file in the top-level fuchsia
directory with the following contents. Then use it normally with your
IDE.

    cmake_minimum_required(VERSION 3.9)
    include(${PROJECT_SOURCE_DIR}/docs/development/languages/c-cpp/fuchsia.cmake)

## Compilation Database (fx compdb)

A [Compilation
Database](https://clang.llvm.org/docs/JSONCompilationDatabase.html) file
can be generated using `fx compdb`. This will create/update the file
`compile_commands.json` in the `fuchsia` directory.

When you add, delete, or rename source files the command needs to be
rerun to update the `compile_commands.json` file.
