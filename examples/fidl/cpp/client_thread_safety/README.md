## Example for fidl::SharedClient

This example demonstrates using the FIDL client designed to support moving
ownership between and sharing ownership by different threads in a multi-threaded
environment.

It shows various ways the user could monitor teardown completion of the client
bindings, and schedule custom cleanup to run after that.

See the [threading guide][threading-guide] for more details.

[threading-guide]: /docs/development/languages/fidl/tutorials/cpp/topics/threading.md
