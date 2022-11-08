// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_SECURITY_LIB_KEYSAFE_KEYSAFE_H_
#define SRC_SECURITY_LIB_KEYSAFE_KEYSAFE_H_

// This UUID is generated with uuidgen
// the ITU-T UUID generator at http://www.itu.int/ITU-T/asn1/uuid.html */
#define TA_KEYSAFE_UUID                              \
  {                                                  \
    0x808032e0, 0xfd9e, 0x4e6f, {                    \
      0x88, 0x96, 0x54, 0x47, 0x35, 0xc9, 0x84, 0x80 \
    }                                                \
  }

// The IDs for the commands that would be handled by this TA
#define TA_KEYSAFE_CMD_GENERATE_KEY 0
#define TA_KEYSAFE_CMD_GET_PUBLIC_KEY 1
#define TA_KEYSAFE_CMD_ENCRYPT_DATA 2
#define TA_KEYSAFE_CMD_DECRYPT_DATA 3
#define TA_KEYSAFE_CMD_SIGN 4
#define TA_KEYSAFE_CMD_GET_HARDWARE_DERIVED_KEY 5
#define TA_KEYSAFE_CMD_PARSE_KEY 6
#define TA_KEYSAFE_CMD_IMPORT_KEY 7
#define TA_KEYSAFE_CMD_GET_USER_DATA_STORAGE_KEY 8
#define TA_KEYSAFE_CMD_ROTATE_HARDWARE_DERIVED_KEY 9

// Supported Algorithms
#define TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA256_2048 1
#define TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA256_3072 2
#define TA_KEYSAFE_ALG_RSA_SSA_PSS_SHA512_4096 3
#define TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA256_2048 4
#define TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA256_3072 5
#define TA_KEYSAFE_ALG_RSA_SSA_PKCS1_SHA512_4096 6
#define TA_KEYSAFE_ALG_ECDSA_SHA256_P256 7
#define TA_KEYSAFE_ALG_ECDSA_SHA512_P384 8
#define TA_KEYSAFE_ALG_ECDSA_SHA512_P521 9
#define TA_KEYSAFE_ALG_AES_GCM_256 10

#define TA_KEYSAFE_WRAPPING_KEY_SIZE_BITS (128)
#define TA_KEYSAFE_AES_GCM_TAG_LEN_BYTES (16)
#define TA_KEYSAFE_AES_GCM_IV_LEN_BYTES (12)

#endif  // SRC_SECURITY_LIB_KEYSAFE_KEYSAFE_H_
