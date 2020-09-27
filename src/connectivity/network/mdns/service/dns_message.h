// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_DNS_MESSAGE_H_
#define SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_DNS_MESSAGE_H_

#include <memory>
#include <string>
#include <vector>

#include "src/lib/inet/ip_address.h"
#include "src/lib/inet/ip_port.h"

namespace mdns {

// DNS record types.
enum class DnsType : uint16_t {
  kInvalid = 0,
  kA = 1,            // Address
  kNs = 2,           // Name Server
  kMd = 3,           // Mail Destination
  kMf = 4,           // Mail Forwarder
  kCName = 5,        // Canonical Name
  kSoa = 6,          // Start of Authority
  kMb = 7,           // Mailbox
  kMg = 8,           // Mail Group
  kMr = 9,           // Mail Rename
  kNull = 10,        // NULL RR
  kWks = 11,         // Well-known-service
  kPtr = 12,         // Domain name pointer
  kHInfo = 13,       // Host information
  kMInfo = 14,       // Mailbox information
  kMx = 15,          // Mail Exchanger
  kTxt = 16,         // Arbitrary text string
  kRp = 17,          // Responsible person
  kAfsDb = 18,       // AFS cell database
  kX25 = 19,         // X_25 calling address
  kIsdn = 20,        // ISDN calling address
  kRt = 21,          // Router
  kNsap = 22,        // NSAP address
  kNsapPtr = 23,     // Reverse NSAP lookup (deprecated)
  kSig = 24,         // Security signature
  kKey = 25,         // Security key
  kPx = 26,          // X.400 mail mapping
  kGPos = 27,        // Geographical position (withdrawn)
  kAaaa = 28,        // IPv6 Address
  kLoc = 29,         // Location Information
  kNxt = 30,         // Next domain (security)
  kEid = 31,         // Endpoint identifier
  kNimLoc = 32,      // Nimrod Locator
  kSrv = 33,         // Service record
  kAtmA = 34,        // ATM Address
  kNaPtr = 35,       // Naming Authority PoinTeR
  kKx = 36,          // Key Exchange
  kCert = 37,        // Certification record
  kA6 = 38,          // IPv6 Address (deprecated)
  kDName = 39,       // Non-terminal DNAME (for IPv6)
  kSink = 40,        // Kitchen sink (experimental)
  kOpt = 41,         // EDNS0 option (meta-RR)
  kApl = 42,         // Address Prefix List
  kDs = 43,          // Delegation Signer
  kSshFp = 44,       // SSH Key Fingerprint
  kIpSecKey = 45,    // IPSECKEY
  kRrSig = 46,       // RRSIG
  kNSec = 47,        // Denial of Existence
  kDnsKey = 48,      // DNSKEY
  kDhcId = 49,       // DHCP Client Identifier
  kNSec3 = 50,       // Hashed Authenticated Denial of Existence
  kNSec3Param = 51,  // Hashed Authenticated Denial of Existence

  kHip = 55,  // Host Identity Protocol

  kSpf = 99,      // Sender Policy Framework for E-Mail
  kUInfo = 100,   // IANA-Reserved
  kUid = 101,     // IANA-Reserved
  kGid = 102,     // IANA-Reserved
  kUnspec = 103,  // IANA-Reserved

  kTKey = 249,   // Transaction key
  kTSig = 250,   // Transaction signature
  kIXfr = 251,   // Incremental zone transfer
  kAXfr = 252,   // Transfer zone of authority
  kMailB = 253,  // Transfer mailbox records
  kMailA = 254,  // Transfer mail agent records
  kAny = 255     // Any type
};

// DNS record classes.
enum class DnsClass : uint16_t {
  kIn = 1,      // Internet
  kCs = 2,      // CSNET
  kCh = 3,      // CHAOS
  kHs = 4,      // Hesiod
  kNone = 254,  // Used in DNS UPDATE [RFC 2136]
  kAny = 255,   // Any class
};

// Class with flag in highest bit.
struct DnsClassAndFlag {
  DnsClass class_;
  bool flag_;
};

// Domain name.
struct DnsName {
  DnsName() {}
  DnsName(const std::string dotted_string) : dotted_string_(dotted_string) {}

