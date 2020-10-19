// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fit/variant.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <vector>

#include <test/inspect/validate/cpp/fidl.h>

#include "fuchsia/inspect/cpp/fidl.h"
#include "lib/inspect/service/cpp/service.h"
#include "lib/vfs/cpp/pseudo_dir.h"
#include "lib/vfs/cpp/service.h"

using fit::holds_alternative;
using inspect::LazyNode;
using std::get;
using test::inspect::validate::Action;
using test::inspect::validate::InitializationParams;
using test::inspect::validate::LazyAction;
using test::inspect::validate::LinkDisposition;
using test::inspect::validate::NumberType;
using test::inspect::validate::ROOT_ID;
using test::inspect::validate::TestResult;

using Value =
    fit::variant<fit::monostate, inspect::Node, inspect::IntProperty, inspect::UintProperty,
                 inspect::DoubleProperty, inspect::StringProperty, inspect::ByteVectorProperty,
                 inspect::BoolProperty, inspect::IntArray, inspect::UintArray, inspect::DoubleArray,
                 inspect::LinearIntHistogram, inspect::LinearUintHistogram,
                 inspect::LinearDoubleHistogram, inspect::ExponentialIntHistogram,
                 inspect::ExponentialUintHistogram, inspect::ExponentialDoubleHistogram>;

class Actor {
 public:
  Actor() : inspector_() {}

  Actor(inspect::InspectSettings settings) : inspector_(settings) {}

  inspect::Inspector& inspector() { return inspector_; }

  TestResult Act(const Action& action) {
    switch (action.Which()) {
      case Action::Tag::kCreateNode:
        return HandleCreateNode(action);
      case Action::Tag::kDeleteNode:
        return HandleDeleteNode(action);
      case Action::Tag::kDeleteProperty:
        return HandleDeleteProperty(action);
      case Action::Tag::kCreateNumericProperty:
        return HandleCreateNumeric(action);
      case Action::Tag::kCreateBytesProperty:
        return HandleCreateBytes(action);
      case Action::Tag::kCreateStringProperty:
        return HandleCreateString(action);
      case Action::Tag::kCreateBoolProperty:
        return HandleCreateBool(action);
      case Action::Tag::kSetNumber:
        return HandleSetNumber(action);
      case Action::Tag::kAddNumber:
        return HandleAddNumber(action);
      case Action::Tag::kSubtractNumber:
        return HandleSubtractNumber(action);
      case Action::Tag::kSetString:
        return HandleSetString(action);
      case Action::Tag::kSetBytes:
        return HandleSetBytes(action);
      case Action::Tag::kSetBool:
        return HandleSetBool(action);
      case Action::Tag::kCreateArrayProperty:
        return HandleCreateArray(action);
      case Action::Tag::kArraySet:
        return HandleArraySet(action);
      case Action::Tag::kArrayAdd:
        return HandleArrayAdd(action);
      case Action::Tag::kArraySubtract:
        return HandleArraySubtract(action);
      case Action::Tag::kCreateLinearHistogram:
        return HandleCreateLinearHistogram(action);
      case Action::Tag::kCreateExponentialHistogram:
        return HandleCreateExponentialHistogram(action);
      case Action::Tag::kInsert:
        return HandleInsert(action);
      case Action::Tag::kInsertMultiple:
        return HandleInsertMultiple(action);
      default:
        return TestResult::UNIMPLEMENTED;
    }
  }

  TestResult ActLazy(const LazyAction& lazy_action) {
    switch (lazy_action.Which()) {
      case LazyAction::Tag::kCreateLazyNode:
        return HandleCreateLazyNode(lazy_action);
      case LazyAction::Tag::kDeleteLazyNode:
        return HandleDeleteLazyNode(lazy_action);
      default:
        return TestResult::UNIMPLEMENTED;
    }
  }

 private:
  template <typename T>
  T* GetFromValueMap(uint64_t id) {
    auto it = value_map_.find(id);
    if (it == value_map_.end()) {
      return nullptr;
    }

    if (!holds_alternative<T>(it->second)) {
      return nullptr;
    }

    return &get<T>(it->second);
  }

