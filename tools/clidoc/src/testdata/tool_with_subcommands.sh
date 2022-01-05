#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# Top level help
if [[ $1 == "--help" ]]; then
    cat <<EOF
Usage: host-tool-cmd
Top level tool description

Options:
    --help   Display usage information
    --verbose Print progress details

Commands:
    subcommand1  Starts subcommand1
    subcommand2  Starts subcommand2
    unrecognized Should not show up - returns Unrecognized argument:
    command      Should not be recursed
    subcommand   Should not be recursed
EOF
elif [[ $1 == "subcommand1" ]] && [[ $2 == "--help" ]]; then
    cat <<EOF
Usage: host-tool-cmd subcommand1 <args>
Description goes here subcommand1 with <args>

Options:
    --sc1   sc1 option for subcommand1
EOF
elif [[ $1 == "subcommand2" ]] && [[ $2 == "--help" ]]; then
    cat <<EOF
Usage: host-tool-cmd subcommand2
Description goes here subcommand2

EOF
elif [[ $1 == "unrecognized" ]] && [[ $2 == "--help" ]]; then
    cat <<EOF
Unrecognized argument: This should not show up in the md file.

EOF
fi
