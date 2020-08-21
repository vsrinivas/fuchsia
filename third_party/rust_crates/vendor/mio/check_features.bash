#!/bin/bash

set -eu

declare -a feature_matrix=(
	"" # None.
	os-poll
	os-poll,os-util
	os-poll,os-util,tcp
	os-poll,os-util,tcp,udp
	os-poll,os-util,tcp,udp,uds
	os-util
	os-util,tcp
	os-util,tcp,udp
	os-util,tcp,udp,uds
	tcp
	tcp,udp
	tcp,udp,uds
	udp
	udp,uds
	uds
	extra-docs
)

#set -x

for features in "${feature_matrix[@]}"; do
	#cargo check --features "$features"
	cargo check --target "x86_64-apple-darwin" --features "$features"
	cargo check --target "x86_64-unknown-freebsd" --features "$features"
	cargo check --target "x86_64-unknown-linux-gnu" --features "$features"
	cargo check --target "x86_64-pc-windows-gnu" --features "$features"
done
