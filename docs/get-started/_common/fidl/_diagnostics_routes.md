## Verifying capability routes

The `ffx scrutiny` utility provides a host of features for auditing system
security, including an audit of the capability routes in the static component
topology. The `verify routes` subcommand discovers and reports routing errors,
which can help you find any missing `offer` or `expose` declarations in your
manifest.

```posix-terminal
ffx scrutiny verify routes
```

This enables you to perform initial validation of your declarations before you
even run the components!

```none {:.devsite-disable-click-to-copy}
[
  {
    "capability_type": "protocol",
    "results": {
      "errors": [
        {
          "capability": "fidl.examples.echo.Echo",
          "error": "no offer declaration for `/core` with name `fidl.examples.echo.Echo`",
          "using_node": "/core/echo_client"
        }
      ]
    }
  }
]
```
