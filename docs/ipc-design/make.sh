#!/bin/bash

# Script for generating HTML output for the document locally.

set -eu

inputs="
    index.md
    mbmq-model.md
    performance.md
    transition-plan.md
    shareable-channels.md
"

for input in $inputs; do
    output="$(basename $input .md).html"

    # Only regenerate the HTML file if the input file changed or this
    # file (make.sh) changed.
    if [ $input -nt $output -o make.sh -nt $output ]; then
        echo -- regenerate $output
        (echo '<style>body { width: 980px; margin: auto; } </style>';
         cmark $input) | ./munge_links.py >$output.tmp
        mv $output.tmp $output
    fi
done
