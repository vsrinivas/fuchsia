#! /bin/bash
# Copy the ipt "ptout" files from the target.
# Requires the netcp program to be in $PATH.
# A local copy of foo.ptlist, named foo.xptlist, is created containing
# local paths of trace buffer files.

set -e

declare -r SCRIPT=$(basename $0)
declare -r SIDEBAND_SUFFIXES=".ktrace .ptlist"

if [ $# -ne 3 ]
then
    echo "Usage: $SCRIPT <zircon-hostname> <path-prefix> <output-path-prefix>" >&2
    echo "Use \"\" for the hostname if unknown and there's only one." >&2
    echo "Example: $SCRIPT \"\" /tmp/ptout ./ptout" >&2
    exit 1
fi

declare -r ZIRCON_HOSTNAME=$1
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
    netcp "${ZIRCON_HOSTNAME}:${SOURCE_SPEC}${s}" "$file"
done

# This file contains a list of the files with trace buffers.
declare -r PTLIST_FILE="${OUTPUT_SPEC}.ptlist"
# Same as foo.ptlist, but contains local paths.
declare -r XPTLIST_FILE="${OUTPUT_SPEC}.xptlist"

rm -f "$XPTLIST_FILE"

while read line
do
    set $line
    seqno=$1
    file=$2
    cpu_suffix="${file#${SOURCE_SPEC}}"
    output_file="${OUTPUT_SPEC}${cpu_suffix}"
    netcp "${ZIRCON_HOSTNAME}:$file" "$output_file"
    echo "$seqno $output_file" >> "$XPTLIST_FILE"
done < "$PTLIST_FILE"
