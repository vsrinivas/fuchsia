// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/mdns/mdns_names.h"

#include "lib/fxl/logging.h"

namespace netconnector {
namespace mdns {

namespace {

static const std::string kLocalDomainName = "local.";
static const std::string kSubtypeSeparator = "._sub.";
static const std::string kTcpSuffix = "._tcp.";
static const std::string kUdpSuffix = "._udp.";

// Checks for valid host, instance or subtype name.
bool IsValidOtherName(const std::string& name) {
  return !name.empty() && name[name.size() - 1] != '.';
}

// Concatenates |strings|.
std::string Concatenate(std::initializer_list<std::string> strings) {
  std::string result;
  size_t result_size = 0;

  for (auto& string : strings) {
    result_size += string.size();
  }

  result.reserve(result_size);

  for (auto& string : strings) {
    result.append(string);
  }

  return result;
}

// Determines if |left| occurs in |name| starting at |*index_in_out|. If so,
// adds |left.size()| to |*index_in_out| and returns true. Otherwise leaves
// |*index_in_out| unchanged and returns false. This function is useful for
// scanning strings from left to right where |*index_in_out| is initially 0.
bool MatchLeft(const std::string& name,
               const std::string& left,
               size_t* index_in_out) {
  FXL_DCHECK(index_in_out);
  size_t index = *index_in_out;

  if (name.compare(index, left.size(), left) != 0) {
    return false;
  }

  *index_in_out = index + left.size();

  return true;
}

// Determines if |right| occurs in |name| immediately before |*index_in_out|.
// If so, subtracts |right.size()| from |*index_in_out| and returns true.
// Otherwise leaves |*index_in_out| unchanged and returns false. This function
// is useful for scanning strings from right to left where |*index_in_out| is
// initially |name.size()|.
bool MatchRight(const std::string& name,
                const std::string& right,
                size_t* index_in_out) {
  FXL_DCHECK(index_in_out);
  size_t index = *index_in_out;

  if (index < right.size()) {
    return false;
  }

  index -= right.size();

  if (name.compare(index, right.size(), right) != 0) {
    return false;
  }

  *index_in_out = index;

  return true;
}

}  // namespace

// static
std::string MdnsNames::LocalHostFullName(const std::string& host_name) {
  FXL_DCHECK(IsValidOtherName(host_name));

  return Concatenate({host_name, ".", kLocalDomainName});
}

// static
std::string MdnsNames::LocalServiceFullName(const std::string& service_name) {
  FXL_DCHECK(IsValidServiceName(service_name));

  return Concatenate({service_name, kLocalDomainName});
}

// static
std::string MdnsNames::LocalServiceSubtypeFullName(
    const std::string& service_name,
    const std::string& subtype) {
  FXL_DCHECK(IsValidServiceName(service_name));
  FXL_DCHECK(IsValidOtherName(subtype));

  return Concatenate(
      {subtype, kSubtypeSeparator, service_name, kLocalDomainName});
}

// static
std::string MdnsNames::LocalInstanceFullName(const std::string& instance_name,
                                             const std::string& service_name) {
  FXL_DCHECK(IsValidOtherName(instance_name));
  FXL_DCHECK(IsValidServiceName(service_name));

  return Concatenate({instance_name, ".", service_name, kLocalDomainName});
}

// static
std::string MdnsNames::LocalInstanceSubtypeFullName(
    const std::string& instance_name,
    const std::string& service_name,
    const std::string& subtype) {
  FXL_DCHECK(IsValidOtherName(instance_name));
  FXL_DCHECK(IsValidServiceName(service_name));
  FXL_DCHECK(IsValidOtherName(subtype));

  return Concatenate({instance_name, ".", subtype, kSubtypeSeparator,
                      service_name, kLocalDomainName});
}

// static
bool MdnsNames::ExtractInstanceName(const std::string& instance_full_name,
                                    const std::string& service_name,
                                    std::string* instance_name) {
  FXL_DCHECK(instance_name);

  // instance_name "." service_name kLocalDomainName

  size_t index = instance_full_name.size();
  if (!MatchRight(instance_full_name, kLocalDomainName, &index) ||
      !MatchRight(instance_full_name, service_name, &index) ||
      !MatchRight(instance_full_name, ".", &index) || index == 0) {
    return false;
  }

  *instance_name = instance_full_name.substr(0, index);

  return true;
}

// static
bool MdnsNames::MatchServiceName(const std::string& name,
                                 const std::string& service_name,
                                 std::string* subtype_out) {
  FXL_DCHECK(subtype_out);

  // [ subtype kSubtypeSeparator ] service_name kLocalDomainName

  size_t index = name.size();
  if (!MatchRight(name, kLocalDomainName, &index) ||
      !MatchRight(name, service_name, &index)) {
    return false;
  }

  if (index == 0) {
    *subtype_out = "";
    return true;
  }

  if (!MatchRight(name, kSubtypeSeparator, &index) || index == 0) {
    return false;
  }

  *subtype_out = name.substr(0, index);
  return true;
}

// static
bool MdnsNames::MatchInstanceName(const std::string& name,
                                  const std::string& instance_name,
                                  const std::string& service_name,
                                  std::string* subtype_out) {
  FXL_DCHECK(subtype_out);

  // instance_name "." [ subtype, kSubtypeSeparator ] service_name
  // kLocalDomainName

  size_t left_index = 0;
  if (!MatchLeft(name, instance_name, &left_index) ||
      !MatchLeft(name, ".", &left_index)) {
    return false;
  }

  size_t right_index = name.size();
  if (!MatchRight(name, kLocalDomainName, &right_index) ||
      !MatchRight(name, service_name, &right_index) ||
      left_index > right_index) {
    return false;
  }

  if (left_index == right_index) {
    *subtype_out = "";
    return true;
  }

  if (!MatchRight(name, kSubtypeSeparator, &right_index) ||
      left_index >= right_index) {
    return false;
  }

  *subtype_out = name.substr(left_index, right_index - left_index);
  return true;
}

// static
bool MdnsNames::IsValidHostName(const std::string& host_name) {
  return IsValidOtherName(host_name);
}

// static
bool MdnsNames::IsValidServiceName(const std::string& service_name) {
  size_t index = service_name.size();

  if (index == 0 || service_name[0] != '_') {
    return false;
  }

  return MatchRight(service_name, kTcpSuffix, &index) ||
         MatchRight(service_name, kUdpSuffix, &index);
}

// static
bool MdnsNames::IsValidInstanceName(const std::string& instance_name) {
  return IsValidOtherName(instance_name);
}

}  // namespace mdns
}  // namespace netconnector
