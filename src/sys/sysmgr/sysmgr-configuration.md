# Sysmgr configuration

## Example

In `myexample.config`:

```json
{
  "services": {
    "fuchsia.example.MyExample": "fuchsia-pkg://fuchsia.com/myexample#meta/myexample.cmx"
  }
}
```

In `BUILD.gn`:

```gn
config_data("myexample.config") {
  for_pkg = "sysmgr"
  sources = "myexample.config"
}
```

## Services

The sysmgr services configuration is a JSON file consisting of service
registrations.  Each entry in the "services" map consists of a service
name and the application URL which provides it.

```json
{
  "services": {
    "service-name-1": "app_without_args",
    "service-name-2": [
        "app_with_args", "arg1", "arg2", "arg3"
    ]
  }
}
```

## Apps

The sysmgr apps configuration is a JSON file consisting of apps to run at
startup.  Each entry in the "apps" list consists of either an app URL or a list
of an app URL and its arguments.

```json
{
  "apps": [
    "app_without_args",
    [ "app_with_args", "arg1", "arg2", "arg3" ]
  ]
}
```
