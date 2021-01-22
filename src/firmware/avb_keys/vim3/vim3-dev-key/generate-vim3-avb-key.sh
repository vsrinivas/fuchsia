# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

AVB_TOOL="${FUCHSIA_DIR}/third_party/android/platform/external/avb/avbtool"

# General dev AVB PRK that may be used for multiple products
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out devkey_atx_prk.pem

# General dev AVB PIK that may be used for multiple products
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out devkey_atx_pik.pem

# Vim3 specific dev PSK
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out vim3_devkey_atx_psk.pem

# Vim3 specific dev PUK
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out vim3_devkey_atx_puk.pem

# Vim3 product ID
dd if=/dev/random bs=1 count=16 of=vim3_dev_product_id.bin

# Vim3 PSK certificate
${AVB_TOOL} make_atx_certificate --output vim3_dev_atx_psk_certificate.bin \
--subject_key vim3_devkey_atx_psk.pem --subject_key_version 0 \
--authority_key devkey_atx_pik.pem --subject vim3_dev_product_id.bin

# Vim3 permanent attributes
${AVB_TOOL} make_atx_permanent_attributes --root_authority_key devkey_atx_prk.pem \
--product_id vim3_dev_product_id.bin --output vim3_dev_atx_permanent_attributes.bin

# Dev AVB PIK
${AVB_TOOL}  make_atx_certificate --output dev_atx_pik_certificate.bin \
--subject_key devkey_atx_pik.pem --subject_key_version 0 \
--authority_key devkey_atx_prk.pem --subject_is_intermediate_authority \
--subject vim3_dev_product_id.bin

${AVB_TOOL} make_atx_metadata --intermediate_key_certificate dev_atx_pik_certificate.bin \
--product_key_certificate vim3_dev_atx_psk_certificate.bin --output vim3_dev_atx_metadata.bin
