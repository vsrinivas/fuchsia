# Cobalt: Telemetry with built-in privacy

[TOC]

## Overview
Cobalt is a pipeline for collecting metrics data from user-owned devices in the
field and producing aggregated reports.

Cobalt includes a suite of features for preserving user privacy and anonymity
while giving product owners the data they need to improve their products.

- **Local Differential Privacy.** If you enable it, Cobalt can add random
noise on the client during metrics collection.
- **Shuffling.** Our pipeline shuffles the collected records to break
linkability. This also amplifies the level of differential privacy.
- **No IDs.** Cobalt does not use device IDs or user IDs; not even pseudonymous
IDs.
- **No exact timestamps.** Cobalt uses day indices only.
- **Only standard metric types.** Not arbitrary protocol buffers.
- **No ad-hoc, post-facto querying.** Whereas on other telemetry systems
standard practice is to *"collect now; ask questions later"*, with Cobalt the
analyst must think in advance about what aggregations they wish to report and
predeclare reports.
- **Only aggregated data is released.** It is not possible to query the raw
collected data.

## Cobalt Core
The code in the Git repo at
[https://fuchsia.googlesource.com/cobalt](https://fuchsia.googlesource.com/cobalt)
is known as **Cobalt Core**. Cobalt Core contains the main client-side
logic for Cobalt. Cobalt Core gets pulled into a Fuchsia checkout
at `//third_party/cobalt`.

## Cobalt on Fuchsia
This directory contains an embedding of Cobalt into the Fuchsia OS. The
primary components are:

- A Cobalt FIDL service in [bin/app](/src/cobalt/bin/app)
- A test application at [bin/testapp](/src/cobalt/bin/testapp)
- The System Metrics Daemon at  [bin/system-metrics](/src/cobalt/bin/system-metrics)