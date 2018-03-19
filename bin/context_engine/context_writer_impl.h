// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "lib/async/cpp/future_value.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/context/fidl/value.fidl.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "peridot/bin/context_engine/context_repository.h"

namespace modular {
class EntityResolver;
}

namespace maxwell {

class ContextRepository;
class ContextValueWriterImpl;

class ContextWriterImpl : ContextWriter {
 public:
  ContextWriterImpl(const ComponentScopePtr& client_info,
                    ContextRepository* repository,
                    modular::EntityResolver* entity_resolver,
                    f1dl::InterfaceRequest<ContextWriter> request);
  ~ContextWriterImpl() override;

  // Takes ownership of |ptr|. Used by ContextWriterImpl and
  // ContextValueWriterImpl.
  void AddContextValueWriter(ContextValueWriterImpl* ptr);

  // Destroys |ptr| and removes it from |value_writer_storage_|. Used by
  // ContextValueWriterImpl on connection error.
  void DestroyContextValueWriter(ContextValueWriterImpl* ptr);

  // Used by ContextValueWriterImpl.
  ContextRepository* repository() const { return repository_; }

  // Used by ContextValueWriterImpl.
  void GetEntityTypesFromEntityReference(
      const f1dl::StringPtr& reference,
      std::function<void(const f1dl::VectorPtr<f1dl::StringPtr>&)> done);

 private:
  // |ContextWriter|
  void CreateValue(f1dl::InterfaceRequest<ContextValueWriter> request,
                   ContextValueType type) override;

  // |ContextWriter|
  void WriteEntityTopic(const f1dl::StringPtr& topic,
                        const f1dl::StringPtr& value) override;

  f1dl::Binding<ContextWriter> binding_;

  ContextSelectorPtr parent_value_selector_;
  ContextRepository* const repository_;
  modular::EntityResolver* const entity_resolver_;

  // Supports WriteEntityTopic.
  std::map<f1dl::StringPtr, ContextRepository::Id> topic_value_ids_;

  // Supports CreateValue().
  std::vector<std::unique_ptr<ContextValueWriterImpl>> value_writer_storage_;

  fxl::WeakPtrFactory<ContextWriterImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextWriterImpl);
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
                         f1dl::InterfaceRequest<ContextValueWriter> request);
  ~ContextValueWriterImpl() override;

 private:
  // |ContextValueWriter|
  void CreateChildValue(f1dl::InterfaceRequest<ContextValueWriter> request,
                        ContextValueType type) override;

  // |ContextValueWriter|
  void Set(const f1dl::StringPtr& content, ContextMetadataPtr metadata) override;

  f1dl::Binding<ContextValueWriter> binding_;

  ContextWriterImpl* const writer_;
  const ContextRepository::Id parent_id_;
  ContextValueType type_;
  FutureValue<ContextRepository::Id> value_id_;

  fxl::WeakPtrFactory<ContextValueWriterImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextValueWriterImpl);
};

}  // namespace maxwell
