# Session Framework Configuration

The Session Framework can be configured to launch a specific session at build time. The
`session_config()` GN template gives the Session Framework access to the configuration file
set in the `config` parameter.  Including this rule in a product definition will automatically
launch Session Framework with a root session, defined as the `session_url` in the json configuration.

## Example configuration file

```
{
  "session_url": "fuchsia-pkg://fuchsia.com/your_session#meta/your_session.cm"
}
```

## Example BUILD.gn

```
import("//src/session/build/session_config.gni")

session_config("your_session_config") {
  config = "path/to/config.json"
}

group("product") {
  public_deps = [
    ":your_session_config",
    // other dependencies
  ]
}
```