  inspect::Node* GetNode(uint64_t id) {
    if (id == ROOT_ID) {
      return &inspector_.GetRoot();
    } else {
      return GetFromValueMap<inspect::Node>(id);
    }
  }

  bool ValueMapContains(uint64_t id) { return value_map_.find(id) != value_map_.end(); }

  bool LazyChildrenMapContains(uint64_t id) {
    return lazy_children_map_.find(id) != lazy_children_map_.end();
  }

  // Emplace all currently held Values into the underlying Inspector object.
  // We do this so that when this instance goes of scope, the Value objects don't call their
  // destructors.
  void Freeze() {
    for (auto& [k, v] : value_map_) {
      inspector_.emplace(std::move(v));
    }
    value_map_.clear();
  }

  TestResult HandleCreateNode(const Action& raw_action) {
    auto& action = raw_action.create_node();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    value_map_.emplace(action.id, parent->CreateChild(action.name));

    return TestResult::OK;
  }

  TestResult HandleDeleteNode(const Action& raw_action) {
    auto& action = raw_action.delete_node();

    if (!ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    value_map_.erase(action.id);

    return TestResult::OK;
  }

  TestResult HandleCreateNumeric(const Action& raw_action) {
    auto& action = raw_action.create_numeric_property();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    if (action.value.is_int_t()) {
      value_map_.emplace(action.id, parent->CreateInt(action.name, action.value.int_t()));
    } else if (action.value.is_uint_t()) {
      value_map_.emplace(action.id, parent->CreateUint(action.name, action.value.uint_t()));
    } else if (action.value.is_double_t()) {
      value_map_.emplace(action.id, parent->CreateDouble(action.name, action.value.double_t()));
    } else {
      return TestResult::UNIMPLEMENTED;
    }

    return TestResult::OK;
  }

  TestResult HandleCreateBytes(const Action& raw_action) {
    auto& action = raw_action.create_bytes_property();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    value_map_.emplace(action.id, parent->CreateByteVector(action.name, action.value));

    return TestResult::OK;
  }

  TestResult HandleCreateString(const Action& raw_action) {
    auto& action = raw_action.create_string_property();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    value_map_.emplace(action.id, parent->CreateString(action.name, action.value));

    return TestResult::OK;
  }

  TestResult HandleCreateBool(const Action& raw_action) {
    auto& action = raw_action.create_bool_property();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    value_map_.emplace(action.id, parent->CreateBool(action.name, action.value));

    return TestResult::OK;
  }

  TestResult HandleDeleteProperty(const Action& raw_action) {
    auto& action = raw_action.delete_property();

    if (!ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    value_map_.erase(action.id);

    return TestResult::OK;
  }

  // Helpful macro for deduplicating the 3 operations for numbers (Set, Add, and Subtract).
#define CREATE_NUMERIC_HANDLER(OP_NAME, TYPE_FUNC)                           \
  TestResult Handle##OP_NAME##Number(const Action& raw_action) {             \
    auto& action = raw_action.TYPE_FUNC();                                   \
                                                                             \
    if (action.value.is_int_t()) {                                           \
      if (auto* val = GetFromValueMap<inspect::IntProperty>(action.id)) {    \
        val->OP_NAME(action.value.int_t());                                  \
        return TestResult::OK;                                               \
      }                                                                      \
    } else if (action.value.is_uint_t()) {                                   \
      if (auto* val = GetFromValueMap<inspect::UintProperty>(action.id)) {   \
        val->OP_NAME(action.value.uint_t());                                 \
        return TestResult::OK;                                               \
      }                                                                      \
    } else if (action.value.is_double_t()) {                                 \
      if (auto* val = GetFromValueMap<inspect::DoubleProperty>(action.id)) { \
        val->OP_NAME(action.value.double_t());                               \
        return TestResult::OK;                                               \
      }                                                                      \
    } else {                                                                 \
      return TestResult::UNIMPLEMENTED;                                      \
    }                                                                        \
                                                                             \
    return TestResult::FAILED;                                               \
  }

  CREATE_NUMERIC_HANDLER(Add, add_number)
  CREATE_NUMERIC_HANDLER(Subtract, subtract_number)
  CREATE_NUMERIC_HANDLER(Set, set_number)

  TestResult HandleSetString(const Action& raw_action) {
    auto& action = raw_action.set_string();

    if (auto* val = GetFromValueMap<inspect::StringProperty>(action.id)) {
      val->Set(action.value);
      return TestResult::OK;
    } else {
      return TestResult::FAILED;
    }
  }

  TestResult HandleSetBytes(const Action& raw_action) {
    auto& action = raw_action.set_bytes();

    if (auto* val = GetFromValueMap<inspect::ByteVectorProperty>(action.id)) {
      val->Set(action.value);
      return TestResult::OK;
    } else {
      return TestResult::FAILED;
    }
  }

  TestResult HandleSetBool(const Action& raw_action) {
    auto& action = raw_action.set_bool();

    if (auto* val = GetFromValueMap<inspect::BoolProperty>(action.id)) {
      val->Set(action.value);
      return TestResult::OK;
    } else {
      return TestResult::FAILED;
    }
  }

  TestResult HandleCreateArray(const Action& raw_action) {
    auto& action = raw_action.create_array_property();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    switch (action.number_type) {
      case NumberType::INT:
        value_map_.emplace(action.id, parent->CreateIntArray(action.name, action.slots));
        break;
      case NumberType::UINT:
        value_map_.emplace(action.id, parent->CreateUintArray(action.name, action.slots));
        break;
      case NumberType::DOUBLE:
        value_map_.emplace(action.id, parent->CreateDoubleArray(action.name, action.slots));
        break;
      default:
        return TestResult::UNIMPLEMENTED;
    };

    return TestResult::OK;
  }

  // Helpful macro for deduplicating the 3 operations for arrays (Set, Add, and Subtract).
#define CREATE_ARRAY_HANDLER(OP_NAME, TYPE_FUNC)                          \
  TestResult HandleArray##OP_NAME(const Action& raw_action) {             \
    auto& action = raw_action.TYPE_FUNC();                                \
                                                                          \
    if (action.value.is_int_t()) {                                        \
      if (auto* val = GetFromValueMap<inspect::IntArray>(action.id)) {    \
        val->OP_NAME(action.index, action.value.int_t());                 \
        return TestResult::OK;                                            \
      }                                                                   \
    } else if (action.value.is_uint_t()) {                                \
      if (auto* val = GetFromValueMap<inspect::UintArray>(action.id)) {   \
        val->OP_NAME(action.index, action.value.uint_t());                \
        return TestResult::OK;                                            \
      }                                                                   \
    } else if (action.value.is_double_t()) {                              \
      if (auto* val = GetFromValueMap<inspect::DoubleArray>(action.id)) { \
        val->OP_NAME(action.index, action.value.double_t());              \
        return TestResult::OK;                                            \
      }                                                                   \
    } else {                                                              \
      return TestResult::UNIMPLEMENTED;                                   \
    }                                                                     \
                                                                          \
    return TestResult::FAILED;                                            \
  }

