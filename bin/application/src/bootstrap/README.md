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
        "service-name-1": "app_without_args",
        "service-name-2": [
           "app_with_args", "arg1", "arg2", "arg3"
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
        "scheme-1": "loader_without_args",
        "scheme-2": [
           "loader_with_args", "arg1", "arg2", "arg3"
        ]
      }
    }

### Apps

The bootstrap apps configuration is a JSON file consisting of apps to run at
startup.  Each entry in the "apps" list consists of either an app URL or a list
of an app URL and its arguments.

    {
      "apps": [
        "app_without_args",
        [ "app_with_args", "arg1", "arg2", "arg3" ]
      ]
    }
