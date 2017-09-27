// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "lib/async/cpp/future_value.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/context/fidl/value.fidl.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "peridot/bin/context_engine/context_repository.h"

namespace maxwell {

class ContextRepository;
class ContextValueWriterImpl;

class ContextWriterImpl : ContextWriter {
 public:
  ContextWriterImpl(const ComponentScopePtr& client_info,
                    ContextRepository* repository,
                    fidl::InterfaceRequest<ContextWriter> request);
  ~ContextWriterImpl() override;

  // Takes ownership of |ptr|. Used by ContextWriterImpl and
  // ContextValueWriterImpl.
  void AddContextValueWriter(ContextValueWriterImpl* ptr);

  // Destroys |ptr| and removes it from |value_writer_storage_|. Used by
  // ContextValueWriterImpl on connection error.
  void DestroyContextValueWriter(ContextValueWriterImpl* ptr);

  // Used by ContextValueWriterImpl.
  ContextRepository* repository() const { return repository_; }

 private:
  // |ContextWriter|
  void CreateValue(fidl::InterfaceRequest<ContextValueWriter> request,
                   ContextValueType type) override;

  // |ContextWriter|
  void WriteEntityTopic(const fidl::String& topic,
                        const fidl::String& value) override;

  fidl::Binding<ContextWriter> binding_;

  ContextSelectorPtr parent_value_selector_;
  ContextRepository* const repository_;

  // Supports WriteEntityTopic.
  std::map<fidl::String, ContextRepository::Id> topic_value_ids_;

  // Supports CreateValue().
  std::vector<std::unique_ptr<ContextValueWriterImpl>> value_writer_storage_;
};

class ContextValueWriterImpl : ContextValueWriter {
 public:
  // Binds |request| to |this|, and configures it to call
  // |writer->DestroyContextValueWriter(this)| when a connection error occurs.
  // Assumes that |writer->AddContextValueWriter(this)| has already been
  // called. If |parent_id| is set, the new value will have |parent_id| as its
  // parent value.
  //
  // Does not take ownership of |writer|.
  ContextValueWriterImpl(ContextWriterImpl* writer,
                         const ContextRepository::Id& parent_id,
                         ContextValueType type,
                         fidl::InterfaceRequest<ContextValueWriter> request);
  ~ContextValueWriterImpl() override;

 private:
  // |ContextValueWriter|
  void CreateChildValue(fidl::InterfaceRequest<ContextValueWriter> request,
                        ContextValueType type) override;

  // |ContextValueWriter|
  void Set(const fidl::String& content, ContextMetadataPtr metadata) override;

  fidl::Binding<ContextValueWriter> binding_;

  ContextWriterImpl* const writer_;
  const ContextRepository::Id parent_id_;
  ContextValueType type_;
  FutureValue<ContextRepository::Id> value_id_;
};

}  // namespace maxwell
