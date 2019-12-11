# Helpful Vim tools for Fuchsia development

## Features

* Configure [YouCompleteMe](youcompleteme.md) to provide error checking,
  code completion, and source navigation within the Fuchsia tree.
* Set path so that `:find` and `gf` know how to find files.
* Fidl syntax highlighting (using /lib/fidl/tools/vim/).
* Basic build system integration so that `:make` builds and populates the
  QuickFix window.

## Installation

1. Update your login script:

   Steps #2 and #3 depend on configuration set by the `fx set` command. Add
   these lines to your startup script (typically `~/.bashrc`).

   ```shell
   export FUCHSIA_DIR=/path/to/fuchsia-dir
   fx set core.x64
   ```

1. Update your Vim startup file:

   If this line exists in your `~/.vimrc file`, remove it:

   ```
   filetype plugin indent on
   ```

   Then add these lines to your `~/.vimrc`.

   ```
   if $FUCHSIA_DIR != ""
     source $FUCHSIA_DIR/scripts/vim/fuchsia.vim
   endif
   filetype plugin indent on
   ```

1. Install YouCompleteMe (YCM):

   Optionally [install YouCompleteMe](youcompleteme.md)
   for fancy completion, source navigation and inline errors.

   If it's installed, `fuchsia.vim` configures YCM properly.

   If everything is working properly, you can place the cursor on an
   identifier in a .cc or .h file then hit Ctrl+], to navigate
   to the definition of the identifier.

   Use `fx compdb` to build a compilation database. YCM will use the
   compilation database which is more reliable and efficient than
   the default `ycm_extra_config.py` configuration.

## TODO

In the future it would be nice to support:
* Fidl indentation
* GN indentation
* Dart, Go and Rust support
* Navigate between generated files and fidl source
