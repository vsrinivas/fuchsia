#!/bin/bash

set -euo pipefail

readonly GO=$1
readonly GODEPFILE=$2
readonly GOROOT=$3
readonly GOPATH=$4
readonly GOPACKAGE=$5
readonly GOOS=$6
readonly GOARCH=$7
readonly DEPFILE=$8
readonly OUTPUT=$9
readonly GOCACHE=${10}

export GOROOT GOPATH GOOS GOARCH GOCACHE

"${GODEPFILE}" -o "${OUTPUT}" "${GOPACKAGE}" > "${DEPFILE}"

"${GO}" build -o "${OUTPUT}" "${GOPACKAGE}"
