# Regulatory Region Service

Reviewed on: 2019-09-27

The Regulatory Region Service provides the ability to _set_ the regulatory region for a device.

## Building

* This project can be added to builds by including `--with
  //src/connectivity/location/regulatory_region` in the `fx set` invocation.
* To include tests, add `--with //src/connectivity/location/regulatory_region:tests`.

## Running

The Regulatory Region Service provides the `fuchsia.location.named_place.RegulatoryRegionConfigurator` service.

## Testing

* This component does not have any logic, so it doesn't have unit tests.
* Integration tests can be run with `fx run-test regulatory_region_integration_test`.

## Source layout

* The entrypoint is located in `src/main.rs`.
* Integration tests are located in `tests/`.

## Editing

If you're editing the code with an IDE, your IDE's Rust integration will
probably want a `Cargo.toml` file describing the source and its dependencies. You can
generate the necessary Cargo file with `fx gen-cargo //src/connectivity/location/regulatory_region:bin`.