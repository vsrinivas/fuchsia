## Example for fidl::WireSharedClient

This example demonstrates using the FIDL client designed to support moving
between and sharing by different threads in a multi-threaded environment.

It shows various ways the user could monitor teardown completion of the client
bindings, and schedule custom cleanup to run after that.
