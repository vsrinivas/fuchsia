# Debug Agent Integration Tests

These tests are all tests that exercise real functionality within Zircon.
They are meant to verify that the debug agent actually does what the unit tests
say it does.

## Functionality

The tests generally work by "tricking" the debug agent that it's connected to
a client. This is important because the DebugAgent works receiving messages
triggered by the message loop and either returns immediatelly from the remote
API (//src/developer/debug/debug_agent/remote_api.h) or sending out notifications.

Generally all the interesting behaviour is in the notifications, as they
represent exceptions (ie. I hit a breakpoint or a process died). There is a
wrapper class meant to easily react to those notifications.

## Usage

This directory contains both the tests (ending in "\_test.cc") and utilities
for making these kind of testing easier.

Some pre-work needs to be done on each test in order to work properly:

### MockStreamBackend

This class mocks being a debug_ipc::StreamBuffer::Writer, which is the interface
the DebugAgent uses to write it's notifications. Each test is meant to inherit
from this class and override the notifications it's interested in.

### MessageLoopWrapper

A simple RAII wrapper over the message loop. Nothing really special about it,
but it's good to handle resources about it.

### SoWrapper

A simple RAII wrapper over loading a .so. This is meant to have an easy way to
query symbols using the dlsym-interface.

### Example

breakpoint_test.cc is a canonical example and should show the usage of these
elements.
