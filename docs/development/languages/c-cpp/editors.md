# C++ Editor/IDE Setup

[TOC]

## CLion

Follow either the **CMake** (recommended) or **Compilation Database**
instructions below to create the appropriate project description file in
the fuchsia root directory.

Then in CLion choose *Import Project from Sources* and select the
fuchsia root directory.

### CLion Performance Tweaks

To improve performance you can try some or all of the following. They
are only suggestions, we recommend checking with directly with JetBrains
at https://intellij-support.jetbrains.com/hc to be sure what works
best for your environment. Also see 

##### Exclude Directories

To speed up indexing time you can exclude directories you are not
working with. You can do that in the Project View by
right-clicking each directory and choosing
*Mark directory as->Excluded*. Note the affected configuration is stored
in `<project>/.idea/misc.xml`

See
[Control Source, Library, and Exclude Directories \- Help \| CLion](https://www.jetbrains.com/help/clion/controlling-source-library-and-exclude-directories.html)
for more information.

##### Unregister Git Repositories

The fuchsia source tree has a fair number of git repositories. Scanning
them can use CPU cycles for CLion. You can unregister the git
repositories you are not working on under
*File -> Settings -> Version Control*. They will still be listed there
so you can add them back later if needed.

##### Tune JVM Options and Platform Properties

See
[Tuning CLion \- Help \| CLion](https://www.jetbrains.com/help/clion/tuning-the-ide.html)
for general tips on tweaking CLion JVM Options and Platform Properties.
As that link suggests, contact CLion support for instructions
regarding the options and values that might help you with whatever issue
you are trying to solve  

## VIM

See [Helpful Vim tools for Fuchsia development](https://fuchsia.googlesource.com/fuchsia/+/master/scripts/vim/README.md).

## Visual Studio Code (vscode)

There are multiple vscode setups known to work to different degrees. The
sections below describe the different setups (pick one).

### clangd

Install
[vscode-clangd](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).
Disable the default C/C++ extension if you have it installed.

In settings, add:

```
"clangd.path": "<absolute path to fuchsia root directory>/buildtools/linux-x64/clang/bin/clangd",
```

*** note
Note that the path to clangd does need to be absolute.
***

Finally, follow the **Compilation Database** instructions below to
generate the `compile_commands.json` in the fuchsia root directory. Then
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

The [fuchsia.cmake](fuchsia.cmake) file located in this directory can
be used with IDEs that support CMake to include most of the fuchsia
source files.

To use, create a CMakeLists.txt file in the fuchsia root
directory with the following contents. Then use it normally with your
IDE.

    cmake_minimum_required(VERSION 3.9)
    include(${PROJECT_SOURCE_DIR}/docs/development/languages/c-cpp/fuchsia.cmake)

## Compilation Database (fx compdb)

A [Compilation
Database](https://clang.llvm.org/docs/JSONCompilationDatabase.html) file
can be generated using `fx compdb`. This will create/update the file
`compile_commands.json` in the fuchsia root directory.

When you add, delete, or rename source files the command needs to be
rerun to update the `compile_commands.json` file.
