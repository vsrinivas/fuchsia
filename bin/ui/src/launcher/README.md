# Mozart Launcher

This directory contains the Launcher, a simple tool for launching
applications within an environment which provides access to the
compositor, view manager, and related UI services.

If the launched application implements ViewProvider, then the launcher
asks the ViewProvider to create a view then adds it as the root of
a new view tree hosted within a new virtual console instance.

This application is primarily intended as scaffolding for running UI
applications while we build up the higher level parts of the system
which will eventually take over this function.

## USAGE

  $ file:///system/apps/launcher <app url> <app args>
