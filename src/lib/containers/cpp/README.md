# Specialized C++ Containers

## Scope

This directory containers some specialized data structures that are meant to
augment the C++ Standard Library. Do not add containers that solve the same
problems that a container from the Standard Library does. Do not add containers
that are useful in the kernel. Those should go in the FBL.

This library never produces I/O.

## Dependencies

Code in this library can only depend on the following sources:
- The C Standard Library.
- The C++ Standard Library.
- The Safemath Library.

No other dependencies are allowed.

## Supported Platforms

Code in this library should build both for host and target. Unit tests should
validate the behaviour on both host and target.

## Naming Convention

The rational is that if a container has STL-like behaviour, it should follow the
STL naming convention. That is, the name classes use under_score instead of the
PascalCase used in the rest of Fuchsia.

