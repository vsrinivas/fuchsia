# DNS Resolver for Security Package Delivery Tests

This component provides a fake `dns_resolver` implementation for security
package delivery tests. The implementation hosts the `fuchsia.net.name.Lookup`
protocol. It uses a shared library for selecting a hostname from the set of all
hostnames specified in `pkg-resolver` repository configurations to designate as
the local package update server, returning the IP address associated with
`localhost` on lookups for said hostname. The same library is used by the
components/tools that:

1. Prepare the URLs in the update package's `packages.json`;
1. Initiate an update, designating the update package URL.
