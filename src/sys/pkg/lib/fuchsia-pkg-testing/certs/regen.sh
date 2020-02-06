#!/bin/bash
set -ex

EXPIRATION_DAYS=3650

# Cleanup any existing working directory and create a new one
rm -rf work
mkdir work

# Clean up old outputs if present
rm -f ca.cert server.certchain server.rsa

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

# Create a server certificate signed by the intermediate CA
openssl req -batch -nodes \
          -newkey rsa:2048 -sha256 \
          -subj "/CN=test.fuchsia.com" \
          -keyout work/server.key \
          -out work/server.req

openssl x509 -req \
          -extensions server -extfile openssl.cnf \
          -in work/server.req \
          -out work/server.cert \
          -CA work/intermediate.cert \
          -CAkey work/intermediate.key \
          -sha256 \
          -days $EXPIRATION_DAYS \
          -set_serial 3

# Export the server's private key
openssl rsa \
          -in work/server.key \
          -out work/server.rsa

# Export the chain of certificates for the server cert
cat work/server.cert work/intermediate.cert work/ca.cert > server.certchain

# Extract the results
mv work/ca.cert .
mv work/server.rsa .

# Clean up temporary files
rm -rf work
