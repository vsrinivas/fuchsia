# Mozart Launcher

This directory contains the Launcher, a simple tool for running
UI applications which expose a ViewProvider.

The Launcher starts the specified application, requests its
ViewProvider service, creates a view, then sets the view as the
root of a new view tree hosted on a new virtual console instance.

If no application is specified, the Launcher simply offers its
|mozart.Launcher| service to the application which created it.

The Launcher must be run within an environment that includes the
compositor and view manager.  If those services are not available,
they may need to be bootstrapped prior to running the Launcher.

This tool is primarily intended as scaffolding for running Mozart
applications while we build up the higher level parts of the system
which will eventually take over this function.

## USAGE

Run an application which offers a ViewProvider service:

    $ file:///system/apps/launcher <app url> <app args>

When the environment does not already contain the compositor and
view manager, it may be necessary to bootstrap it:

    $ file:///system/apps/bootstrap file:///system/apps/launcher <app url> <app args>

### Arguments

Standard logging arguments:

    --verbose=[<level>]
    --quiet=[<level>]

### Examples

    $ file:///system/apps/launcher file:///system/apps/noodles_view
