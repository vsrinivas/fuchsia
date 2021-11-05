#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly VALIDATOR="$1"
readonly DATA_DIR="$2"

function base_cmd {
  readonly SCHEMA="$DATA_DIR/$1"
  readonly JSON="$DATA_DIR/$2"
  echo "$VALIDATOR $SCHEMA $JSON"
}

# A simple base case.
cmd="$(base_cmd test_schema.json test_document.json)"
if ! $cmd; then
  "Command '$cmd' failed unexpectedly."
  exit 1
fi

# Make sure that an internal reference is properly used to invalidate a file.
# This exercises a failure mode in the rapidjson-based validator.
cmd="$(base_cmd test_schema_with_ref.json test_document.json)"
if $cmd; then
  "Command '$cmd' succeeded unexpectedly."
  exit 1
fi

# A document with comments can be validated if parsed as JSON5.
cmd="$(base_cmd test_schema.json test_document_with_comments.json) --json5"
if ! $cmd; then
  "Command '$cmd' failed unexpectedly."
  exit 1
fi
