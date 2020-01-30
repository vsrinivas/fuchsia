# Zircon display driver test strategy

Status: Approved

Authors: payamm@google.com, rlb@google.com

Last Updated: 2020-01-30

## Objective

Ensure that Zircon display drivers are robust, fast, and modifiable.

A robust driver is:

 1. Able to deal with known hardware "quirks".
 2. Protected from regressions.
 3. Free from resource leaks.
 4. Generally immune to misbehaving clients.

A fast driver is:

 1. Demonstrably fast through benchmarks.
 2. Free from spinlocks (or other busy-waits) and pathologically large critical
    sections.
 3. Free from allocations on the fast path.

A modifiable driver:

 1. Has targeted tests that narrow most bugs down to at most one class or small
    (300 loc) module.
 2. Has larger tests that verify a driver's support for all display functions.
 3. Has an intelligible threading model

## Background

The diagram below shows the overall structure of components involved in draw
pixels on a given display. The components that are relevant to this document are
colored in blue.

![Diagram: Scenic connects to the Fuchsia display subsystem. The Core Display
Driver serves the fuchsia.hardware.display FIDL interface, and interacts with
the hardware-specific Display Driver. The display drivers rely on Sysmem and
low-level protocols such as GPIO, I2C, DSI, and HDMI](display-driver-env.png)

The display stack can be divided into two main components: Core Display and
Device Driver. Core Display is the hardware-independent layer sitting between
display clients and display drivers. Device driver is the actual driver that
communicates with actual hardware to drive pixels onto a screen.

Each device driver can be further divided into "control" code that organizes
independent hardware functions into useful features and "driver" code that
programs those independent hardware functions.

## Overview

Most software projects with good testing discipline organize tests into phases,
with increasing cost and (typically) decreasing precision at each phase. All
tests are a tradeoff in maintenance effort, accuracy (false-positive rates), and
precision (size of code under test). Tests for drivers are no different.

That said, unit testing device drivers is notoriously hard because hardware has
limited specifications and misbehaves in myriad ways, making reproducibility
hard and simulation effectively impossible. If a test directly verifies that
code is issuing the correct MMIO sequence, that test neither covers a large
fraction of the failure modes nor does it continue to work in the presence of
small changes to the codebase.

To address these concerns:

 1. Only "control" code should be exercised in unit tests.
 2. "Driver" code will be exercised in conformance tests running on target
    hardware.
 3. Integration and stress tests will be used to ensure that drivers are not
    depending on undefined behavior.
 4. End-to-end tests will verify that applications continue to work.
 5. Fuzz tests will ensure that drivers are robust to misbehaving clients.

## Detailed design

### Unit tests

> Only "control" code should be exercised in unit tests.

In addition to the MMIO/register poking work of a driver, displays and GPUs also
have a large amount of code for OS functions. They are responsible for managing
power, video modes, and OS resources (e.g. zx::event signaling). They also
handle firmware loading, CPU-side state tracking, etc.

This "control" code is the source of many bugs and can benefit from unit tests
with the accompanying ASAN/TSAN coverage. Separating this code from hardware
interaction improves code coverage, makes tests deterministic, and tightens the
feedback loop on most bugs.

We will follow the same strategy for the common display controller code in
`zircon/system/dev/display/display`.

Sometimes it is not possible to separate these two types of code, e.g. when
testing self-contained hardware functionality like TLB management. In those
cases, a test fixture can reset the hardware in between each test case. For now,
this can be achieved by creating an in-driver test-suite that runs after `Bind`
but before `MakeVisible`.

### Conformance tests

> "Driver" code will be exercised in conformance tests running on target
> hardware.

`zircon/system/dev/display/display/test` currently contains test fixtures and
helper classes for exercising the display core and a driver. At the moment, only
the `fake-display` driver can be used.

In order to reduce the scope of tests and improve their accuracy and precision,
we will create a conformance test suite in the core display controller that
verifies that the display-impl is working correctly. This allows us to test
display-core separately with high confidence.

### Integration tests

> Integration [...] tests will be used to ensure that drivers are not depending
> on undefined behavior.

Normally integration tests focus entirely on making sure that the component
under test is using APIs correctly. A Fuchsia system is effectively a
distributed system in a box, so integration tests have an additional function:
FIDL and Banjo services can be used as points of fault injection.

Single-process integration tests are well-contained and thus offer a good
starting point for aggregate performance tests. We can build benchmarks for
common client workflows by profiling test execution and restricting samples to
display driver code.

Stress tests are a form of integration test that is helpful for
kernel-adjacent code with many complex interactions. Test accuracy must be
weighed against test latency, but most tests are not optimal. Accuracy and
latency can be improved at the same time by increasing the stresses per second
-- deliberately injecting faults, process crashes, and load can turn a 5 minute
test into an accurate release qualifier.

Concretely, the core display controller and the various display-impl drivers
will be subjected to integration tests that delay messages and pretend that
other processes have died or produced invalid inputs. Once there are established
patterns, a shared set of `FlakyFoo` classes will be created as testing fakes.

Resource leaks will be detected by introspecting the process during test
shutdown.

### End-to-end (e2e) tests

> End-to-end tests will verify that applications continue to work.

All of the aforementioned tests will provide high confidence, but there will be
missed cases and imperfect tests. Here we list applications that are either
directly involved in the Fuchsia UX or are simple enough to treat them as test
cases for the whole graphics stack.

#### Manual

Inspect:

 1. virtcon scrolling correctly
 2. visual output of the display-test uapp
 3. visual output from runtests -t scenic_tests
 4. Disconnect and reconnect a display, verify that outputs are stable

#### Automated

The manual tests above rely on human judgment or actuation to validate the
stack. The large variety of target devices means we cannot rely on OEM-style
camera captures. For now, we will not have automated end-to-end tests.

In the future, tests can be automated by using
[Chamelium](https://www.chromium.org/chromium-os/testing/chamelium).

### Fuzz tests

> Fuzz tests will ensure that drivers are robust to misbehaving clients.

TBD. Once there are fuzz tests for sysmem, we can build upon them. For now,
there are some integration tests verifying that the display layer doesn't crash
in the face of naive client mistakes.
