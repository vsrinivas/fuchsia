

<!--
    (C) Copyright 2018 The Fuchsia Authors. All rights reserved.
    Use of this source code is governed by a BSD-style license that can be
    found in the LICENSE file.
-->

# The Driver Development Kit Tutorial

This document is part of the [Zircon Driver Development Kit](overview.md) documentation.

This Driver Development Kit (*DDK*) tutorial documentation section consists of:

*   [Getting Started](getting_started.md) &mdash; a beginner's guide to writing device drivers
*   [Simple Drivers](simple.md) &mdash; an overview of what a driver does, with code examples
*   [Hardware Interfacing](hardware.md) &mdash; how to deal with your device's hardware
*   [RAMDisk Device](ramdisk.md) &mdash; walkthrough of RAMdisk block driver
*   [Ethernet Devices](ethernet.md) &mdash; walkthrough of Intel Ethernet driver
*   [Advanced Topics and Tips](advanced.md) &mdash; hints for experienced driver writers
    and comments on unusual situations
*   [Tracing](tracing.md) &mdash; monitoring driver performance with tracing
*   [Reference](reference.md) &mdash; helper functions, data structures, manifest constants

The sections are listed above in default reading order, but it's perfectly fine
to jump around and read them in order of interest or applicability.

Generally, each section is written with increasing complexity; the first parts of each
section can safely be skipped / skimmed by experts, whereas a beginner should find
sufficient explanation to allow them to understand the advanced sections.

Indeed, the above chapter structure follows the same progression: from beginner
to advanced, allowing the expert to skip / skim early sections while providing a
beginner with sufficient information.

