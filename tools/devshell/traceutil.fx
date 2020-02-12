# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Run, inspect and debug
#### EXECUTABLE=${HOST_TOOLS_DIR}/traceutil
### Fuchsia tracing utility
## Usage: traceutil <flags> <subcommand> <subcommand args>
##
## Subcommands:
##        commands         list all command names
##        convert          Converts a JSON trace to a viewable HTML trace, or an FXT trace to both JSON and HTML.
##        flags            describe all known top-level flags
##        help             describe subcommands and their syntax
##        record           Record a trace on a target, download, and convert to HTML.
##
##
## Use "traceutil flags" for a list of top-level flags
