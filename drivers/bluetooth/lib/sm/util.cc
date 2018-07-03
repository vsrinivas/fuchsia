// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <openssl/aes.h>

#include "garnet/drivers/bluetooth/lib/hci/util.h"
#include "lib/fxl/logging.h"

namespace btlib {

using common::BufferView;
using common::ByteBuffer;
using common::DeviceAddress;
using common::UInt128;

namespace sm {
namespace util {
namespace {

constexpr size_t kPreqSize = 7;

// Swap the endianness of a 128-bit integer. |in| and |out| should not be backed
// by the same buffer.
void Swap128(const UInt128& in, UInt128* out) {
  FXL_DCHECK(out);
  for (size_t i = 0; i < in.size(); ++i) {
    (*out)[i] = in[in.size() - i - 1];
  }
}

// XOR two 128-bit integers and return the result in |out|. It is possible to
// pass a pointer to one of the inputs as |out|.
void Xor128(const UInt128& int1, const UInt128& int2, UInt128* out) {
  FXL_DCHECK(out);

  uint64_t lower1 = *reinterpret_cast<const uint64_t*>(int1.data());
  uint64_t upper1 = *reinterpret_cast<const uint64_t*>(int1.data() + 8);

  uint64_t lower2 = *reinterpret_cast<const uint64_t*>(int2.data());
  uint64_t upper2 = *reinterpret_cast<const uint64_t*>(int2.data() + 8);

  uint64_t lower_res = lower1 ^ lower2;
  uint64_t upper_res = upper1 ^ upper2;

  std::memcpy(out->data(), &lower_res, 8);
  std::memcpy(out->data() + 8, &upper_res, 8);
}

}  // namespace

std::string PairingMethodToString(PairingMethod method) {
  switch (method) {
    case PairingMethod::kJustWorks:
      return "Just Works";
    case PairingMethod::kPasskeyEntryInput:
      return "Passkey Entry (input)";
    case PairingMethod::kPasskeyEntryDisplay:
      return "Passkey Entry (display)";
    case PairingMethod::kNumericComparison:
      return "Numeric Comparison";
    case PairingMethod::kOutOfBand:
      return "OOB";
    default:
      break;
  }
  return "(unknown)";
}

PairingMethod SelectPairingMethod(bool sec_conn, bool local_oob, bool peer_oob,
                                  bool mitm_required, IOCapability local_ioc,
                                  IOCapability peer_ioc, bool local_initiator) {
  if ((sec_conn && (local_oob || peer_oob)) ||
      (!sec_conn && local_oob && peer_oob)) {
    return PairingMethod::kOutOfBand;
  }

  // If neither device requires MITM protection or if the peer has not I/O
  // capable, we select Just Works.
  if (!mitm_required || peer_ioc == IOCapability::kNoInputNoOutput) {
    return PairingMethod::kJustWorks;
  }

  // Select the pairing method by comparing I/O capabilities. The switch
  // statement will return if an authenticated entry method is selected.
  // Otherwise, we'll break out and default to Just Works below.
  switch (local_ioc) {
    case IOCapability::kNoInputNoOutput:
      break;

    case IOCapability::kDisplayOnly:
      switch (peer_ioc) {
        case IOCapability::kKeyboardOnly:
        case IOCapability::kKeyboardDisplay:
          return PairingMethod::kPasskeyEntryDisplay;
        default:
          break;
      }
      break;

    case IOCapability::kDisplayYesNo:
      switch (peer_ioc) {
        case IOCapability::kDisplayYesNo:
          return sec_conn ? PairingMethod::kNumericComparison
                          : PairingMethod::kJustWorks;
        case IOCapability::kKeyboardDisplay:
          return sec_conn ? PairingMethod::kNumericComparison
                          : PairingMethod::kPasskeyEntryDisplay;
        case IOCapability::kKeyboardOnly:
          return PairingMethod::kPasskeyEntryDisplay;
        default:
          break;
      }
      break;

    case IOCapability::kKeyboardOnly:
      return PairingMethod::kPasskeyEntryInput;

    case IOCapability::kKeyboardDisplay:
      switch (peer_ioc) {
        case IOCapability::kKeyboardOnly:
          return PairingMethod::kPasskeyEntryDisplay;
        case IOCapability::kDisplayOnly:
          return PairingMethod::kPasskeyEntryInput;
        case IOCapability::kDisplayYesNo:
          return sec_conn ? PairingMethod::kNumericComparison
                          : PairingMethod::kPasskeyEntryInput;
        default:
          break;
      }

      // If both devices have KeyboardDisplay then use Numeric Comparison
      // if S.C. is supported. Otherwise, the initiator always displays and the
      // responder inputs a passkey.
      if (sec_conn) {
        return PairingMethod::kNumericComparison;
      }
      return local_initiator ? PairingMethod::kPasskeyEntryDisplay
                             : PairingMethod::kPasskeyEntryInput;
  }

  return PairingMethod::kJustWorks;
}

void Encrypt(const common::UInt128& key, const common::UInt128& plaintext_data,
             common::UInt128* out_encrypted_data) {
  // Swap the bytes since "the most significant octet of key corresponds to
  // key[0], the most significant octet of plaintextData corresponds to in[0]
  // and the most significant octet of encryptedData corresponds to out[0] using
  // the notation specified in FIPS-197" for the security function "e" (Vol 3,
  // Part H, 2.2.1).
  UInt128 be_k, be_pt, be_enc;
  Swap128(key, &be_k);
  Swap128(plaintext_data, &be_pt);

  AES_KEY k;
  AES_set_encrypt_key(be_k.data(), 128, &k);
  AES_encrypt(be_pt.data(), be_enc.data(), &k);

  Swap128(be_enc, out_encrypted_data);
}

void C1(const UInt128& tk, const UInt128& rand, const ByteBuffer& preq,
        const ByteBuffer& pres, const DeviceAddress& initiator_addr,
        const DeviceAddress& responder_addr, UInt128* out_confirm_value) {
  FXL_DCHECK(preq.size() == kPreqSize);
  FXL_DCHECK(pres.size() == kPreqSize);
  FXL_DCHECK(out_confirm_value);

  UInt128 p1, p2;

  // Calculate p1 = pres || preq || rat’ || iat’
  hci::LEAddressType iat = hci::AddressTypeToHCI(initiator_addr.type());
  hci::LEAddressType rat = hci::AddressTypeToHCI(responder_addr.type());
  p1[0] = static_cast<uint8_t>(iat);
  p1[1] = static_cast<uint8_t>(rat);
  std::memcpy(p1.data() + 2, preq.data(), preq.size());  // Bytes [2-8]
  std::memcpy(p1.data() + 2 + preq.size(), pres.data(), pres.size());  // [9-15]

  // Calculate p2 = padding || ia || ra
  BufferView ia = initiator_addr.value().bytes();
  BufferView ra = responder_addr.value().bytes();
  std::memcpy(p2.data(), ra.data(), ra.size());  // Lowest 6 bytes
  std::memcpy(p2.data() + ra.size(), ia.data(), ia.size());  // Next 6 bytes
  std::memset(p2.data() + ra.size() + ia.size(), 0,
              p2.size() - ra.size() - ia.size());  // Pad 0s for the remainder

  // Calculate the confirm value: e(tk, e(tk, rand XOR p1) XOR p2)
  UInt128 tmp;
  Xor128(rand, p1, &p1);
  Encrypt(tk, p1, &tmp);
  Xor128(tmp, p2, &tmp);
  Encrypt(tk, tmp, out_confirm_value);
}

void S1(const UInt128& tk, const UInt128& r1, const UInt128& r2,
        UInt128* out_stk) {
  FXL_DCHECK(out_stk);

  UInt128 r_prime;

  // Take the lower 64-bits of r1 and r2 and concatanate them to produce
  // r’ = r1’ || r2’, where r2' contains the LSB and r1' the MSB.
  constexpr size_t kHalfSize = sizeof(UInt128) / 2;
  std::memcpy(r_prime.data(), r2.data(), kHalfSize);
  std::memcpy(r_prime.data() + kHalfSize, r1.data(), kHalfSize);

  // Calculate the STK: e(tk, r’)
  Encrypt(tk, r_prime, out_stk);
}

}  // namespace util
}  // namespace sm
}  // namespace btlib
