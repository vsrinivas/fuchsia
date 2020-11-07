#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import re
import sys

# The regexes for the "CPU" line preamble and that of a set of CPUID values for
# a given (leaf, subleaf), respectively. Both assume the leading and trailing
# whitespace have been stripped.
CPU_LINE_RE = re.compile("^CPU(\s[0-9]+)?:$")
CPUID_LINE_RE = re.compile(
    "^0x([a-z0-9]{8})\s0x([a-z0-9]{2}):\seax=0x([a-z0-9]{8})\sebx=0x([a-z0-9]{8})\secx=0x([a-z0-9]{8})\sedx=0x([a-z0-9]{8})$"
)


def main():
    Log("reading from stdin...")
    data = sys.stdin.readlines()
    jsonish = Parse(data)
    if jsonish is None:
        Log("error: could not parse input")
        return 1
    Log("writing to stdout...")
    json.dumps(jsonish, indent=4, sys.stdout)


# Prints to stderr, reserving stdout for the final JSON output.
def Log(msg):
    sys.stderr.write("%s\n" % msg)


def Parse(data):
    if not data:
        Log("error: no data provided")
        return None
    elif not CPU_LINE_RE.match(data[0]):
        Log(
            "error: expected `%s` for a first line; not \"%s\"" %
            (CPU_LINE_RE, data[0]))
        return None
    data = data[1:]

    jsonish = []
    for line in data:
        line = line.strip()

        if not line:
            break

        # If we happen on another "CPU..." line, assume that we are viewing
        # concatenated CPUID values for different CPUs (as is the case with
        # `cpuid -r`) - and that we have already fully collected the data from
        # a previous CPU.
        if CPU_LINE_RE.match(line):
            break

        matches = CPUID_LINE_RE.match(line)
        if matches is None:
            Log("error: could not parse line \"%s\"" % line)
            return None
        values = matches.groups()
        if len(values) < 6:
            Log(
                "error: parsed %s from line \"%s\"; expected more values" %
                (values, line))
            return None
        jsonish.append(
            {
                "leaf": int(values[0], 16),
                "subleaf": int(values[1], 16),
                "eax": int(values[2], 16),
                "ebx": int(values[3], 16),
                "ecx": int(values[4], 16),
                "edx": int(values[5], 16),
            })
    return jsonish


if __name__ == "__main__":
    sys.exit(main())
