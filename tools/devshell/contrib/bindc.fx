# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#### CATEGORY=Run, inspect and debug
#### EXECUTABLE=${HOST_TOOLS_DIR}/bindc
### run the bind program compiler and debugger

## USAGE:
##     bindc [FLAGS] [OPTIONS] <input>
##
## FLAGS:
##     -a, --disable-autobind    Disable automatically binding the driver so that the driver must be bound on a user's
##                               request.
##     -h, --help                Prints help information
##     -V, --version             Prints version information
##
## OPTIONS:
##     -d, --debug <device_file>            A file containing the properties of a specific device, as a list of key-value
##                                          pairs. This will be used as the input to the bind program debugger. In debug
##                                          mode no compiler output is produced, so --output should not be specified.
##     -i, --include <include>...           The bind library input files. These may be included by the bind program. They
##                                          should be in the format described in //tools/bindc/README.md.
##     -f, --include-file <include_file>    Specifiy the bind library input files as a file. The file must contain a list
##                                          of filenames that are bind library input files that may be included by the bind
##                                          program. Those files should be in the format described in
##                                          //tools/bindc/README.md.
##     -o, --output <output>                Output file. The compiler emits a C header file.
##
## ARGS:
##     <input>    The bind program input file. This should be in the format described in //tools/bindc/README.md.
