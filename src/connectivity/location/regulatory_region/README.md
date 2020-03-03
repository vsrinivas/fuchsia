# Regulatory Region Service

Reviewed on: 2019-10-18

The Regulatory Region Service provides the abilities to
* set the regulatory region, and
* get updates to the value of the regulatory region.

## Building

* To add this project to your build, append `--with
  //src/connectivity/location/regulatory_region` in the `fx set` invocation.
* To include tests, add `--with //src/connectivity/location/regulatory_region:tests`.

## Running

The Regulatory Region Service provides the following protocols:
* `fuchsia.location.named_place.RegulatoryRegionConfigurator`
* `fuchsia.location.named_place.RegulatoryRegionWatcher`

## Testing

* Unit tests can be run with `fx run-test regulatory_region_tests`.
* Integration tests can be run with `fx run-test regulatory_region_integration_test`.

## Source layout

* The entrypoint is located in `src/main.rs`.
* Modules are enumerated in `src/lib.rs`.
* Unit tests are placed in the same file as the implementation code that they exercise.
* Integration tests are located in `tests/`.

## Editing

If you're editing the code with an IDE, your IDE's Rust integration will
probably want a `Cargo.toml` file describing the source and its dependencies. You can
generate the necessary Cargo file with `fx gen-cargo //src/connectivity/location/regulatory_region:bin`.