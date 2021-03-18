# Tools

This directory contains tools and utilities for executing the Compatibility Test
Suite (CTS).

Note:
* Tests for host-side tools should go in //sdk/cts/tests/tools.
* Binaries that are needed for specific tests should go in //sdk/cts/util/...,
  in an appropriate subdirectory.
* This tools directory should be used for holding tools that operate on the
  overall Compatibility Test Suite.

## testdriver

testdriver is a program that is packaged alongside every CTS artifact, and
enables execution of the CTS outside of the fuchsia tree.