  std::string dotted_string_;
};

// IPV4 address.
struct DnsV4Address {
  inet::IpAddress address_;
};

// IPV6 address.
struct DnsV6Address {
  inet::IpAddress address_;
};

// Query type for DNS message headers.
enum class DnsOpCode : uint16_t {
  kQuery = 0,
  kInverseQuery = 1,
  kStatus = 2,

  kNotify = 4,
  kUpdate = 5,
};

// Response code for DNS message headers.
enum class DnsResponseCode : uint16_t {
  kNoError = 0,
  kFormatError = 1,
  kServerFailure = 2,
  kNameError = 3,
  kNotImplemented = 4,
  kRefused = 5,
  kYXDomain = 6,
  kYXRrSet = 7,
  kNxRrSet = 8,
  kNotAuthorized = 9,
  kNotZone = 10,
};

// DNS message header.
struct DnsHeader {
  uint16_t id_ = 0;
  uint16_t flags_ = 0;
  uint16_t question_count_ = 0;
  uint16_t answer_count_ = 0;
  uint16_t authority_count_ = 0;
  uint16_t additional_count_ = 0;

  bool response() { return (flags_ & kQueryResponseMask) != 0; }

  DnsOpCode op_code() { return static_cast<DnsOpCode>((flags_ & kOpCodeMask) >> kOpCodeShift); }

  bool authoritative_answer() { return (flags_ & kAuthoritativeAnswerMask) != 0; }

  bool truncated() { return (flags_ & kTruncationMask) != 0; }

  bool recursion_desired() { return (flags_ & kRecursionDesiredMask) != 0; }

  bool recursion_available() { return (flags_ & kRecursionAvailableMask) != 0; }

  DnsResponseCode response_code() {
    return static_cast<DnsResponseCode>(flags_ & kResponseCodeMask);
  }

  void SetResponse(bool value);
  void SetOpCode(DnsOpCode op_code);
  void SetAuthoritativeAnswer(bool value);
  void SetTruncated(bool value);
  void SetRecursionDesired(bool value);
  void SetRecursionAvailable(bool value);
  void SetResponseCode(DnsResponseCode response_code);

 private:
  static constexpr uint16_t kQueryResponseMask = 0x8000u;
  static constexpr uint16_t kOpCodeMask = 0x7800u;
  static constexpr uint16_t kOpCodeShift = 11u;
  static constexpr uint16_t kAuthoritativeAnswerMask = 0x0400u;
  static constexpr uint16_t kTruncationMask = 0x0200u;
  static constexpr uint16_t kRecursionDesiredMask = 0x0100u;
  static constexpr uint16_t kRecursionAvailableMask = 0x0080u;
  static constexpr uint16_t kResponseCodeMask = 0x000fu;
};

// DNS question record.
struct DnsQuestion {
  DnsQuestion();
  DnsQuestion(const std::string& name, DnsType type);

  DnsName name_;
  DnsType type_;
  DnsClass class_ = DnsClass::kIn;
  bool unicast_response_ = false;
};

// Additional data for type 'A' resource records.
struct DnsResourceDataA {
  DnsV4Address address_;

  bool operator==(const DnsResourceDataA& other) const {
    return address_.address_ == other.address_.address_;
  }
};

// Additional data for type 'NS' resource records.
struct DnsResourceDataNs {
  DnsName name_server_domain_name_;

  bool operator==(const DnsResourceDataNs& other) const {
    return name_server_domain_name_.dotted_string_ == other.name_server_domain_name_.dotted_string_;
  }
};

// Additional data for type 'CNAME' resource records.
struct DnsResourceDataCName {
  DnsName canonical_name_;

  bool operator==(const DnsResourceDataCName& other) const {
    return canonical_name_.dotted_string_ == other.canonical_name_.dotted_string_;
  }
};

// Additional data for type 'PTR' resource records.
struct DnsResourceDataPtr {
  DnsName pointer_domain_name_;

  bool operator==(const DnsResourceDataPtr& other) const {
    return pointer_domain_name_.dotted_string_ == other.pointer_domain_name_.dotted_string_;
  }
};

// Additional data for type 'TXT' resource records.
struct DnsResourceDataTxt {
  std::vector<std::string> strings_;

