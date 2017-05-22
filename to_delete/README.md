# NetConnector

NetConnector provides high-level connectivity services for Fuchsia-to-Fuchsia
communication. In its current manifestation, it serves to unblock developers
who want to implement distributed scenarios.

## NetConnector Service/Utility

The application `netconnector` runs either as the NetConnector service or as
a utility for managing the service. See [the README file](src/README.md) for
details.

## NetConnector Example App

The application `netconnector_example` runs either as an example requestor or
as an example responding service. See [the README file](examples/netconnector_example/README.md) for details.