  CREATE_ARRAY_HANDLER(Set, array_set)
  CREATE_ARRAY_HANDLER(Add, array_add)
  CREATE_ARRAY_HANDLER(Subtract, array_subtract)

  TestResult HandleCreateLinearHistogram(const Action& raw_action) {
    auto& action = raw_action.create_linear_histogram();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    if (action.floor.is_int_t()) {
      value_map_.emplace(
          action.id, parent->CreateLinearIntHistogram(action.name, action.floor.int_t(),
                                                      action.step_size.int_t(), action.buckets));
    } else if (action.floor.is_uint_t()) {
      value_map_.emplace(
          action.id, parent->CreateLinearUintHistogram(action.name, action.floor.uint_t(),
                                                       action.step_size.uint_t(), action.buckets));
    } else if (action.floor.is_double_t()) {
      value_map_.emplace(action.id, parent->CreateLinearDoubleHistogram(
                                        action.name, action.floor.double_t(),
                                        action.step_size.double_t(), action.buckets));
    } else {
      return TestResult::UNIMPLEMENTED;
    }

    return TestResult::OK;
  }

  TestResult HandleCreateExponentialHistogram(const Action& raw_action) {
    auto& action = raw_action.create_exponential_histogram();

    if (ValueMapContains(action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    if (action.floor.is_int_t()) {
      value_map_.emplace(action.id,
                         parent->CreateExponentialIntHistogram(
                             action.name, action.floor.int_t(), action.initial_step.int_t(),
                             action.step_multiplier.int_t(), action.buckets));
    } else if (action.floor.is_uint_t()) {
      value_map_.emplace(action.id,
                         parent->CreateExponentialUintHistogram(
                             action.name, action.floor.uint_t(), action.initial_step.uint_t(),
                             action.step_multiplier.uint_t(), action.buckets));
    } else if (action.floor.is_double_t()) {
      value_map_.emplace(action.id,
                         parent->CreateExponentialDoubleHistogram(
                             action.name, action.floor.double_t(), action.initial_step.double_t(),
                             action.step_multiplier.double_t(), action.buckets));
    } else {
      return TestResult::UNIMPLEMENTED;
    }

    return TestResult::OK;
  }

  TestResult HandleInsert(const Action& raw_action) {
    auto& action = raw_action.insert();

    if (action.value.is_int_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearIntHistogram>(action.id)) {
        val->Insert(action.value.int_t());
      } else if (auto* val = GetFromValueMap<inspect::ExponentialIntHistogram>(action.id)) {
        val->Insert(action.value.int_t());
      } else {
        return TestResult::FAILED;
      }
    } else if (action.value.is_uint_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearUintHistogram>(action.id)) {
        val->Insert(action.value.uint_t());
      } else if (auto* val = GetFromValueMap<inspect::ExponentialUintHistogram>(action.id)) {
        val->Insert(action.value.uint_t());
      } else {
        return TestResult::FAILED;
      }
    } else if (action.value.is_double_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearDoubleHistogram>(action.id)) {
        val->Insert(action.value.double_t());
      } else if (auto* val = GetFromValueMap<inspect::ExponentialDoubleHistogram>(action.id)) {
        val->Insert(action.value.double_t());
      } else {
        return TestResult::FAILED;
      }
    } else {
      return TestResult::UNIMPLEMENTED;
    }

