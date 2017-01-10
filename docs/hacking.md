# Notes for hacking on Magenta

This file contains a random collection of notes for hacking on Magenta.

## Regenerating syscall support

Syscall support is generated from system/public/magenta/syscalls.sysgen.
Generally we don't want to willy-nilly add syscalls of course.
But if, perchance, you need to regenerate the syscall support files follow
these instructions.

Generally it's best to regenerate the files in a clean client.
That way `git status` will tell you what files have been changed.
Regenerating the files is easy enough:

```
$ scripts/run-sysgen.sh
```

## Terminal navigation and keyboard shortcuts

* Alt+Tab switches between VTs
* Alt+F{1,2,...} switches directly to a VT
* Alt+Up/Down scrolls up and down by lines
* Shift+PgUp/PgDown scrolls up and down by half page
* Ctrl+Alt+Delete reboots
