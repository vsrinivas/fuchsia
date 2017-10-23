# Zsh Completion

Partial Zsh completion support for the `fx` tool.

## Use

Add `//scripts/zsh-completion/` to your `fpath` before running `compinit`. For
example:
```
fpath+=( ~/fuchsia/scripts/zsh-completion )
```

## Improve

Subcommands are completed by looking in `//scripts/devshell/` but there isn't
completion for most subcommand arguments. To add completion for `fx foo` write a
new autoload function in `//scripts/zsh-completion/_fx_foo`. It will be called
by the `_fx` completion function when needed. The `${fuchsia_dir}` and
`${fuchsia_build_dir}` local variables will be available to the subcommand
completion function.