    return TestResult::OK;
  }

  TestResult HandleInsertMultiple(const Action& raw_action) {
    auto& action = raw_action.insert_multiple();

    if (action.value.is_int_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearIntHistogram>(action.id)) {
        val->Insert(action.value.int_t(), action.count);
      } else if (auto* val = GetFromValueMap<inspect::ExponentialIntHistogram>(action.id)) {
        val->Insert(action.value.int_t(), action.count);
      } else {
        return TestResult::FAILED;
      }
    } else if (action.value.is_uint_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearUintHistogram>(action.id)) {
        val->Insert(action.value.uint_t(), action.count);
      } else if (auto* val = GetFromValueMap<inspect::ExponentialUintHistogram>(action.id)) {
        val->Insert(action.value.uint_t(), action.count);
      } else {
        return TestResult::FAILED;
      }
    } else if (action.value.is_double_t()) {
      if (auto* val = GetFromValueMap<inspect::LinearDoubleHistogram>(action.id)) {
        val->Insert(action.value.double_t(), action.count);
      } else if (auto* val = GetFromValueMap<inspect::ExponentialDoubleHistogram>(action.id)) {
        val->Insert(action.value.double_t(), action.count);
      } else {
        return TestResult::FAILED;
      }
    } else {
      return TestResult::UNIMPLEMENTED;
    }

    return TestResult::OK;
  }

  TestResult HandleCreateLazyNode(const LazyAction& raw_lazy_action) {
    auto& lazy_action = raw_lazy_action.create_lazy_node();

    if (LazyChildrenMapContains(lazy_action.id)) {
      return TestResult::FAILED;
    }

    auto* parent = GetNode(lazy_action.parent);
    if (!parent) {
      return TestResult::FAILED;
    }

    Actor actor;
    for (const auto& action : lazy_action.actions) {
      actor.Act(action);
    }
    actor.Freeze();
    auto cb = [clone = std::move(actor.inspector())]() { return fit::make_ok_promise(clone); };

    if (lazy_action.disposition == LinkDisposition::CHILD) {
      lazy_children_map_.emplace(lazy_action.id, parent->CreateLazyNode(lazy_action.name, cb));
    } else {
      lazy_children_map_.emplace(lazy_action.id, parent->CreateLazyValues(lazy_action.name, cb));
    }

    return TestResult::OK;
  }

  TestResult HandleDeleteLazyNode(const LazyAction& raw_lazy_action) {
    auto& lazy_action = raw_lazy_action.delete_lazy_node();

    if (!LazyChildrenMapContains(lazy_action.id)) {
      return TestResult::FAILED;
    }

    lazy_children_map_.erase(lazy_action.id);

    return TestResult::OK;
  }

  inspect::Inspector inspector_;
  std::map<uint64_t, Value> value_map_;
  std::map<uint64_t, LazyNode> lazy_children_map_;
};

