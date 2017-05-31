#! /bin/bash
# Copy the ipt "ptout" files from the target.
# Requires the netcp program to be in $PATH.
#
# Notes: At the moment this assumes a cpu-mode trace with up to four cpus.

set -e

declare -r SCRIPT=$(basename $0)
declare -r SIDEBAND_SUFFIXES=".ktrace .ptlist"

if [ $# -ne 3 ]
then
    echo "Usage: $SCRIPT <magenta-hostname> <path-prefix> <output-path-prefix>" >&2
    echo "Use \"\" for the hostname if unknown and there's only one." >&2
    echo "Example: $SCRIPT \"\" /tmp/ptout ./ptout" >&2
    exit 1
fi

declare -r MAGENTA_HOSTNAME=$1
declare -r SOURCE_SPEC=$2
declare -r OUTPUT_SPEC=$3
declare -r OUTPUT_DIR="$(dirname $OUTPUT_SPEC)"

if [ ! -d "$OUTPUT_DIR" ]
then
    echo "Not a directory: $OUTPUT_DIR" >&2
    exit 1
fi

# Keep debugging easy during development.
set -x

for s in $SIDEBAND_SUFFIXES
do
    file="${OUTPUT_SPEC}${s}"
    netcp "${MAGENTA_HOSTNAME}:${SOURCE_SPEC}${s}" "$file"
done

# This file contains a list of the files with trace buffers.
declare -r PTLIST_FILE="${OUTPUT_SPEC}.ptlist"

for f in $(cat "$PTLIST_FILE" | awk '{print $2}')
do
    cpu_suffix="${f#${SOURCE_SPEC}}"
    output_file="${OUTPUT_SPEC}${cpu_suffix}"
    netcp "${MAGENTA_HOSTNAME}:$f" "$output_file"
done
