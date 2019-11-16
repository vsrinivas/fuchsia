// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/variant.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <vector>

#include <test/inspect/validate/cpp/fidl.h>

using fit::holds_alternative;
using std::get;
using test::inspect::validate::Action;
using test::inspect::validate::InitializationParams;
using test::inspect::validate::NumberType;
using test::inspect::validate::ROOT_ID;
using test::inspect::validate::TestResult;

using Value =
    fit::variant<fit::monostate, inspect::Node, inspect::IntProperty, inspect::UintProperty,
                 inspect::DoubleProperty, inspect::StringProperty, inspect::ByteVectorProperty,
                 inspect::IntArray, inspect::UintArray, inspect::DoubleArray,
                 inspect::LinearIntHistogram, inspect::LinearUintHistogram,
                 inspect::LinearDoubleHistogram, inspect::ExponentialIntHistogram,
                 inspect::ExponentialUintHistogram, inspect::ExponentialDoubleHistogram>;

class Puppet : public test::inspect::validate::Validate {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  void Initialize(InitializationParams params, InitializeCallback callback) {
    value_map_.clear();
    inspector_ = std::make_unique<inspect::Inspector>(
        inspect::InspectSettings{.maximum_size = params.vmoSize()});
    if (inspector_ == nullptr || !bool(*inspector_)) {
      callback(zx::vmo(ZX_HANDLE_INVALID), TestResult::FAILED);
    } else {
      callback(inspector_->DuplicateVmo(), TestResult::OK);
    }
  }

  void Act(Action action, ActCallback callback) {
    switch (action.Which()) {
      case Action::Tag::kCreateNode:
        callback(HandleCreateNode(action));
        break;
      case Action::Tag::kDeleteNode:
        callback(HandleDeleteNode(action));
        break;
      case Action::Tag::kDeleteProperty:
        callback(HandleDeleteProperty(action));
        break;
      case Action::Tag::kCreateNumericProperty:
        callback(HandleCreateNumeric(action));
        break;
      case Action::Tag::kCreateBytesProperty:
        callback(HandleCreateBytes(action));
        break;
      case Action::Tag::kCreateStringProperty:
        callback(HandleCreateString(action));
        break;
      case Action::Tag::kSetNumber:
        callback(HandleSetNumber(action));
        break;
      case Action::Tag::kAddNumber:
        callback(HandleAddNumber(action));
        break;
      case Action::Tag::kSubtractNumber:
        callback(HandleSubtractNumber(action));
        break;
      case Action::Tag::kSetString:
        callback(HandleSetString(action));
        break;
      case Action::Tag::kSetBytes:
        callback(HandleSetBytes(action));
        break;
      case Action::Tag::kCreateArrayProperty:
        callback(HandleCreateArray(action));
        break;
      case Action::Tag::kArraySet:
        callback(HandleArraySet(action));
        break;
      case Action::Tag::kArrayAdd:
        callback(HandleArrayAdd(action));
        break;
      case Action::Tag::kArraySubtract:
        callback(HandleArraySubtract(action));
        break;
      case Action::Tag::kCreateLinearHistogram:
        callback(HandleCreateLinearHistogram(action));
        break;
      case Action::Tag::kCreateExponentialHistogram:
        callback(HandleCreateExponentialHistogram(action));
        break;
      case Action::Tag::kInsert:
        callback(HandleInsert(action));
        break;
      case Action::Tag::kInsertMultiple:
        callback(HandleInsertMultiple(action));
        break;
      default:
        callback(TestResult::UNIMPLEMENTED);
        break;
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
      return &inspector_->GetRoot();
    } else {
      return GetFromValueMap<inspect::Node>(id);
    }
  }

  bool ValueMapContains(uint64_t id) { return value_map_.find(id) != value_map_.end(); }

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

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<test::inspect::validate::Validate> bindings_;
  std::map<uint64_t, Value> value_map_;
  std::unique_ptr<inspect::Inspector> inspector_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::Create());

  loop.Run();
  return 0;
}
