// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_COMPONENT_ID_INDEX_H_
#define SRC_SYS_APPMGR_COMPONENT_ID_INDEX_H_

#include <lib/fit/result.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/lib/files/unique_fd.h"
#include "src/sys/appmgr/moniker.h"

namespace component {

// ComponentIdIndex provides an API for accessing the component ID index.
//
// Usage:
// - CreateFromAppmgrConfigDir() to create an instance of this class.
// - LookupMoniker() to look up the instance ID of a given moniker.
class ComponentIdIndex : public fbl::RefCounted<ComponentIdIndex> {
 public:
  enum class Error;
  using InstanceId = std::string;
  using MonikerToInstanceId = std::map<Moniker, ComponentIdIndex::InstanceId>;

  // Parses the component id index file from the given |appmgr_config_dir|.
  // If the index file does not exist, an empty index is used.
  //
  // Possible errors:
  //  - ComponentIdIndex::Error::INVALID_JSON
  //  - ComponentIdIndex::Error::INVALID_SCHEMA
  //  - ComponentIdIndex::Error::INVALID_INSTANCE_ID
  //  - ComponentIdIndex::Error::DUPLICATE_INSTANCE_ID
  //  - ComponentIdIndex::Error::DUPLICATE_MONIKER
  static fit::result<fbl::RefPtr<ComponentIdIndex>, Error> CreateFromAppmgrConfigDir(
      const fxl::UniqueFD& appmgr_config_dir);

  // Parses the component id index from the given |index_contents|.
  //
  // Possible errors:
  //  - ComponentIdIndex::Error::INVALID_JSON
  //  - ComponentIdIndex::Error::INVALID_SCHEMA
  //  - ComponentIdIndex::Error::INVALID_INSTANCE_ID
  //  - ComponentIdIndex::Error::DUPLICATE_INSTANCE_ID
  //  - ComponentIdIndex::Error::DUPLICATE_MONIKER
  static fit::result<fbl::RefPtr<ComponentIdIndex>, Error> CreateFromIndexContents(
      const std::string& index_contents);

  // Returns the instance ID of the given moniker if it exists.
  std::optional<InstanceId> LookupMoniker(const Moniker& moniker) const;

  bool restrict_isolated_persistent_storage() const {
    return restrict_isolated_persistent_storage_;
  }

 private:
  // Initialize a ComponentIdIndex with a Moniker->InstanceID mapping. No validation is performed on
  // the supplied |moniker_to_id|. Use the CreateFromAppmgrConfigDir() factory instead.
  ComponentIdIndex(MonikerToInstanceId moniker_to_id, bool restrict_isolated_persistent_storage);

  MonikerToInstanceId moniker_to_id_;

  bool restrict_isolated_persistent_storage_;
};

// Error space used by ComponentIdIndex.
enum class ComponentIdIndex::Error {
  // Index is not valid JSON
  INVALID_JSON,
  // Index does not adhere to the correct JSON schema.
  INVALID_SCHEMA,
  // Instance IDs should be 64 lowercased hex-chars (which represents 256bits)
  INVALID_INSTANCE_ID,
  // The specified moniker must contain a URL string and a non-empty realm path.
  INVALID_MONIKER,
  // There are two index entries for the same instance_id.
  DUPLICATE_INSTANCE_ID,
  // There are two index entries for the same appmgr_moniker.
  DUPLICATE_MONIKER,
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_COMPONENT_ID_INDEX_H_
