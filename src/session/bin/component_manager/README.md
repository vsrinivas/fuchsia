### Component Manager for Session Framework

This is a temporary workaround to allow command line tools to connect to the session manager
services so that the session manager can launch sessions.

Once the session manager no longer needs to run under a component manager launched
by `sysmgr`, and the session manager's services can be routed to command line tools using only
component manager routing primitives, this will no longer be needed.

### Concepts

The component manager is  started by `sysmgr` with session manager as the root component,
by specifying the following in a `sysmgr.config`:

```
"services": {
  "some service": [ "component manager url", "session manager url" ]
},
"startup_services": [
  "some service"
]
```

This exposes the service to other components run under `sysmgr` (e.g., a command line tool).

The session manager exposes the service with the following entry in its `.cml`.

```

"expose": [
  {
    "service_protocol": "some service",
    "from": "self",
  }
],
```

This component manager instance then looks for the service in the session manager's
`exec/expose/svc` directory and binds it to the component manager's `out/svc`.

A command line tool then specifies the following in its `.cml`:

```
"use": [
        {
           "service_protocol": "some service",
           "from": "realm",
        }
    ]
```

This request is now routed to this component manager via `sysmgr`, as specified in the
`sysmgr.config` outlined above.

### Building

The binary is included when you add the following to your `fx set`:

```
--with-base=//src/session:all
```