# System Bootstrap Application

This directory contains Bootstrap, an application which is responsible
for setting up an environment which provides access to global system
services.

This application is intended to be run very early in the Fuchsia boot
process before bringing up the graphical environment.  In a fully
integrated system you will not have need to run Bootstrap directly
because it will already be running.

Bootstrap is designed to be fairly robust.  If any of the services
dies, they will be restarted automatically the next time an
application attempts to connect to that service.

By default, bootstrap reads a configuration file from
"/system/data/bootstrap/services.config".

## USAGE

Bootstrap takes a single application to run within the newly created
environment.

    $ file:///system/apps/bootstrap [args...] <app url> <app args...>

### Arguments

Register additional services by providing service name and application url to launch:

    --reg=<service1 name>@<app1 url>[,<service2 name>@<app2 url>...]

Use a different configuration file:

    --config=<filename>

Do not register services from config file (note that --reg-associate will not work);

    --no-config

Standard logging arguments:

    --verbose=[<level>]
    --quiet=[<level>]

### Examples

Run 'device_runner':

    $ file:///system/apps/bootstrap file:///system/apps/device_runner

Run 'launcher' passing 'noodles_view' as argument to it:

    $ file:///system/apps/bootstrap file:///system/apps/launcher file:///system/apps/noodles_view

Run 'guide' providing two additional services in its environment:

    $ file:///system/apps/bootstrap --reg=hitchhike::Towel@file:///system/apps/towel,hitchhike::HoopyFrood@file:///system/apps/ford_prefect file:///system/apps/guide

Run 'my_app' with a custom configuration file:

    $ file:///system/apps/bootstrap --config=my.config file:///system/apps/my_app

Run 'my_app' without any other configured services:

    $ file:///system/apps/bootstrap --no-config --reg=my::Service@file:///system/apps/my_server file:///system/apps/my_app

## FUTURE WORK

Bootstrap only sets up one environment whereas it could perhaps set
up a tree of environments and launch applications within them according
to their privilege level and the isolation they require.