class Puppet : public test::inspect::validate::Validate {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
    diagnostics_directory_ = context_->outgoing()->GetOrCreateDirectory("diagnostics");
  }

  void Initialize(InitializationParams params, InitializeCallback callback) override {
    if (actor_ != nullptr) {
      callback(zx::vmo(ZX_HANDLE_INVALID), TestResult::ILLEGAL);
      return;
    }
    actor_ = std::make_unique<Actor>(inspect::InspectSettings{.maximum_size = params.vmoSize()});

    if (!bool(actor_->inspector())) {
      callback(zx::vmo(ZX_HANDLE_INVALID), TestResult::FAILED);
    } else {
      callback(actor_->inspector().DuplicateVmo(), TestResult::OK);
    }
  }

  void InitializeTree(InitializationParams params, InitializeTreeCallback callback) override {
    if (actor_ != nullptr) {
      callback(nullptr, TestResult::ILLEGAL);
      return;
    }
    actor_ = std::make_unique<Actor>(inspect::InspectSettings{.maximum_size = params.vmoSize()});

    tree_handler_ = inspect::MakeTreeHandler(&actor_->inspector());
    fuchsia::inspect::TreePtr tree_ptr;
    tree_handler_(tree_ptr.NewRequest());
    callback(std::move(tree_ptr), TestResult::OK);
  }

  void Publish(PublishCallback callback) override {
    if (actor_ == nullptr) {
      callback(TestResult::ILLEGAL);
      return;
    }

    diagnostics_directory_->AddEntry(
        fuchsia::inspect::Tree::Name_,
        std::make_unique<vfs::Service>(inspect::MakeTreeHandler(&actor_->inspector())));
    callback(TestResult::OK);
  }

  void Unpublish(PublishCallback callback) override {
    diagnostics_directory_->RemoveEntry(fuchsia::inspect::Tree::Name_);
    callback(TestResult::OK);
  }

  void Act(Action action, ActCallback callback) override {
    if (actor_ == nullptr) {
      callback(TestResult::ILLEGAL);
    } else {
      callback(actor_->Act(action));
    }
  }

  void ActLazy(LazyAction lazy_action, ActLazyCallback callback) override {
    if (actor_ == nullptr) {
      callback(TestResult::ILLEGAL);
    } else {
      callback(actor_->ActLazy(lazy_action));
    }
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  vfs::PseudoDir* diagnostics_directory_;
  fidl::BindingSet<test::inspect::validate::Validate> bindings_;
  fidl::InterfaceRequestHandler<fuchsia::inspect::Tree> tree_handler_;
  std::unique_ptr<Actor> actor_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::CreateAndServeOutgoingDirectory());

  loop.Run();
  return 0;
}