  bool operator==(const DnsResourceDataTxt& other) const { return strings_ == other.strings_; }
};

// Additional data for type 'AAAA' resource records.
struct DnsResourceDataAaaa {
  DnsV6Address address_;

  bool operator==(const DnsResourceDataAaaa& other) const {
    return address_.address_ == other.address_.address_;
  }
};

// Additional data for type 'SRV' resource records.
struct DnsResourceDataSrv {
  uint16_t priority_ = 0;
  uint16_t weight_ = 0;
  inet::IpPort port_;
  DnsName target_;

  bool operator==(const DnsResourceDataSrv& other) const {
    return priority_ == other.priority_ && weight_ == other.weight_ && port_ == other.port_ &&
           target_.dotted_string_ == other.target_.dotted_string_;
  }
};

// Additional data for type 'OPT' resource records.
struct DnsResourceDataOpt {
  std::vector<uint8_t> options_;

  bool operator==(const DnsResourceDataOpt& other) const { return options_ == other.options_; }
};

// Additional data for type 'NSEC' resource records.
struct DnsResourceDataNSec {
  DnsName next_domain_;
  std::vector<uint8_t> bits_;

  bool operator==(const DnsResourceDataNSec& other) const {
    return next_domain_.dotted_string_ == other.next_domain_.dotted_string_ && bits_ == other.bits_;
  }
};

// DNS resource record.
struct DnsResource {
  static constexpr uint32_t kShortTimeToLive = 2 * 60;
  static constexpr uint32_t kLongTimeToLive = 75 * 60;

  DnsResource();
  DnsResource(const std::string& name, DnsType type);
  DnsResource(const DnsResource& other);
  ~DnsResource();

  DnsResource& operator=(const DnsResource& other);

  bool operator==(const DnsResource& other) const {
    if (name_.dotted_string_ != other.name_.dotted_string_ || type_ != other.type_ ||
        class_ != other.class_ || cache_flush_ != other.cache_flush_ ||
        time_to_live_ != other.time_to_live_) {
      return false;
    }

    switch (type_) {
      case mdns::DnsType::kA:
        return a_ == other.a_;
      case mdns::DnsType::kNs:
        return ns_ == other.ns_;
      case mdns::DnsType::kCName:
        return cname_ == other.cname_;
      case mdns::DnsType::kPtr:
        return ptr_ == other.ptr_;
      case mdns::DnsType::kTxt:
        return txt_ == other.txt_;
      case mdns::DnsType::kAaaa:
        return aaaa_ == other.aaaa_;
      case mdns::DnsType::kSrv:
        return srv_ == other.srv_;
      case mdns::DnsType::kOpt:
        return opt_ == other.opt_;
      case mdns::DnsType::kNSec:
        return nsec_ == other.nsec_;
      default:
        return true;
    }
  }

  DnsName name_;
  DnsType type_ = DnsType::kInvalid;
  DnsClass class_ = DnsClass::kIn;
  bool cache_flush_ = false;
  uint32_t time_to_live_;
  union {
    DnsResourceDataA a_;
    DnsResourceDataNs ns_;
    DnsResourceDataCName cname_;
    DnsResourceDataPtr ptr_;
    DnsResourceDataTxt txt_;
    DnsResourceDataAaaa aaaa_;
    DnsResourceDataSrv srv_;
    DnsResourceDataOpt opt_;
    DnsResourceDataNSec nsec_;
  };
};

// DNS message.
struct DnsMessage {
  void UpdateCounts() {
    header_.question_count_ = static_cast<uint16_t>(questions_.size());
    header_.answer_count_ = static_cast<uint16_t>(answers_.size());
    header_.authority_count_ = static_cast<uint16_t>(authorities_.size());
    header_.additional_count_ = static_cast<uint16_t>(additionals_.size());
  }

  DnsHeader header_;
  std::vector<std::shared_ptr<DnsQuestion>> questions_;
  std::vector<std::shared_ptr<DnsResource>> answers_;
  std::vector<std::shared_ptr<DnsResource>> authorities_;
  std::vector<std::shared_ptr<DnsResource>> additionals_;
};

}  // namespace mdns

