#!/usr/bin/env bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Run, inspect and debug
#### EXECUTABLE=${PREBUILT_3P_DIR}/python3/${HOST_PLATFORM}/bin/python3 ${FUCHSIA_DIR}/tools/devshell/contrib/extract-pprof.py
### Extract pprof data from inspect.json
## Fuchsia diagnostics provides a component inspection mechanism ("inspect")
## (see ## https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect);
## Fuchsia snapshots (see https://fuchsia.dev/reference/tools/fx/cmd/snapshot)
## include data collected from inspect in a file typically named inspect.json.
##
## pprof is a tool for visualization and analysis of profiling data (see
## https://github.com/google/pprof). Certain Fuchsia components expose profiling
## data to inspect; this data ends up in snapshots which can be useful for
## debugging crashes, memory leaks, concurrency bugs, etc.
##
## This tool extracts and decodes the profiling data for the given component
## moniker from inspect.json so that it can be used with the pprof tool.
##
## usage: fx extract-pprof --inspect FILE --component MONIKER
##
##   --inspect   Path to the inspect.json file
##   --component Moniker to extract pprof data for (e.g. core/network/netstack)
##
## optional arguments:
##   --output    Directory to extract pprof data to; If not specified, a temporary
##               directory will be created
