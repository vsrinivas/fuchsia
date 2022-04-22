# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.2...v0.1.2
[0.1.1]: https://github.com/bertptrs/tracing-mutex/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/bertptrs/tracing-mutex/releases/tag/v0.1.0
