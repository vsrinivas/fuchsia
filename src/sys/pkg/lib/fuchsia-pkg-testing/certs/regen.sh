#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -exuo pipefail

EXPIRATION_DAYS=3650

# Cleanup any existing working directory and create a new one
rm -rf work
mkdir work

# Set exit trap to clean up temporary files
function finish {
  rm -rf work
}
trap finish EXIT

# Clean up old outputs if present
rm -f ca.cert server.certchain test.fuchsia.com.rsa

# Create a new self-signed root CA
openssl req -batch -nodes \
          -newkey rsa:4096 -sha256 \
          -subj "/CN=fuchsia test RSA CA" \
          -keyout work/ca.key \
          -out work/ca.req

openssl x509 -req \
          -in work/ca.req \
          -out work/ca.cert \
          -extensions ca -extfile openssl.cnf \
          -signkey work/ca.key \
          -sha256 \
          -days $EXPIRATION_DAYS \
          -set_serial 1

# Create an intermediate CA signed by the root CA
openssl req -batch -nodes \
          -newkey rsa:3072 -sha256 \
          -subj "/CN=fuchsia test RSA level 2 intermediate" \
          -keyout work/intermediate.key \
          -out work/intermediate.req

openssl x509 -req \
          -extensions itermediate -extfile openssl.cnf \
          -in work/intermediate.req \
          -out work/intermediate.cert \
          -CA work/ca.cert \
          -CAkey work/ca.key \
          -sha256 \
          -days $EXPIRATION_DAYS \
          -set_serial 2

# Create a test.fuchsia.com certificate signed by the intermediate CA
openssl req -batch -nodes \
          -newkey rsa:2048 -sha256 \
          -subj "/CN=test.fuchsia.com" \
          -keyout work/test.fuchsia.com.key \
          -out work/test.fuchsia.com.req

openssl x509 -req \
          -extensions test_fuchsia_com -extfile openssl.cnf \
          -in work/test.fuchsia.com.req \
          -out work/test.fuchsia.com.cert \
          -CA work/intermediate.cert \
          -CAkey work/intermediate.key \
          -sha256 \
          -days $EXPIRATION_DAYS \
          -set_serial 3

# Export the test.fuchsia.com private key
openssl rsa \
          -in work/test.fuchsia.com.key \
          -out work/test.fuchsia.com.rsa

# Create a *.fuchsia-updates.googleusercontent.com certificate signed by the intermediate CA
openssl req -batch -nodes \
          -newkey rsa:2048 -sha256 \
          -subj "/CN=*.fuchsia-updates.googleusercontent.com" \
          -keyout work/wildcard.fuchsia-updates.googleusercontent.com.key \
          -out work/wildcard.fuchsia-updates.googleusercontent.com.req

openssl x509 -req \
          -extensions wildcard_fuchsia_updates_googleusercontent_com -extfile openssl.cnf \
          -in work/wildcard.fuchsia-updates.googleusercontent.com.req \
          -out work/wildcard.fuchsia-updates.googleusercontent.com.cert \
          -CA work/intermediate.cert \
          -CAkey work/intermediate.key \
          -sha256 \
          -days $EXPIRATION_DAYS \
          -set_serial 3

# Export the second *.fuchsia-updates.googleusercontent.com private key
openssl rsa \
          -in work/wildcard.fuchsia-updates.googleusercontent.com.key \
          -out work/wildcard.fuchsia-updates.googleusercontent.com.rsa

# Export the chains of certificates for the server certs
cat work/test.fuchsia.com.cert work/intermediate.cert work/ca.cert > test.fuchsia.com.certchain
cat work/wildcard.fuchsia-updates.googleusercontent.com.cert work/intermediate.cert work/ca.cert > wildcard.fuchsia-updates.googleusercontent.com.certchain

# Extract the results
mv work/ca.cert .
mv work/test.fuchsia.com.rsa .
mv work/wildcard.fuchsia-updates.googleusercontent.com.rsa .
