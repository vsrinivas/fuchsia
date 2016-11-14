# Mozart Launcher Service Provider

This directory contains the Launcher, a service which binds view trees
to the virtual console.

The Launcher must be run within an environment that includes the
compositor and view manager.  If those services are not available,
they may need to be bootstrapped prior to running the Launcher.

See also the Launch program which is used to launch a view provider
application and ask the Launcher to display it.

## USAGE

This program should not be run directly.

Use file:///system/apps/launch instead.
