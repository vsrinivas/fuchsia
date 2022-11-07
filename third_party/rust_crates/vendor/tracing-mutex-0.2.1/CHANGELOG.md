# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.1] - 2022-05-23

### Added

- Build [docs.rs] documentation with all features enabled for completeness.
- Add support for `std::sync::Condvar`

### Fixed

- The `parkinglot` module is now correctly enabled by the `parkinglot` feature rather than the
  `lockapi` feature.

## [0.2.0] - 2022-05-07

### Added
- Generic support for wrapping mutexes that implement the traits provided by the
  [`lock_api`][lock_api] crate. This can be used for creating support for other mutex providers that
  implement it.

- Support for [`parking_lot`][parking_lot] mutexes. Support includes type aliases for all
  provided mutex types as well as a dedicated `Once` wrapper.

- Simple benchmark to track the rough performance penalty incurred by dependency tracking.

### Breaking

- The library now requires edition 2021.

- The `Mutex`- and `RwLockGuards` now dereference to `T` rather than the lock guard they wrap. This
  is technically a bugfix but can theoretically break existing code.

- Self-cycles are no longer allowed for lock dependencies. They previously were because it usually
  isn't a problem, but it can create RWR deadlocks with `RwLocks`.

### Changed

- The project now targets edition 2021

## [0.1.2] - 2021-05-27

### Added
- Added missing type aliases for the guards returned by `DebugMutex` and `DebugRwLock`. These new
  type aliases function the same as the ones they belong to, resolving to either the tracing
  versions when debug assertions are enabled or the standard one when they're not.

### Fixed
- Fixed a corruption error where deallocating a previously cyclic mutex could result in a panic.

## [0.1.1] - 2021-05-24

### Changed
- New data structure for interal dependency graph, resulting in quicker graph updates.

### Fixed
- Fixed an issue where internal graph ordering indices were exponential rather than sequential. This
  caused the available IDs to run out way more quickly than intended.

## [0.1.0] - 2021-05-16 [YANKED]

Initial release.

[Unreleased]: https://github.com/bertptrs/tracing-mutex/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/bertptrs/tracing-mutex/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/bertptrs/tracing-mutex/releases/tag/v0.1.0

[docs.rs]: https://docs.rs/tracing-mutex/latest/tracing_mutex/
[lock_api]: https://docs.rs/lock_api/
[parking_lot]: https://docs.rs/parking_lot/
