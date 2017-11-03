// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This class maintains an inverted index for ContextMetadata structs.
// It helps answer the question "what objects have metadata that matches
// these key/value pairs" very efficiently.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_INDEX_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_INDEX_H_

#include <map>
#include <set>
#include <string>
#include <map>

#include "lib/context/fidl/metadata.fidl.h"
#include "lib/context/fidl/value.fidl.h"

namespace maxwell {

namespace internal {

// Returns a set of strings which encode both the keys and values in |metadata|
// and |type| for use in an inverted index.
std::set<std::string> EncodeMetadataAndType(ContextValueType type,
                                            const ContextMetadataPtr& metadata);

}  // namespace internal

class ContextIndex {
 public:
  // TODO(thatguy): Move this enum into context_repository.cc.
  using Id = std::string;

  void Add(Id id, ContextValueType type, const ContextMetadataPtr& metadata);
  void Remove(Id id, ContextValueType type, const ContextMetadataPtr& metadata);

  // Intersects the ids in |out| with those of type |type| and match every
  // field in |metadata|.
  void Query(ContextValueType type,
             const ContextMetadataPtr& metadata,
             std::set<Id>* out);

 private:
  // A posting list from encoded value list to ids.
  std::map<std::string, std::set<Id>> index_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_INDEX_H_
