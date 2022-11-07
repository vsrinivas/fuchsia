#!/bin/bash
set -eux

cd "$(dirname "$0")"

for key_size in 2048 4096; do
    key="rsa-$key_size"
    pk8="$key.pk8.der"
    spki="$key.spki.der"
    pkcs1="$key.pkcs1.der"
    key="$key.der"

    if [ ! -f "$key" ]; then
        openssl genpkey -algorithm RSA \
                        -pkeyopt "rsa_keygen_bits:$key_size" \
                        -pkeyopt rsa_keygen_pubexp:65537 \
                        -outform der \
                        -out "$key"
    fi

    openssl rsa -in "$key" \
                -inform der \
                -RSAPublicKey_out \
                -outform der \
                -out "$pkcs1"

    openssl rsa -in "$key" \
                -inform der \
                -pubout \
                -outform der \
                -out "$spki"

    openssl pkcs8 -topk8 \
                  -inform der \
                  -in "$key" \
                  -outform der \
                  -out "$pk8" \
                  -nocrypt
done
