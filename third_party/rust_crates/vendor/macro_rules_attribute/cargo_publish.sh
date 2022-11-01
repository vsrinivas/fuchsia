#!/bin/sh

set -euxo pipefail

(cd src/proc_macro
    cargo publish
)

for i in $(seq 10)
do
    cargo publish && exit 0
    sleep 5
done
cargo publish
