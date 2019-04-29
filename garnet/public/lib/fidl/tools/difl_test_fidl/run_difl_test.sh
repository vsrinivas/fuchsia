#!/bin/bash

cd "$(dirname "${BASH_SOURCE[0]}")/.."

PYTHON=
for p in python3.7 python3.6; do
    if command -v "${p}" > /dev/null; then
        PYTHON="${p}"
        break
    fi
done

if [[ -z "${PYTHON}" ]]; then
    echo 'WARNING: no recent Python found, not running difl tests'
else
    "${PYTHON}" -m mypy difl/*.py
    exec "${PYTHON}" -m difl.test "$@"
fi
