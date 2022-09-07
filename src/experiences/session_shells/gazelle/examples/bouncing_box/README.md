# Bouncing Box

An example app built using AppKit.

## Build:
```
fx set workstation_eng.<board> --with //src/experiences/session_shells/gazelle/examples
```

## Run
The following instructions will run bouncing_box under the elements-collections in Ermine:
```
$ ffx component create /core/session-manager/session:session/workstation_session/login_shell/ermine_shell/elements:bouncing_box fuchsia-pkg://fuchsia.com/bouncing_box#meta/bouncing_box.cm

$ ffx component start /core/session-manager/session:session/workstation_session/login_shell/ermine_shell/elements:bouncing_box
```