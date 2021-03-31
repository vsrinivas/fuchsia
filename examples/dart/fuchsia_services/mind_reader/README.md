# Mind Reader
The mind reader example consists of two components, a client and a server.
The example demonstrates a few concepts which are important for understanding
how components interact with each other.
- [Launching a component](#Launching-a-component)
- [Exposing a public service](#Exposing-a-public-service)
- [Exposing a service to a launched component](#Exposing-a-Service-to-a-Launched-Component)

## Building and Running
To build the example first make sure the package is set to build
`fx set workstation.x64 --with //examples/dart/fuchsia_services/mind_reader/bin:mind-reader-dart`

You can now build the example
`fx build`

Ensure that you have the package server running
`fx serve`

In a new window start the logger
`fx log`

Optionally, you can filter your logs to only include those that are emitted from
the relevant process and control verbosity using --verbosity 4 (where 4 is verbose)
`fx log --tag mind_reader_client`

Launch the client (note: if you are not running this on the host but rather on the
device directly you can omit the `fx shell` part of the command)
`fx shell run "fuchsia-pkg://fuchsia.com/mind-reader-dart#meta/mind_reader_client.cmx"`

Now try running the command again but provide a secret thought to expose to the
child process (note: if you are invoking this command with `fx shell` you must
escape the quotes to ensure that the parser treats the argument as one string)
`fx shell run "fuchsia-pkg://fuchsia.com/mind-reader-dart#meta/mind_reader_client.cmx" --thought "my secret thought"`

## Concepts
### Launching a Component
The example shows how components can use the `fuchsia.sys.Launcher` to launch
a new component and optionally control its lifecycle.

### Exposing a Public Service
When the server is launched it exposes the `MindReader` service as an outgoing
service. This process registers the service in the server's  `out/public` directory. The
client component has a handle to this directory and thus can connect to the
service.

### Exposing a Service to a Launched Component
The client exposes the `ThoughtLeaker` service to the server component which
is launched. This service is made available to the server via its `in/svc` directory.
The server can connect to this service to read the clients thoughts.
