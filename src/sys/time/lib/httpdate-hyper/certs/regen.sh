#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

## A script for regenerating TLS certificates used in tests for httpdate-hyper.
## This generates certificates that are already expired which should not be used
## outside of a test.
## In most cases, the certificates merged in tree alongside this script are
## sufficient to run the tests. This script should only need to be run in cases
## where the script itself has been updated to produce certificates with updated
## properties (such as using a different crypto scheme).

set -e

# Validity range of the certificates, deliberately set in the past.
START_DATE="20150315010000Z"
END_DATE="20160315010000Z"

WORK_DIR="./work"
if [ -a $WORK_DIR ]; then
  echo "Attempted to create $WORK_DIR directory for temporary artifacts but it already exists."
  echo "Please check contents and remove or run the script from another directory."
  exit 1
fi

mkdir $WORK_DIR
# openssl ca maintains a database for cert management. Since we only need the
# certificates, put the database in the work dir we remove after cert generation.
touch $WORK_DIR/index.txt

# Clean up old outputs if present
rm -f ca.cert server.certchain server.rsa notbefore notafter

# Create a new self-signed root CA
openssl req -batch -nodes \
          -newkey rsa:4096 -sha256 \
          -subj "/CN=fuchsia time test RSA CA" \
          -keyout $WORK_DIR/ca.key \
          -out $WORK_DIR/ca.req

openssl ca \
          -config openssl.cnf \
          -extensions ca_ext -extfile openssl.cnf \
          -in $WORK_DIR/ca.req \
          -out $WORK_DIR/ca.cert \
          -notext \
          -outdir $WORK_DIR \
          -selfsign \
          -keyfile $WORK_DIR/ca.key \
          -create_serial \
          -startdate $START_DATE \
          -enddate $END_DATE

# Create an intermediate CA signed by the root CA
openssl req -batch -nodes \
          -newkey rsa:3072 -sha256 \
          -subj "/CN=fuchsia time test RSA level 2 intermediate" \
          -keyout $WORK_DIR/intermediate.key \
          -out $WORK_DIR/intermediate.req

openssl ca \
          -config openssl.cnf \
          -extensions intermediate -extfile openssl.cnf \
          -in $WORK_DIR/intermediate.req \
          -out $WORK_DIR/intermediate.cert \
          -notext \
          -outdir $WORK_DIR \
          -cert $WORK_DIR/ca.cert \
          -keyfile $WORK_DIR/ca.key \
          -create_serial \
          -startdate $START_DATE \
          -enddate $END_DATE

# Create a server certificate signed by the intermediate CA
openssl req -batch -nodes \
          -newkey rsa:2048 -sha256 \
          -subj "/CN=test.fuchsia.com" \
          -keyout $WORK_DIR/server.key \
          -out $WORK_DIR/server.req

openssl ca \
          -config openssl.cnf \
          -extensions server -extfile openssl.cnf \
          -in $WORK_DIR/server.req \
          -out $WORK_DIR/server.cert \
          -notext \
          -outdir $WORK_DIR \
          -cert $WORK_DIR/intermediate.cert \
          -keyfile $WORK_DIR/intermediate.key \
          -create_serial \
          -startdate $START_DATE \
          -enddate $END_DATE

# Export certificate's valid date range in ISO 8601/RFC 3349
NOT_BEFORE=$(openssl x509 -in $WORK_DIR/server.cert -noout -startdate \
          | cut -d= -f 2)
date --date="$NOT_BEFORE" --iso-8601=sec --utc > $WORK_DIR/notbefore
NOT_AFTER=$(openssl x509 -in $WORK_DIR/server.cert -noout -enddate \
          | cut -d= -f 2)
date --date="$NOT_AFTER" --iso-8601=sec --utc > $WORK_DIR/notafter

# Export the server's private key
openssl rsa \
          -in $WORK_DIR/server.key \
          -out $WORK_DIR/server.rsa

# Export the chain of certificates for the server cert
cat $WORK_DIR/server.cert $WORK_DIR/intermediate.cert $WORK_DIR/ca.cert > server.certchain

# Extract the results
mv $WORK_DIR/ca.cert .
mv $WORK_DIR/server.rsa .
mv $WORK_DIR/notbefore .
mv $WORK_DIR/notafter .

# Clean up temporary files
rm -rf $WORK_DIR
