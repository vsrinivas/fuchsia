# Helpful VIM tools for Fuchsia development

## Features

* Configure YouCompleteMe to provide error checking, completion and source navigation within the Fuchsia tree.
* Set path so that `:find` and `gf` know how to find files.
* Fidl syntax highlighting (using /lib/fidl/tools/vim/).

## Installation

Make sure `env.sh` is being called in your login scripts. This code depends on variables set in `env.sh` and by the
`fset` command.

Add this to your `vimrc`:
```
if $FUCHSIA_DIR != ""
  source $FUCHSIA_DIR/scripts/vim/fuchsia.vim
endif
```

Optionally install [YouCompleteMe](https://github.com/Valloric/YouCompleteMe) for fancy completion, source navigation
and inline errors.

## See also

For Magenta YouCompleteMe integration: https://fuchsia.googlesource.com/magenta/+/master/docs/editors.md

## TODO

In the future it would be nice to support:
* Fidl indentation
* GN highlighting and indentation
* Dart, Go and Rust support
* Build system integration
* Navigate between generated files and fidl source
