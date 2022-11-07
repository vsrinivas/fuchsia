# Change Log
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased] - Unreleased

## [0.9.0] - 2018-07-30
### Added
- Add reflection to `Predicate`.
- Add support for predicates returning why they failed (`find_case`) which can
  be combined with the new `predicates-tree` crate.
- Split out `predicates-core` for reducing ecosystem breaking changes.

### Changed
- Predicates must also implement `PredicateReflection`

[Unreleased]: https://github.com/assert-rs/predicates-rs/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/assert-rs/predicates-rs/compare/v0.5.2...v0.9.0
