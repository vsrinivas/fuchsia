# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Run, inspect and debug
#### EXECUTABLE=${PREBUILT_3P_DIR}/binutils-gdb/${HOST_PLATFORM}/bin/gdb
### Run GDB, the GNU DeBugger.

## USAGE:
##     fx gdb [ARGUMENTS to GDB...]
##
## This simply runs the prebuilt GDB binary with the arguments given.
## See https://www.gnu.org/software/gdb/documentation/ for details.
##
## NOTE:
##
## On Linux, it may be necessary to run:
## ```shell
## sudo apt install libpython3.7 libdebuginfod1
## ```
## before running the prebuilt GDB binary.
