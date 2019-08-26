Ermine development shell for Fuchsia.

## Build

Use the following fx set command to build the shell:

```bash
fx set x64 --release --product=workstation
```

## Ask Bar

The shell is quite rudimentary. It displays an Ask bar to allow the user to
type in the URI of the component they wish to launch. It also supports http URLs
and, for queries that don't resolve to an installed component, it launches the
the search page using Chromium.

Clicking outside the Ask bar dismisses it. Pressing Esc key also dismisses it.
Use Alt+Space key combo to summon it back.

You component launched from the Ask bar opens up into a Story. Currently,
stories are displayed one per screen. You can page through stories using a swipe
gesture, but the gesture needs to be performed outside the story. Pressing the
[x] icon on top right removes the story.

## sessionctl

`sessionctl` provides similar functionality as the Ask Bar, but from the command
line using `fx shell`:

```bash
fx shell sessionctl add_mod <component URI>
```

where `component URI` is the URI of the component you are trying to launch.
