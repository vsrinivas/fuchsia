// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"

namespace btlib {
namespace sdp {

// A ServiceRecord represents a service record in a SDP database.
// The service has a number of attributes identified by defined IDs and each
// attribute has a value.
class ServiceRecord {
 public:
  // Create a new service record with the handle given.
  // Also generates a UUID and sets the Service ID attribute.
  explicit ServiceRecord(ServiceHandle handle);

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceRecord);

  // Directly sets an attribute to a specific DataElement
  void SetAttribute(AttributeId id, DataElement value);

  // Get the value of an attribute. The attribute must be set.
  // Use HasAttribute() to detect if an attribute is set.
  const DataElement& GetAttribute(AttributeId id) const;

  // Returns true if there is an attribute with |id| in this record.
  bool HasAttribute(AttributeId id) const;

  // Removes the attribute identified by |id|. Idempotent.
  void RemoveAttribute(AttributeId id);

  // Returns the handle of this service.
  ServiceHandle handle() const { return handle_; }

  // Fills a DataElement sequence conisting of alternating attribute IDs and
  // attribute value DataElements, if they are present.  The attributes are
  // ordered by attribute ID in ascentind order.
  // If no attributes are present, returns a DataElement sequence with no
  // elements.
  DataElement GetAttributes(
      const std::unordered_set<AttributeId>& attributes) const;

  // Returns true if any value of the attributes in this service contain all
  // of the |uuids| given.  The uuids need not be in any specific attribute
  // value.
  bool FindUUID(const std::unordered_set<common::UUID>& uuids) const;

  // Convenience function to set the service class id list attribute.
  void SetServiceClassUUIDs(const std::vector<common::UUID>& classes);

  using ProtocolListId = uint8_t;

  constexpr static ProtocolListId kPrimaryProtocolList = 0x00;

  // Adds a protocol to a protocol descriptor list.
  // Convenience function for adding protocol discriptor list attributes.
  // |id| identifies the list to be added to.
  // |uuid| must be a protocol UUID.
  // |params| is either:
  //   - a DataElement sequence of parameters
  //   - a null DataElement, for which nothing will be appended
  //   - a single DataElement parameter
  // kPrimaryProtocolList is presented as the primary protocol.
  // Other protocol will be added to the addiitonal protocol lists,
  void AddProtocolDescriptor(const ProtocolListId id, const common::UUID& uuid,
                             DataElement params);

  // Adds a profile to the bluetooth profile descrpitor list attribute.
  // |uuid| is the UUID of the profile. |major| and |minor| are the major and
  // minor versions of the profile supported.
  void AddProfile(const common::UUID& uuid, uint8_t major, uint8_t minor);

  // Adds a set of language attributes.
  // |language| is required (and must be two characters long)
  // At least one other attribute must be non-empty.
  // Empty attributes will be omitted.
  // All strings are UTF-8 encoded.
  // Returns true if attributes were added, false otherwise.
  bool AddInfo(const std::string& language_code, const std::string& name,
               const std::string& description, const std::string& provider);

  // Set the security level required to connect to this service.
  // See v5.0, Vol 3, Part C, Section 5.2.2.8
  void set_security_level(SecurityLevel security_level) {
    security_level_ = security_level;
  }
  SecurityLevel security_level() const { return security_level_; }

 private:
  ServiceHandle handle_;

  std::unordered_map<AttributeId, DataElement> attributes_;

  // Additional protocol lists, by id.
  // Each one of these elements is a sequence of the form that would
  // a protocol list (a sequence of sequences of protocols and params)
  std::unordered_map<ProtocolListId, DataElement> addl_protocols_;

  SecurityLevel security_level_;
};

}  // namespace sdp
}  // namespace btlib
