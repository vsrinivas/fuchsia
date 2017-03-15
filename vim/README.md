# Helpful VIM tools for Fuchsia development

## Features

* Configure YouCompleteMe to provide error checking, completion and source navigation within the Fuchsia tree.
* Set path so that `:find` and `gf` know how to find files.
* Fidl syntax highlighting (using /lib/fidl/tools/vim/).

## Installation

1. Update your login script

   Steps #2 and #3 depend on variables set in `env.sh` and by the `fset`
   command. Add these lines to your startup script (typically `~/.bashrc`).

   ```
   source /path-to-fuchsia-dir/scripts/env.sh
   fset x86-64
   ```

1. Update your vim startup file

   Add these lines to `~/.vimrc`:

   ```
   if $FUCHSIA_DIR != ""
     source $FUCHSIA_DIR/scripts/vim/fuchsia.vim
   endif
   ```

1. Install YouCompleteMe (ycm)

   Optionally install [YouCompleteMe](https://github.com/Valloric/YouCompleteMe)
   for fancy completion, source navigation and inline errors.  See the
   [installation guide](https://github.com/Valloric/YouCompleteMe#installation).

   Google users can install YCM by adding these two lines to `.vimrc`:

   ```
   source /usr/share/vim/google/google.vim
   Glug youcompleteme-google
   ```

## See also

For Magenta YouCompleteMe integration:
https://fuchsia.googlesource.com/magenta/+/master/docs/editors.md

## TODO

In the future it would be nice to support:
* Fidl indentation
* GN highlighting and indentation
* Dart, Go and Rust support
* Build system integration
* Navigate between generated files and fidl source