template <>
struct std::hash<mdns::DnsResourceDataA> {
  std::size_t operator()(const mdns::DnsResourceDataA& value) const noexcept {
    return std::hash<inet::IpAddress>{}(value.address_.address_);
  }
};

template <>
struct std::hash<mdns::DnsResourceDataNs> {
  std::size_t operator()(const mdns::DnsResourceDataNs& value) const noexcept {
    return std::hash<std::string>{}(value.name_server_domain_name_.dotted_string_);
  }
};

template <>
struct std::hash<mdns::DnsResourceDataCName> {
  std::size_t operator()(const mdns::DnsResourceDataCName& value) const noexcept {
    return std::hash<std::string>{}(value.canonical_name_.dotted_string_);
  }
};

template <>
struct std::hash<mdns::DnsResourceDataPtr> {
  std::size_t operator()(const mdns::DnsResourceDataPtr& value) const noexcept {
    return std::hash<std::string>{}(value.pointer_domain_name_.dotted_string_);
  }
};

template <>
struct std::hash<mdns::DnsResourceDataTxt> {
  std::size_t operator()(const mdns::DnsResourceDataTxt& value) const noexcept {
    size_t result = 0;
    for (auto& s : value.strings_) {
      result = (result << 1) ^ std::hash<std::string>{}(s);
    }

    return result;
  }
};

template <>
struct std::hash<mdns::DnsResourceDataAaaa> {
  std::size_t operator()(const mdns::DnsResourceDataAaaa& value) const noexcept {
    return std::hash<inet::IpAddress>{}(value.address_.address_);
  }
};

template <>
struct std::hash<mdns::DnsResourceDataSrv> {
  std::size_t operator()(const mdns::DnsResourceDataSrv& value) const noexcept {
    size_t result = value.priority_;
    result = (result << 1) ^ value.weight_;
    result = (result << 1) ^ value.port_.as_uint16_t();
    result = (result << 1) ^ std::hash<std::string>{}(value.target_.dotted_string_);
    return result;
  }
};

template <>
struct std::hash<mdns::DnsResourceDataOpt> {
  std::size_t operator()(const mdns::DnsResourceDataOpt& value) const noexcept {
    size_t result = 0;
    for (auto& option : value.options_) {
      result = (result << 1) ^ option;
    }

    return result;
  }
};

template <>
struct std::hash<mdns::DnsResourceDataNSec> {
  std::size_t operator()(const mdns::DnsResourceDataNSec& value) const noexcept {
    size_t result = std::hash<std::string>{}(value.next_domain_.dotted_string_);
    for (auto& bit : value.bits_) {
      result = (result << 1) ^ bit;
    }

    return result;
  }
};

template <>
struct std::hash<mdns::DnsResource> {
  std::size_t operator()(const mdns::DnsResource& resource) const noexcept {
    size_t result = std::hash<std::string>{}(resource.name_.dotted_string_);
    result = (result << 1) ^ static_cast<uint16_t>(resource.type_);
    result = (result << 1) ^ static_cast<uint16_t>(resource.class_);
    result = (result << 1) ^ std::hash<bool>{}(resource.cache_flush_);
    result = (result << 1) ^ std::hash<uint32_t>{}(resource.time_to_live_);

    switch (resource.type_) {
      case mdns::DnsType::kA:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataA>{}(resource.a_);
      case mdns::DnsType::kNs:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataNs>{}(resource.ns_);
      case mdns::DnsType::kCName:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataCName>{}(resource.cname_);
      case mdns::DnsType::kPtr:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataPtr>{}(resource.ptr_);
      case mdns::DnsType::kTxt:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataTxt>{}(resource.txt_);
      case mdns::DnsType::kAaaa:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataAaaa>{}(resource.aaaa_);
      case mdns::DnsType::kSrv:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataSrv>{}(resource.srv_);
      case mdns::DnsType::kOpt:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataOpt>{}(resource.opt_);
      case mdns::DnsType::kNSec:
        return (result << 1) ^ std::hash<mdns::DnsResourceDataNSec>{}(resource.nsec_);
      default:
        return result;
    }
  }
};

#endif  // SRC_CONNECTIVITY_NETWORK_MDNS_SERVICE_DNS_MESSAGE_H_
