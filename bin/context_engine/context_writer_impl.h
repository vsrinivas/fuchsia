// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "apps/maxwell/lib/async/future_value.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/maxwell/services/context/value.fidl.h"
#include "apps/maxwell/services/user/scope.fidl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

class ContextRepository;

class ContextWriterImpl : ContextWriter {
 public:
  ContextWriterImpl(const ComponentScopePtr& client_info,
                    ContextRepository* repository,
                    fidl::InterfaceRequest<ContextWriter> request);
  ~ContextWriterImpl() override;

 private:
  // |ContextWriter|
  void AddValue(ContextValuePtr value, const AddValueCallback& done) override;

  // |ContextWriter|
  void AddChildValue(const fidl::String& parent_id,
                     ContextValuePtr value,
                     const AddChildValueCallback& done) override;

  // |ContextWriter|
  void UpdateMetadata(const fidl::String& id,
                      ContextMetadataPtr metadata) override;

  // |ContextWriter|
  void UpdateContent(const fidl::String& id,
                     const fidl::String& content) override;

  // |ContextWriter|
  void Update(const fidl::String& id, ContextValuePtr value) override;

  // |ContextWriter|
  void WriteEntityTopic(const fidl::String& topic,
                        const fidl::String& value) override;

  fidl::Binding<ContextWriter> binding_;

  ContextSelectorPtr parent_value_selector_;
  ContextRepository* const repository_;

  // Supports WriteEntityTopic.
  std::map<fidl::String, ContextRepository::Id> topic_value_ids_;
};

}  // namespace maxwell
