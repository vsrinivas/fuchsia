The [Remote Control Service](rcs.md) offers an API for querying and connecting
to arbitrary FIDL services on the target.

Queries can match an arbitrary number of services on the system, and `select`
will output all of the matches, formatted according to their place in the
component topology.

In the special case of a query that uniquely matches a single service, RCS can
connect to that service and pass a handle to it to the host for FFX to use.
This is how the the [plugin system](proxy-plugin.md) is able to create FIDL
proxies using the component selector mapping.

To query for services on a target, write a
[selector](https://fuchsia.dev/reference/fidl/fuchsia.diagnostics#Selector) to
match the service(s) of interest.
