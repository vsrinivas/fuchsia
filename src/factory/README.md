# Factory

## factory_store_providers
Components that implement protocols in the fuchsia.factory library.

## fake_factory_items
A component that implements fuchsia.boot.FactoryItems and is used to write tests that depend on this
protocol without relying on the real implementation in Zircon.

## factoryctl
Command line tool to list and read factory files from fuchsia.factory APIs and raw data from
fuschsia.boot.FactoryItems.
