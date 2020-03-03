# Storage Manager Manual Integration

## Overview

This is a manually invoked test that enables validating changes to the
`identity_storage_manager` library.  The test exercises the library against
real hardware devices; therefore, they are not hermetic and should not be
run automatically.  This test exists in lieu of an integration test that
would normally exercise these flows, and will be replaced once such an
integration test is possible.

## Running

The test can be invoked by a developer as a normal integration test, using
`fx test`.

## Future work

This test will be removed once an automated integration test is set up that
provides coverage for the account system - local storage integration.
