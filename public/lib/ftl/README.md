# Fuchsia Template Library

A library containing basic C++ building blocks, such as logging and reference
counting.

This library builds for both host (e.g., Linux and macOS) as well as target
(i.e., Fuchsia) platforms and is not source or binary stable. When building for
host platforms, we currently build with C++11, which means this library needs
to build with C++11.
