Ermine development shell for Fuchsia.

## Build

Use the following fx set command to build the shell:

```bash
fx set workstation.x64
```

## Ask Bar

The shell is quite rudimentary. It displays an Ask bar to allow the user to
type in the name of the component they wish to launch. Example: `terminal`

They can also type the full Fuchsia package URI of the component:

`fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx`

Clicking outside the Ask bar dismisses it. Pressing Esc key also dismisses it.
Use Alt+Space key combo to summon it back.


## session-control

`session-control` provides similar functionality as the Ask Bar, but from the command
line using `fx shell`:

```bash
fx shell session-control add <component URI>
```

where `component URI` is the URI of the component you are trying to launch.
