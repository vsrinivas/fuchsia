# Echo realm

This directory contains the common "realm" component used by the FIDL examples
to launch client and server components and route capabilities using
[Component Framework](/docs/concepts/components/introduction.md).

The realm component encapsulates both the server and client components and
performs the necessary capability routing of FIDL protocols between them.
This enables the examples to be created and executed as a single unit
within the [component topology](/docs/concepts/components/v2/topology.md).

For more details on how this component is used in the the FIDL examples,
see `//examples/fidl/README.md`.
