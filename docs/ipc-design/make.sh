#!/bin/bash

# Script for generating HTML output for the document locally.

set -eu

input=mbmq-model.md
output=mbmq-model.html

if [ $input -nt $output -o make.sh -nt $output ]; then
    echo -- regenerate
    (echo '<style>body { width: 980px; margin: auto; } </style>';
     cmark $input) >$output.tmp
    mv $output.tmp $output
fi
