// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_WRITER_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_WRITER_IMPL_H_

#include <map>
#include <string>

#include <fuchsia/cpp/modular.h>
#include "lib/async/cpp/future_value.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/lib/bound_set/bound_set.h"

namespace modular {

class ContextRepository;
class ContextValueWriterImpl;
class EntityResolver;

class ContextWriterImpl : ContextWriter {
 public:
  ContextWriterImpl(const ComponentScope& client_info,
                    ContextRepository* repository,
                    modular::EntityResolver* entity_resolver,
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

  // Used by ContextValueWriterImpl.
  void GetEntityTypesFromEntityReference(
      const fidl::StringPtr& reference,
      std::function<void(const fidl::VectorPtr<fidl::StringPtr>&)> done);

 private:
  // |ContextWriter|
  void CreateValue(fidl::InterfaceRequest<ContextValueWriter> request,
                   ContextValueType type) override;

  // |ContextWriter|
  void WriteEntityTopic(fidl::StringPtr topic,
                        fidl::StringPtr value) override;

  fidl::Binding<ContextWriter> binding_;

  ContextSelector parent_value_selector_;
  ContextRepository* const repository_;
  modular::EntityResolver* const entity_resolver_;

  // Supports WriteEntityTopic.
  std::map<fidl::StringPtr, ContextRepository::Id> topic_value_ids_;

  // Supports CreateValue().
  std::vector<std::unique_ptr<ContextValueWriterImpl>> value_writer_storage_;

  // Supports GetEntityTypesFromEntityReference
  //
  // TODO(rosswang): consider adding removal capability to |InterfacePtrSet|
  // instead.
  BoundPtrSet<modular::Entity> entities_;

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
                         fidl::InterfaceRequest<ContextValueWriter> request);
  ~ContextValueWriterImpl() override;

 private:
  // |ContextValueWriter|
  void CreateChildValue(fidl::InterfaceRequest<ContextValueWriter> request,
                        ContextValueType type) override;

  // |ContextValueWriter|
  void Set(fidl::StringPtr content,
           ContextMetadataPtr metadata) override;

  fidl::Binding<ContextValueWriter> binding_;

  ContextWriterImpl* const writer_;
  const ContextRepository::Id parent_id_;
  ContextValueType type_;
  FutureValue<ContextRepository::Id> value_id_;

  fxl::WeakPtrFactory<ContextValueWriterImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextValueWriterImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_WRITER_IMPL_H_
