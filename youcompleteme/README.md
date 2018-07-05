# YouCompleteMe for Fuchsia Developers

You can use [YouCompleteMe](https://github.com/Valloric/YouCompleteMe) to
provide error checking, completion and source navigation within the Fuchsia
tree.

YouCompleteMe works natively with Vim but it can also be integrated
with other editors through [ycmd](https://github.com/Valloric/ycmd).

## Install

See the [installation guide](
https://github.com/Valloric/YouCompleteMe#installation).

**Note**: Installing YCM on MacOS with Homebrew is not recommended because
of library compatibility errors. Use the official installation guide instead.

### gLinux (Googlers only)

(compiling on gLinux, even if editing over SSHFS on MacOS) Ignore the above.
Search the Google intranet for "YouCompleteMe" for installation instructions.

## Configure

### Vim

The general [Vim Fuchsia instructions](
https://fuchsia.googlesource.com/scripts/+/master/vim/README.md) will do this
automatically.

### Other editors (ycmd)

You'll need to set the ycmd config option `global_ycm_extra_conf` to point to
`${FUCHSIA_DIR}/scripts/youcompleteme/ycm_extra_conf.py`.
Note you may need to manually replace `${FUCHSIA_DIR}` with the correct path.

Alternatively, you can create a `.ycm_extra_conf.py` symbolic link to let YCM
automatically find the config for any fuchsia repository:

```
ln -s $FUCHSIA_DIR/scripts/youcompleteme/ycm_extra_conf.py $FUCHSIA_DIR/.ycm_extra_conf.py
```

## See also

[Zircon editor integration](
https://fuchsia.googlesource.com/zircon/+/master/docs/editors.md)
