# System Bootstrap Application

This directory contains Bootstrap, an application which is responsible
for setting up an environment which provides access to global system
services.

This application runs quite early in the Fuchsia boot process. See the
[boot sequence](https://fuchsia.googlesource.com/docs/+/master/boot_sequence.md)
for more information.

Bootstrap is designed to be fairly robust.  If any of the services
dies, they will be restarted automatically the next time an
application attempts to connect to that service.

By default, bootstrap reads configuration files from
`/system/data/bootstrap/apps.config` and
`/system/data/bootstrap/services.config`.

## CONFIGURATION

### Services

The bootstrap services configuration is a JSON file consisting of service
registrations.  Each entry in the "services" map consists of a service
name and the application URL which provides it.

    {
      "services": {
        "service-name-1": "file:///system/apps/app_without_args",
        "service-name-2": [
           "file:///system/apps/app_with_args", "arg1", "arg2", "arg3"
        ]
      }
    }

### App Loaders

The bootstrap loaders configuration is a JSON file consisting of application
loader mappings.  Each entry in the "loader" map consists of a URI scheme and
the application loader that can load from it.  The root loader, capable of
handling "file" URIs, is mapped by default.

    {
      "loaders": {
        "scheme-1": "file:///system/apps/loader_without_args",
        "scheme-2": [
           "file:///system/apps/loader_with_args", "arg1", "arg2", "arg3"
        ]
      }
    }

### Apps

The bootstrap apps configuration is a JSON file consisting of apps to run at
startup.  Each entry in the "apps" list consists of either an app URL or a list
of an app URL and its arguments.

    {
      "apps": [
        "file:///system/apps/app_without_args",
        [ "file:///system/apps/app_with_args", "arg1", "arg2", "arg3" ]
      ]
    }

## DIRECT USAGE (uncommon)

Bootstrap takes a single application to run within the newly created
environment.

    bootstrap [args...] <app url> <app args...>

Bootstrap can also be run without any initial apps for debugging purposes.

    bootstrap -

URLs can be specified in any of the following forms:

    file:///path/to/my_app : canonical url
    /path/to/my_app : absolute path
    my_app : partial name resolved in path

### Arguments

Register additional services by providing service name and application url to launch:

    --reg=<service1 name>@<app1 url>[,<service2 name>@<app2 url>...]

Use a different configuration file:

    --config=<filename>

Do not register services from config file (note that --reg-associate will not work);

    --no-config

Set environment label (default is "boot"):

    --label=<name>

Standard logging arguments:

    --verbose=[<level>]
    --quiet=[<level>]

### Examples

Run 'device_runner':

    bootstrap device_runner

Run 'launcher' passing 'noodles_view' as argument to it:

    bootstrap launch noodles_view

Run 'guide' providing two additional services in its environment:

    bootstrap --reg=hitchhike::Towel@file:///path/to/towel,hitchhike::HoopyFrood@file:///path/to/ford_prefect guide

Run 'my_app' with a custom configuration file:

    bootstrap --config=my.config my_app

Run 'my_app' without any other configured services:

    bootstrap --no-config --reg=my::Service@file:///path/to/my_server my_app
