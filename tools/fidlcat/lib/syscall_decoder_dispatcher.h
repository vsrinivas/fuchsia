// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/display_options.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "tools/fidlcat/lib/comparator.h"
#include "tools/fidlcat/lib/decode_options.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/exception_decoder.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/syscall_decoder.h"
#include "tools/fidlcat/lib/type_decoder.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

const fidl_codec::Struct& GetUint128StructDefinition();
std::unique_ptr<fidl_codec::Type> SyscallTypeToFidlCodecType(fidlcat::SyscallType);

template <typename ClassType, typename Type>
class ClassField;

template <typename ClassType>
class Class;

// Base class for all conditions on fields.
template <typename ClassType>
class ClassFieldConditionBase {
 public:
  ClassFieldConditionBase() = default;
  virtual ~ClassFieldConditionBase() = default;

  // Returns true if the condition is true.
  virtual bool True(const ClassType* object, debug::Arch /*arch*/) = 0;
};

// Condition which checks that the field has an expected value.
template <typename ClassType, typename Type>
class ClassFieldCondition : public ClassFieldConditionBase<ClassType> {
 public:
  ClassFieldCondition(const ClassField<ClassType, Type>* field, Type value)
      : field_(field), value_(value) {}

  bool True(const ClassType* object, debug::Arch /*arch*/) override;

 private:
  // The field we check.
  const ClassField<ClassType, Type>* const field_;
  // The value we expect.
  const Type value_;
};

// Condition which checks that the masked field has an expected value.
template <typename ClassType, typename Type>
class ClassFieldMaskedCondition : public ClassFieldConditionBase<ClassType> {
 public:
  ClassFieldMaskedCondition(const ClassField<ClassType, Type>* field, Type mask, Type value)
      : field_(field), mask_(mask), value_(value) {}

  bool True(const ClassType* object, debug::Arch /*arch*/) override;

 private:
  // The field we check.
  const ClassField<ClassType, Type>* const field_;
  // The mask to apply to the field.
  const Type mask_;
  // The value we expect.
  const Type value_;
};

// Condition which checks that the architecture has an expected value.
template <typename ClassType, typename Type>
class ArchCondition : public ClassFieldConditionBase<ClassType> {
 public:
  explicit ArchCondition(debug::Arch arch) : arch_(arch) {}

  bool True(const ClassType* object, debug::Arch /*arch*/) override;

 private:
  // The architecture we check.
  const debug::Arch arch_;
};

// Base class for all class fields.
template <typename ClassType>
class ClassFieldBase {
 public:
  ClassFieldBase(std::string_view name, SyscallType syscall_type)
      : name_(name), syscall_type_(syscall_type) {}
  virtual ~ClassFieldBase() = default;

  const std::string& name() const { return name_; }
  SyscallType syscall_type() const { return syscall_type_; }

  // Add a condition which must be true to display the input/output.
  template <typename Type>
  ClassFieldBase<ClassType>* DisplayIfEqual(const ClassField<ClassType, Type>* field, Type value) {
    conditions_.push_back(std::make_unique<ClassFieldCondition<ClassType, Type>>(field, value));
    return this;
  }

  // Add a condition which must be true to display the input/output.
  template <typename Type>
  ClassFieldBase<ClassType>* DisplayIfMaskedEqual(const ClassField<ClassType, Type>* field,
                                                  Type mask, Type value) {
    conditions_.push_back(
        std::make_unique<ClassFieldMaskedCondition<ClassType, Type>>(field, mask, value));
    return this;
  }

  // Define the architecture needed to display the input/output.
  ClassFieldBase<ClassType>* DisplayIfArch(debug::Arch arch) {
    conditions_.push_back(std::make_unique<ArchCondition<ClassType, uint8_t>>(arch));
    return this;
  }

  bool ConditionsAreTrue(const ClassType* object, debug::Arch arch) {
    for (const auto& condition : conditions_) {
      if (!condition->True(object, arch)) {
        return false;
      }
    }
    return true;
  }

  virtual std::unique_ptr<fidl_codec::Type> ComputeType() const {
    return std::make_unique<fidl_codec::InvalidType>();
  }

  virtual std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                           debug::Arch arch) const = 0;

  uint8_t id() const { return id_; }

  ClassFieldBase<ClassType>* SetId(uint8_t id) {
    id_ = id;
    return this;
  }

 private:
  std::string name_;
  const SyscallType syscall_type_;
  std::vector<std::unique_ptr<ClassFieldConditionBase<ClassType>>> conditions_;
  uint8_t id_ = 0;
};

// Define a class field for basic types.
template <typename ClassType, typename Type>
class ClassField : public ClassFieldBase<ClassType> {
 public:
  ClassField(std::string_view name, SyscallType syscall_type, Type (*get)(const ClassType* from))
      : ClassFieldBase<ClassType>(name, syscall_type), get_(get) {}

  Type (*get() const)(const ClassType* from) { return get_; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override;

 private:
  // Function which can extract the value of the field for a given object.
  Type (*get_)(const ClassType* from);
};

// Define a class field which is an array of base type items.
template <typename ClassType, typename Type>
class ArrayField : public ClassFieldBase<ClassType> {
 public:
  ArrayField(std::string_view name, SyscallType syscall_type,
             std::pair<const Type*, int> (*get)(const ClassType* from))
      : ClassFieldBase<ClassType>(name, syscall_type), get_(get) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    auto type = SyscallTypeToFidlCodecType(this->syscall_type());
    ClassType dummy;
    std::pair<const Type*, int> result = (*get_)(&dummy);
    int fixed_size = result.second;
    return std::make_unique<fidl_codec::ArrayType>(std::move(type), fixed_size);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override;

 private:
  // Function which can extract the address of the field for a given object.
  std::pair<const Type*, int> (*get_)(const ClassType* from);
};

// Define a class field which is an array of base type items. The size of the array is dynamic.
template <typename ClassType, typename Type, typename SizeType>
class DynamicArrayField : public ClassFieldBase<ClassType> {
 public:
  DynamicArrayField(std::string_view name, SyscallType syscall_type,
                    std::pair<const Type*, SizeType> (*get)(const ClassType* from))
      : ClassFieldBase<ClassType>(name, syscall_type), get_(get) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    auto type = SyscallTypeToFidlCodecType(this->syscall_type());
    return std::make_unique<fidl_codec::VectorType>(std::move(type));
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override;

 private:
  // Function which can extract the address of the field for a given object.
  std::pair<const Type*, SizeType> (*get_)(const ClassType* from);
};

// Define a class field which is a class.
template <typename ClassType, typename Type>
class ClassClassField : public ClassFieldBase<ClassType> {
 public:
  ClassClassField(std::string_view name, const Type* (*get)(const ClassType* from),
                  const Class<Type>* field_class)
      : ClassFieldBase<ClassType>(name, SyscallType::kStruct),
        get_(get),
        field_class_(field_class) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return field_class_->ComputeType();
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override {
    return field_class_->GenerateValue(get_(object), arch);
  }

 private:
  // Function which can extract the address of the field for a given object.
  const Type* (*get_)(const ClassType* from);
  // Definition of the field's class.
  const Class<Type>* const field_class_;
};

// Define a class field which is an array of objects.
template <typename ClassType, typename Type>
class ArrayClassField : public ClassFieldBase<ClassType> {
 public:
  ArrayClassField(std::string_view name, std::pair<const Type*, int> (*get)(const ClassType* from),
                  const Class<Type>* sub_class)
      : ClassFieldBase<ClassType>(name, SyscallType::kStruct), get_(get), sub_class_(sub_class) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    auto type = sub_class_->ComputeType();
    ClassType dummy;
    std::pair<const Type*, int> result = (*get_)(&dummy);
    int fixed_size = result.second;
    return std::make_unique<fidl_codec::ArrayType>(std::move(type), fixed_size);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override;

 private:
  // Function which can extract the address of the field for a given object.
  std::pair<const Type*, int> (*get_)(const ClassType* from);
  // Definition of the array items' class.
  const Class<Type>* const sub_class_;
};

// Define a class field which is an array of objects. The size of the array is dynamic.
template <typename ClassType, typename Type>
class DynamicArrayClassField : public ClassFieldBase<ClassType> {
 public:
  DynamicArrayClassField(std::string_view name, const Type* (*get)(const ClassType* from),
                         uint32_t (*get_size)(const ClassType* from), const Class<Type>* sub_class)
      : ClassFieldBase<ClassType>(name, SyscallType::kStruct),
        get_(get),
        get_size_(get_size),
        sub_class_(sub_class) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    auto type = sub_class_->ComputeType();
    return std::make_unique<fidl_codec::VectorType>(std::move(type));
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const override;

 private:
  // Function which can extract the address of the field for a given object.
  const Type* (*get_)(const ClassType* from);
  // Function which can extract the size of the array for a given object.
  uint32_t (*get_size_)(const ClassType* from);
  // Definition of the array items' class.
  const Class<Type>* const sub_class_;
};

// Define a class.
template <typename ClassType>
class Class {
 public:
  const std::string& name() const { return name_; }

  const std::vector<std::unique_ptr<ClassFieldBase<ClassType>>>& fields() const { return fields_; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const {
    return std::make_unique<fidl_codec::StructType>(struct_definition_, true);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug::Arch arch) const {
    if (object == nullptr) {
      return std::make_unique<fidl_codec::NullValue>();
    }

    auto struct_value = std::make_unique<fidl_codec::StructValue>(struct_definition_);
    for (const auto& field : fields()) {
      if (field->ConditionsAreTrue(object, arch)) {
        std::unique_ptr<fidl_codec::Value> val = field->GenerateValue(object, arch);
        struct_value->AddField(field->name(), field->id(), std::move(val));
      }
    }
    return std::move(struct_value);
  }

  template <typename Type>
  ClassField<ClassType, Type>* AddField(std::unique_ptr<ClassField<ClassType, Type>> field,
                                        uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ArrayField<ClassType, Type>* AddField(std::unique_ptr<ArrayField<ClassType, Type>> field,
                                        uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type, typename SizeType>
  DynamicArrayField<ClassType, Type, SizeType>* AddField(
      std::unique_ptr<DynamicArrayField<ClassType, Type, SizeType>> field, uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ClassClassField<ClassType, Type>* AddField(
      std::unique_ptr<ClassClassField<ClassType, Type>> field, uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ArrayClassField<ClassType, Type>* AddField(
      std::unique_ptr<ArrayClassField<ClassType, Type>> field, uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  DynamicArrayClassField<ClassType, Type>* AddField(
      std::unique_ptr<DynamicArrayClassField<ClassType, Type>> field, uint8_t id = 0) {
    auto result = field.get();
    result->SetId(id);
    AddFieldToStructDefinition(*field, id);
    fields_.push_back(std::move(field));
    return result;
  }

 protected:
  explicit Class(std::string_view name) : name_(name), struct_definition_(name) {}
  Class(const Class&) = delete;
  Class& operator=(const Class&) = delete;

 private:
  void AddFieldToStructDefinition(ClassFieldBase<ClassType>& field, uint8_t id) {
    auto type = field.ComputeType();
    struct_definition_.AddMember(field.name(), std::move(type), id);
  }
  // Name of the class.
  std::string name_;
  // List of all fields in the class. Some fields can be specified several times
  // with different conditions.
  std::vector<std::unique_ptr<ClassFieldBase<ClassType>>> fields_;
  // Struct definition for the generated value.
  fidl_codec::Struct struct_definition_;
};

// Base class (not templated) for system call arguments.
class SyscallArgumentBase {
 public:
  SyscallArgumentBase(int index, SyscallType syscall_type)
      : index_(index), syscall_type_(syscall_type) {}
  virtual ~SyscallArgumentBase() = default;

  int index() const { return index_; }
  SyscallType syscall_type() const { return syscall_type_; }

 private:
  const int index_;
  const SyscallType syscall_type_;
};

template <typename Type>
class SyscallArgumentBaseTyped : public SyscallArgumentBase {
 public:
  SyscallArgumentBaseTyped(int index, SyscallType syscall_type)
      : SyscallArgumentBase(index, syscall_type) {}

  // Ensures that the argument data will be in memory.
  virtual void Load(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const {}

  // True if the argument data is available.
  virtual bool Loaded(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const { return false; }

  // True if the argument data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const {
    return false;
  }

  // The data for the argument.
  virtual Type Value(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const { return Type(); }

  // For buffers, ensures that the buffer will be in memory.
  virtual void LoadArray(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/,
                         size_t /*size*/) const {}

  // For buffers, true if the buffer is available.
  virtual bool ArrayLoaded(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/,
                           size_t /*size*/) const {
    return false;
  }

  // For buffers, get a pointer on the buffer data.
  virtual Type* Content(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const {
    return nullptr;
  }

  virtual debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const {
    return debug_ipc::AutomationOperand();
  }
};

// Defines an basic type argument for a system call.
// A basic type argument can be stored in a 64 bit register.
template <typename Type>
class SyscallArgument : public SyscallArgumentBaseTyped<Type> {
 public:
  SyscallArgument(int index, SyscallType syscall_type)
      : SyscallArgumentBaseTyped<Type>(index, syscall_type) {}

  // Redefine index within the class to avoid a compiler error.
  int index() const { return SyscallArgumentBase::index(); }

  bool Loaded(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override { return true; }

  bool ValueValid(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {
    return true;
  }

  Type Value(SyscallDecoderInterface* decoder, Stage /*stage*/) const override {
    return Type(decoder->ArgumentValue(index()));
  }

  debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const override {
    debug_ipc::AutomationOperand operand;
    if (static_cast<uint64_t>(index()) < argument_indexes.size()) {
      operand.InitRegister(argument_indexes[index()]);
    } else {
      // This will only happen on X64 when we have more than 6 arguments. The last two arguments are
      // placed on the stack.
      operand.InitStackSlot((index() - argument_indexes.size()) * 8);
    }
    return operand;
  }
};

// Defines a buffer argument for a system call.
// A buffer argument is defined by a pointer which can be stored in a 64 bit
// register. The data for the buffer stays in memory (referenced by the
// pointer).
template <typename Type>
class SyscallPointerArgument : public SyscallArgumentBaseTyped<Type> {
 public:
  SyscallPointerArgument(int index, SyscallType syscall_type)
      : SyscallArgumentBaseTyped<Type>(index, syscall_type) {}

  int index() const { return SyscallArgumentBase::index(); }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    decoder->LoadArgument(stage, index(), sizeof(Type));
  }

  bool Loaded(SyscallDecoderInterface* decoder, Stage stage) const override {
    return decoder->ArgumentLoaded(stage, index(), sizeof(Type));
  }

  bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const override {
    return decoder->ArgumentContent(stage, index()) != nullptr;
  }

  Type Value(SyscallDecoderInterface* decoder, Stage stage) const override {
    uint8_t* content = decoder->ArgumentContent(stage, index());
    if (content == nullptr) {
      return Type();
    }
    return *reinterpret_cast<Type*>(content);
  }

  void LoadArray(SyscallDecoderInterface* decoder, Stage stage, size_t size) const override {
    decoder->LoadArgument(stage, index(), size);
  }

  bool ArrayLoaded(SyscallDecoderInterface* decoder, Stage stage, size_t size) const override {
    return decoder->ArgumentLoaded(stage, index(), size);
  }

  Type* Content(SyscallDecoderInterface* decoder, Stage stage) const override {
    return reinterpret_cast<Type*>(decoder->ArgumentContent(stage, index()));
  }

  debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const override {
    debug_ipc::AutomationOperand operand;
    if (static_cast<uint64_t>(index()) < argument_indexes.size()) {
      operand.InitRegister(argument_indexes[index()]);
    } else {
      // This will only happen on X64 when we have more than 6 arguments. The last two arguments are
      // placed on the stack.
      operand.InitStackSlot((index() - argument_indexes.size()) * 8);
    }
    return operand;
  }
};

// Base class for all data accesses.
class AccessBase {
 public:
  AccessBase() = default;
  virtual ~AccessBase() = default;

  // Returns the real type of the data (because, for example, handles are
  // implemented as uint32_t).
  virtual SyscallType GetSyscallType() const = 0;

  // Computes the fidl codec type for this access. Currently, we are not able to compute it for all
  // the cases. When we are not able to compute it, this method returns null.
  std::unique_ptr<fidl_codec::Type> ComputeType() const;

  // For buffers, ensures that the buffer will be in memory.
  virtual void LoadArray(SyscallDecoderInterface* decoder, Stage stage, size_t size) = 0;

  // For buffers, true if the buffer is available.
  virtual bool ArrayLoaded(SyscallDecoderInterface* decoder, Stage stage, size_t size) const = 0;

  // For buffers, get a pointer on the buffer data.
  virtual const uint8_t* Uint8Content(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // Returns the automation operand that will load the value of this access.
  virtual debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const = 0;
};

// Use to access data for an input or an output.
template <typename Type>
class Access : public AccessBase {
 public:
  Access() = default;

  // Ensures that the data will be in memory.
  virtual void Load(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // True if the data is available.
  virtual bool Loaded(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // True if the data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // The data.
  virtual Type Value(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // For buffers, get a pointer on the buffer data.
  virtual const Type* Content(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  const uint8_t* Uint8Content(SyscallDecoderInterface* decoder, Stage stage) const override {
    return reinterpret_cast<const uint8_t*>(Content(decoder, stage));
  }

  // Generates the fidl codec value for this access.
  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const;
};

// Access to a system call argument. There is a direct access to the value
// given when the system call is called. For struct or buffer input arguments
// and for output arguments (all are pointers), we need to load the referenced
// data to be able to access the actual content. This is done within LoadInputs
// at the system call entry for input arguments. This is done within LoadOutputs
// after the system call returns for output arguments.
// All the basic types values and the pointer values are read at the system call
// entry.
template <typename Type>
class ArgumentAccess : public Access<Type> {
 public:
  explicit ArgumentAccess(const SyscallArgumentBaseTyped<Type>* argument) : argument_(argument) {}

  SyscallType GetSyscallType() const override { return argument_->syscall_type(); }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    argument_->Load(decoder, stage);
  }

  bool Loaded(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->Loaded(decoder, stage);
  }

  bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->ValueValid(decoder, stage);
  }

  Type Value(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->Value(decoder, stage);
  }

  void LoadArray(SyscallDecoderInterface* decoder, Stage stage, size_t size) override {
    argument_->LoadArray(decoder, stage, size);
  }

  bool ArrayLoaded(SyscallDecoderInterface* decoder, Stage stage, size_t size) const override {
    return argument_->ArrayLoaded(decoder, stage, size);
  }

  const Type* Content(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->Content(decoder, stage);
  }

  debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const override {
    return argument_->ComputeAutomationOperand(argument_indexes);
  }

 private:
  const SyscallArgumentBaseTyped<Type>* const argument_;
};

// Access to a field of a system call argument.
template <typename ClassType, typename Type>
class FieldAccess : public Access<Type> {
 public:
  explicit FieldAccess(const SyscallPointerArgument<ClassType>* argument,
                       Type (*get)(const ClassType* from), SyscallType syscall_type)
      : argument_(argument), get_(get), syscall_type_(syscall_type) {}

  SyscallType GetSyscallType() const override { return syscall_type_; }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    argument_->LoadArray(decoder, stage, sizeof(ClassType));
  }

  bool Loaded(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->ArrayLoaded(decoder, stage, sizeof(ClassType));
  }

  bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const override {
    return argument_->Content(decoder, stage) != nullptr;
  }

  Type Value(SyscallDecoderInterface* decoder, Stage stage) const override {
    return get_(argument_->Content(decoder, stage));
  }

  void LoadArray(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/, size_t /*size*/) override {}

  bool ArrayLoaded(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/,
                   size_t /*size*/) const override {
    return false;
  }

  const Type* Content(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {
    return nullptr;
  }

  debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const override {
    return argument_->ComputeAutomationOperand(argument_indexes);
  }

 private:
  const SyscallPointerArgument<ClassType>* const argument_;
  Type (*get_)(const ClassType* from);
  const SyscallType syscall_type_;
};

// Access to a field of a system call argument.
template <typename ClassType, typename Type>
class PointerFieldAccess : public Access<Type> {
 public:
  explicit PointerFieldAccess(const SyscallPointerArgument<ClassType>* argument,
                              const Type* (*get)(const ClassType* from), SyscallType syscall_type)
      : argument_(argument), get_(get), syscall_type_(syscall_type) {}

  SyscallType GetSyscallType() const override { return syscall_type_; }

  void Load(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {}

  bool Loaded(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {
    return false;
  }

  bool ValueValid(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {
    return false;
  }

  Type Value(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override { return {}; }

  void LoadArray(SyscallDecoderInterface* decoder, Stage stage, size_t size) override {
    argument_->LoadArray(decoder, stage, sizeof(ClassType));
    ClassType* object = argument_->Content(decoder, stage);
    if (object != nullptr) {
      decoder->LoadBuffer(stage, reinterpret_cast<uint64_t>(get_(object)), size);
    }
  }

  bool ArrayLoaded(SyscallDecoderInterface* decoder, Stage stage, size_t size) const override {
    ClassType* object = argument_->Content(decoder, stage);
    return (object == nullptr) ||
           decoder->BufferLoaded(stage, reinterpret_cast<uint64_t>(get_(object)), size);
  }

  const Type* Content(SyscallDecoderInterface* decoder, Stage stage) const override {
    ClassType* object = argument_->Content(decoder, stage);
    return reinterpret_cast<const Type*>(
        decoder->BufferContent(stage, reinterpret_cast<uint64_t>(get_(object))));
  }

  debug_ipc::AutomationOperand ComputeAutomationOperand(
      const std::vector<debug::RegisterID>& argument_indexes) const override {
    return argument_->ComputeAutomationOperand(argument_indexes);
  }

 private:
  const SyscallPointerArgument<ClassType>* const argument_;
  const Type* (*get_)(const ClassType* from);
  const SyscallType syscall_type_;
};

// Base class for the syscall arguments' conditions.
class SyscallInputOutputConditionBase {
 public:
  SyscallInputOutputConditionBase() = default;
  virtual ~SyscallInputOutputConditionBase() = default;

  // Ensures that the data will be in memory.
  virtual void Load(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // True if the data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  // True if the condition is satisfied.
  virtual bool True(SyscallDecoderInterface* decoder, Stage stage) const = 0;

  virtual bool ComputeAutomationCondition(
      const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked, debug::Arch arch,
      Syscall& syscall, std::vector<debug_ipc::AutomationCondition>& condition_vect) const = 0;
};

// Condition that a syscall argument must meet.
template <typename Type>
class SyscallInputOutputCondition : public SyscallInputOutputConditionBase {
 public:
  SyscallInputOutputCondition(std::unique_ptr<Access<Type>> access, Type value)
      : access_(std::move(access)), value_(value) {}

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    access_->Load(decoder, stage);
  }

  bool ValueValid(SyscallDecoderInterface* decoder, Stage stage) const override {
    return access_->ValueValid(decoder, stage);
  }

  bool True(SyscallDecoderInterface* decoder, Stage stage) const override {
    return access_->Value(decoder, stage) == value_;
  }

  bool ComputeAutomationCondition(
      const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked, debug::Arch arch,
      Syscall& syscall, std::vector<debug_ipc::AutomationCondition>& condition_vect) const override;

 private:
  // Access to the syscall argument.
  const std::unique_ptr<Access<Type>> access_;
  // Value which is expected.
  Type value_;
};

// Condition which checks that the architecture has an expected value.
class SyscallInputOutputArchCondition : public SyscallInputOutputConditionBase {
 public:
  explicit SyscallInputOutputArchCondition(debug::Arch arch) : arch_(arch) {}

  void Load(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {}

  bool ValueValid(SyscallDecoderInterface* /*decoder*/, Stage /*stage*/) const override {
    return true;
  }

  bool True(SyscallDecoderInterface* decoder, Stage /*stage*/) const override {
    return decoder->arch() == arch_;
  }

  bool ComputeAutomationCondition(
      const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked, debug::Arch arch,
      Syscall& syscall,
      std::vector<debug_ipc::AutomationCondition>& condition_vect) const override {
    return arch_ == arch;
  }

 private:
  // The architecture we check.
  const debug::Arch arch_;
};

// Base class for the inputs/outputs we want to display for a system call.
class SyscallInputOutputBase {
 public:
  explicit SyscallInputOutputBase(int64_t error_code, std::string_view name)
      : error_code_(error_code), name_(name) {}
  virtual ~SyscallInputOutputBase() = default;

  // For outputs, error code which must have been returned to be able to display
  // the ouput.
  int64_t error_code() const { return error_code_; }

  // Name of the input/output.
  const std::string& name() const { return name_; }

  // Id of the input/output
  uint8_t id() const { return id_; }

  const std::vector<std::unique_ptr<SyscallInputOutputConditionBase>>* conditions() const {
    return &conditions_;
  }

  // Returns true if this value is displayed inline.
  virtual bool InlineValue() const { return true; }

  // Computes the fidl codec type for this input/output.
  virtual std::unique_ptr<fidl_codec::Type> ComputeType() const;

  // Adds a condition which must be true to display the input/output.
  template <typename Type>
  SyscallInputOutputBase* DisplayIfEqual(std::unique_ptr<Access<Type>> access, Type value) {
    conditions_.push_back(
        std::make_unique<SyscallInputOutputCondition<Type>>(std::move(access), value));
    return this;
  }

  // Sets a unique id to distinguish the input/output from other conditional input/outputs with the
  // same name
  SyscallInputOutputBase* SetId(uint8_t id) {
    id_ = id;
    return this;
  }

  // Defines the architecture needed to display the input/output.
  SyscallInputOutputBase* DisplayIfArch(debug::Arch arch) {
    conditions_.push_back(std::make_unique<SyscallInputOutputArchCondition>(arch));
    return this;
  }

  // Ensures that all the data needed to display the input/output is available.
  virtual void Load(SyscallDecoderInterface* decoder, Stage stage) const {
    for (const auto& condition : conditions_) {
      condition->Load(decoder, stage);
    }
  }

  // Generates the fidl codec value for this input/output.
  virtual std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                           Stage stage) const;

  // True if all the conditions are met.
  bool ConditionsAreTrue(SyscallDecoderInterface* decoder, Stage stage) {
    for (const auto& condition : conditions_) {
      if (!condition->True(decoder, stage)) {
        return false;
      }
    }
    return true;
  }

  // Returns true if everything which needs memory has generated automation instructions.
  virtual bool GetAutomationInstructions(
      const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
      const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall);

 private:
  // For ouput arguments, condition the error code must meet.
  const int64_t error_code_;
  // Name of the displayed value.
  const std::string name_;
  // Conditions which must be met to display this input/output.
  std::vector<std::unique_ptr<SyscallInputOutputConditionBase>> conditions_;
  // A unique id to distinguish input/output from other conditional intput/output with the same name
  uint8_t id_ = 0;
};

// An input/output which only displays an expression (for example, the value of
// an argument). This is always displayed inline.
template <typename Type>
class SyscallInputOutput : public SyscallInputOutputBase {
 public:
  SyscallInputOutput(int64_t error_code, std::string_view name,
                     std::unique_ptr<Access<Type>> access)
      : SyscallInputOutputBase(error_code, name), access_(std::move(access)) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override { return access_->ComputeType(); }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    access_->Load(decoder, stage);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override {
    return access_->GenerateValue(decoder, stage);
  }

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override {
    return true;
  }

  const Access<Type>* access_ptr() const { return access_.get(); }

 private:
  const std::unique_ptr<Access<Type>> access_;
};

// An input/output which only displays a pointer.  This is always displayed inline.
template <typename Type>
class SyscallInputOutputPointer : public SyscallInputOutput<Type> {
 public:
  SyscallInputOutputPointer(int64_t error_code, std::string_view name,
                            std::unique_ptr<Access<Type>> access)
      : SyscallInputOutput<Type>(error_code, name, std::move(access)) {}

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override;
};

// An input/output which displays actual/requested. This is always displayed inline.
template <typename Type>
class SyscallInputOutputActualAndRequested : public SyscallInputOutputBase {
 public:
  SyscallInputOutputActualAndRequested(int64_t error_code, std::string_view name,
                                       std::unique_ptr<Access<Type>> actual,
                                       std::unique_ptr<Access<Type>> requested)
      : SyscallInputOutputBase(error_code, name),
        actual_(std::move(actual)),
        requested_(std::move(requested)) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return std::make_unique<fidl_codec::ActualAndRequestedType>();
  }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    actual_->Load(decoder, stage);
    requested_->Load(decoder, stage);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override {
    auto actual = actual_->GenerateValue(decoder, stage);
    uint64_t actual_absolute;
    bool actual_negative;
    if (!actual->GetIntegerValue(&actual_absolute, &actual_negative) || actual_negative) {
      return nullptr;
    }
    auto requested = requested_->GenerateValue(decoder, stage);
    uint64_t requested_absolute;
    bool requested_negative;
    if (!requested->GetIntegerValue(&requested_absolute, &requested_negative) ||
        requested_negative) {
      return nullptr;
    }
    return std::make_unique<fidl_codec::ActualAndRequestedValue>(actual_absolute,
                                                                 requested_absolute);
  }

 private:
  // Current value.
  const std::unique_ptr<Access<Type>> actual_;
  // Value which has been requested or value that should have been requested.
  const std::unique_ptr<Access<Type>> requested_;
};

// An input/output which is one indirect value (access via a pointer).
// This is always displayed inline.
template <typename Type, typename FromType>
class SyscallInputOutputIndirect : public SyscallInputOutputBase {
 public:
  SyscallInputOutputIndirect(int64_t error_code, std::string_view name, SyscallType syscall_type,
                             std::unique_ptr<Access<FromType>> buffer)
      : SyscallInputOutputBase(error_code, name),
        syscall_type_(syscall_type),
        buffer_(std::move(buffer)) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return SyscallTypeToFidlCodecType(syscall_type_);
  }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    buffer_->LoadArray(decoder, stage, sizeof(Type));
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override;

 private:
  // Type of the value.
  SyscallType syscall_type_;
  // Access to the buffer which contains all the items.
  const std::unique_ptr<Access<FromType>> buffer_;
  // Item count in the buffer.
  const std::unique_ptr<Access<size_t>> buffer_size_;
};

// An input/output which is a composed of several items of the same type.
// This is always displayed outline.
template <typename Type, typename FromType, typename SizeType>
class SyscallInputOutputBuffer : public SyscallInputOutputBase {
 public:
  SyscallInputOutputBuffer(int64_t error_code, std::string_view name, SyscallType syscall_type,
                           std::unique_ptr<Access<FromType>> buffer,
                           std::unique_ptr<Access<SizeType>> elem_size,
                           std::unique_ptr<Access<SizeType>> elem_count)
      : SyscallInputOutputBase(error_code, name),
        syscall_type_(syscall_type),
        buffer_(std::move(buffer)),
        elem_size_(std::move(elem_size)),
        elem_count_(std::move(elem_count)) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    std::unique_ptr<fidl_codec::Type> elem_type = SyscallTypeToFidlCodecType(syscall_type_);
    return std::make_unique<fidl_codec::VectorType>(std::move(elem_type));
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;

  bool InlineValue() const override { return false; }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    elem_size_->Load(decoder, stage);
    if (elem_count_ != nullptr) {
      elem_count_->Load(decoder, stage);
    }

    if (elem_size_->Loaded(decoder, stage) &&
        ((elem_count_ == nullptr) || elem_count_->Loaded(decoder, stage))) {
      SizeType value = elem_size_->Value(decoder, stage);
      if (elem_count_ != nullptr) {
        value *= elem_count_->Value(decoder, stage);
      }
      if (value > 0) {
        buffer_->LoadArray(decoder, stage, value * sizeof(Type));
      }
    }
  }

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override;

 private:
  // Type of one buffer item.
  SyscallType syscall_type_;
  // Access to the buffer which contains all the items.
  const std::unique_ptr<Access<FromType>> buffer_;
  // Size in bytes of one element in the buffer.
  const std::unique_ptr<Access<SizeType>> elem_size_;
  // Element count in the buffer. If null, we have exactly one element.
  const std::unique_ptr<Access<SizeType>> elem_count_;
};

// An input/output which is a buffer. Each item in this buffer is a pointer to a C string.
class SyscallInputOutputStringBuffer : public SyscallInputOutputBase {
 public:
  SyscallInputOutputStringBuffer(int64_t error_code, std::string_view name,
                                 std::unique_ptr<Access<char*>> buffer,
                                 std::unique_ptr<Access<uint32_t>> count, size_t max_size)
      : SyscallInputOutputBase(error_code, name),
        buffer_(std::move(buffer)),
        count_(std::move(count)),
        max_size_(max_size) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return std::make_unique<fidl_codec::StringType>();
  }

  bool InlineValue() const override { return false; }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    count_->Load(decoder, stage);

    if (count_->Loaded(decoder, stage)) {
      uint32_t count = count_->Value(decoder, stage);
      if (count > 0) {
        buffer_->LoadArray(decoder, stage, count * sizeof(char*));
        if (buffer_->ArrayLoaded(decoder, stage, count * sizeof(char*))) {
          const char* const* buffer = buffer_->Content(decoder, stage);
          if (buffer != nullptr) {
            for (uint32_t i = 0; i < count; ++i) {
              if (buffer[i] != nullptr) {
                decoder->LoadBuffer(stage, reinterpret_cast<uint64_t>(buffer[i]), max_size_);
              }
            }
          }
        }
      }
    }
  }

 private:
  // Access to the buffer which contains all the items.
  const std::unique_ptr<Access<char*>> buffer_;
  // Element count in the buffer.
  const std::unique_ptr<Access<uint32_t>> count_;
  // Maximum size of a string.
  size_t max_size_;
};

// An input/output which is a string. This is always displayed inline.
template <typename FromType>
class SyscallInputOutputString : public SyscallInputOutputBase {
 public:
  SyscallInputOutputString(int64_t error_code, std::string_view name,
                           std::unique_ptr<Access<FromType>> string,
                           std::unique_ptr<Access<size_t>> string_size)
      : SyscallInputOutputBase(error_code, name),
        string_(std::move(string)),
        string_size_(std::move(string_size)) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return std::make_unique<fidl_codec::StringType>();
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override {
    const char* string = reinterpret_cast<const char*>(string_->Content(decoder, stage));
    size_t string_size = string_size_->Value(decoder, stage);
    if (string == nullptr) {
      return std::make_unique<fidl_codec::NullValue>();
    } else {
      return std::make_unique<fidl_codec::StringValue>(
          fidl_codec::StringValue(std::string_view(string, string_size)));
    }
  }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    string_size_->Load(decoder, stage);

    if (string_size_->Loaded(decoder, stage)) {
      size_t value = string_size_->Value(decoder, stage);
      if (value > 0) {
        string_->LoadArray(decoder, stage, value);
      }
    }
  }

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override;

 private:
  const std::unique_ptr<Access<FromType>> string_;
  const std::unique_ptr<Access<size_t>> string_size_;
};

// An input/output which is a string of fixed size. This is always displayed inline.
class SyscallInputOutputFixedSizeString : public SyscallInputOutputBase {
 public:
  SyscallInputOutputFixedSizeString(int64_t error_code, std::string_view name,
                                    std::unique_ptr<Access<char>> string, size_t string_size)
      : SyscallInputOutputBase(error_code, name),
        string_(std::move(string)),
        string_size_(string_size) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return std::make_unique<fidl_codec::StringType>();
  }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    string_->LoadArray(decoder, stage, string_size_);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override {
    const char* string = reinterpret_cast<const char*>(string_->Content(decoder, stage));
    if (string == nullptr) {
      return std::make_unique<fidl_codec::NullValue>();
    }
    size_t size = strnlen(string, string_size_);
    return std::make_unique<fidl_codec::StringValue>(
        fidl_codec::StringValue(std::string_view(string, size)));
  }

 private:
  const std::unique_ptr<Access<char>> string_;
  size_t string_size_;
};

// An input/output which is an object. This is always displayed outline.
template <typename ClassType, typename SizeType>
class SyscallInputOutputObject : public SyscallInputOutputBase {
 public:
  SyscallInputOutputObject(int64_t error_code, std::string_view name,
                           std::unique_ptr<AccessBase> buffer,
                           std::unique_ptr<Access<SizeType>> buffer_size,
                           const Class<ClassType>* class_definition)
      : SyscallInputOutputBase(error_code, name),
        buffer_(std::move(buffer)),
        buffer_size_(std::move(buffer_size)),
        class_definition_(class_definition) {}

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return class_definition_->ComputeType();
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override {
    const auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
    return class_definition_->GenerateValue(object, decoder->arch());
  }

  bool InlineValue() const override { return false; }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    if (buffer_size_ != nullptr) {
      buffer_size_->Load(decoder, stage);
      if (buffer_size_->Loaded(decoder, stage)) {
        size_t value = buffer_size_->Value(decoder, stage);
        buffer_->LoadArray(decoder, stage, value);
      }
    } else {
      buffer_->LoadArray(decoder, stage, sizeof(ClassType));
    }
  }

 private:
  // Access to the buffer (raw data) which contains the object.
  const std::unique_ptr<AccessBase> buffer_;
  // Access to the buffer size. If nul, the size of the buffer is the size of ClassType.
  const std::unique_ptr<Access<SizeType>> buffer_size_;
  // Class definition for the displayed object.
  const Class<ClassType>* class_definition_;
};

// An input/output which is an array of objects. This is always displayed outline.
template <typename ClassType, typename SizeType>
class SyscallInputOutputObjectArray : public SyscallInputOutputBase {
 public:
  SyscallInputOutputObjectArray(int64_t error_code, std::string_view name,
                                std::unique_ptr<AccessBase> buffer,
                                std::unique_ptr<Access<SizeType>> buffer_size,
                                const Class<ClassType>* class_definition)
      : SyscallInputOutputBase(error_code, name),
        buffer_(std::move(buffer)),
        buffer_size_(std::move(buffer_size)),
        class_definition_(class_definition) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    std::unique_ptr<fidl_codec::Type> elem_type = class_definition_->ComputeType();
    return std::make_unique<fidl_codec::VectorType>(std::move(elem_type));
  }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    buffer_size_->Load(decoder, stage);

    if (buffer_size_->Loaded(decoder, stage)) {
      size_t value = buffer_size_->Value(decoder, stage);
      if (value > 0) {
        buffer_->LoadArray(decoder, stage, sizeof(ClassType) * value);
      }
    }
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;

 private:
  // Access to the buffer (raw data) which contains the object.
  const std::unique_ptr<AccessBase> buffer_;
  // Access to the buffer size.
  const std::unique_ptr<Access<SizeType>> buffer_size_;
  // Class definition for the displayed object.
  const Class<ClassType>* class_definition_;
};

class SyscallFidlMessageBase : public SyscallInputOutputBase {
 public:
  SyscallFidlMessageBase(int64_t error_code, std::string_view name,
                         fidl_codec::SyscallFidlType type,
                         std::unique_ptr<Access<zx_handle_t>> handle,
                         std::unique_ptr<Access<uint32_t>> options,
                         std::unique_ptr<Access<uint8_t>> bytes,
                         std::unique_ptr<Access<uint32_t>> num_bytes)
      : SyscallInputOutputBase(error_code, name),
        type_(type),
        handle_(std::move(handle)),
        options_(std::move(options)),
        bytes_(std::move(bytes)),
        num_bytes_(std::move(num_bytes)) {}

  fidl_codec::SyscallFidlType type() const { return type_; }
  const Access<zx_handle_t>* handle() const { return handle_.get(); }
  const Access<uint32_t>* options() const { return options_.get(); }
  const Access<uint8_t>* bytes() const { return bytes_.get(); }
  const Access<uint32_t>* num_bytes() const { return num_bytes_.get(); }

  void LoadBytes(SyscallDecoderInterface* decoder, Stage stage) const;

  class ByteBuffer {
   public:
    ByteBuffer(SyscallDecoderInterface* decoder, Stage stage, const SyscallFidlMessageBase* from);
    ~ByteBuffer() { delete[] buffer_; }

    const uint8_t* bytes() const { return bytes_; }
    uint32_t count() const { return count_; }

   private:
    uint8_t* buffer_ = nullptr;
    const uint8_t* bytes_ = nullptr;
    uint32_t count_ = 0;
  };

 private:
  const fidl_codec::SyscallFidlType type_;
  const std::unique_ptr<Access<zx_handle_t>> handle_;
  const std::unique_ptr<Access<uint32_t>> options_;
  const std::unique_ptr<Access<uint8_t>> bytes_;
  const std::unique_ptr<Access<uint32_t>> num_bytes_;
};

// An input/output which is a FIDL message. This is always displayed outline.
template <typename HandleType>
class SyscallFidlMessage : public SyscallFidlMessageBase {
 public:
  SyscallFidlMessage(int64_t error_code, std::string_view name, fidl_codec::SyscallFidlType type,
                     std::unique_ptr<Access<zx_handle_t>> handle,
                     std::unique_ptr<Access<uint32_t>> options,
                     std::unique_ptr<Access<uint8_t>> bytes,
                     std::unique_ptr<Access<uint32_t>> num_bytes,
                     std::unique_ptr<Access<HandleType>> handles,
                     std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessageBase(error_code, name, type, std::move(handle), std::move(options),
                               std::move(bytes), std::move(num_bytes)),
        handles_(std::move(handles)),
        num_handles_(std::move(num_handles)) {}

  const Access<HandleType>* handles() const { return handles_.get(); }
  const Access<uint32_t>* num_handles() const { return num_handles_.get(); }

  void Load(SyscallDecoderInterface* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    LoadBytes(decoder, stage);

    num_handles_->Load(decoder, stage);
    if (num_handles_->Loaded(decoder, stage)) {
      uint32_t value = num_handles_->Value(decoder, stage);
      if (value > 0) {
        handles_->LoadArray(decoder, stage, value * sizeof(HandleType));
      }
    }
  }

  bool GetAutomationInstructions(const std::vector<debug::RegisterID>& argument_indexes,
                                 bool is_invoked,
                                 const std::vector<debug_ipc::AutomationCondition>& conditions,
                                 Syscall& syscall) override;

 private:
  const std::unique_ptr<Access<HandleType>> handles_;
  const std::unique_ptr<Access<uint32_t>> num_handles_;
};

class SyscallFidlMessageHandle : public SyscallFidlMessage<zx_handle_t> {
 public:
  SyscallFidlMessageHandle(
      int64_t error_code, std::string_view name, fidl_codec::SyscallFidlType type,
      std::unique_ptr<Access<zx_handle_t>> handle, std::unique_ptr<Access<uint32_t>> options,
      std::unique_ptr<Access<uint8_t>> bytes, std::unique_ptr<Access<uint32_t>> num_bytes,
      std::unique_ptr<Access<zx_handle_t>> handles, std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessage<zx_handle_t>(error_code, name, type, std::move(handle),
                                        std::move(options), std::move(bytes), std::move(num_bytes),
                                        std::move(handles), std::move(num_handles)) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;
};

class SyscallFidlMessageHandleInfo : public SyscallFidlMessage<zx_handle_info_t> {
 public:
  SyscallFidlMessageHandleInfo(int64_t error_code, std::string_view name,
                               fidl_codec::SyscallFidlType type,
                               std::unique_ptr<Access<zx_handle_t>> handle,
                               std::unique_ptr<Access<uint32_t>> options,
                               std::unique_ptr<Access<uint8_t>> bytes,
                               std::unique_ptr<Access<uint32_t>> num_bytes,
                               std::unique_ptr<Access<zx_handle_info_t>> handles,
                               std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessage<zx_handle_info_t>(
            error_code, name, type, std::move(handle), std::move(options), std::move(bytes),
            std::move(num_bytes), std::move(handles), std::move(num_handles)) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;
};

class SyscallFidlMessageHandleDisposition : public SyscallFidlMessage<zx_handle_disposition_t> {
 public:
  SyscallFidlMessageHandleDisposition(int64_t error_code, std::string_view name,
                                      fidl_codec::SyscallFidlType type,
                                      std::unique_ptr<Access<zx_handle_t>> handle,
                                      std::unique_ptr<Access<uint32_t>> options,
                                      std::unique_ptr<Access<uint8_t>> bytes,
                                      std::unique_ptr<Access<uint32_t>> num_bytes,
                                      std::unique_ptr<Access<zx_handle_disposition_t>> handles,
                                      std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessage<zx_handle_disposition_t>(
            error_code, name, type, std::move(handle), std::move(options), std::move(bytes),
            std::move(num_bytes), std::move(handles), std::move(num_handles)) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoderInterface* decoder,
                                                   Stage stage) const override;
};

enum class SyscallKind {
  // Describes a function (like startup functions).
  kFunction,
  // Describes a regular syscall (with no special handling).
  kRegularSyscall,
  // zx_channel_read and zx_channel_read_etc syscalls.
  kChannelRead,
  // zx_channel_write and zx_channel_write_etc syscalls.
  kChannelWrite,
  // zx_channel_call syscall.
  kChannelCall
};

// Defines a syscall we want to decode/display.
class Syscall {
 public:
  Syscall(std::string_view name, SyscallReturnType return_type, SyscallKind kind)
      : name_(name),
        return_type_(return_type),
        kind_(kind),
        breakpoint_name_((kind == SyscallKind::kFunction) ? name_ : "$plt(" + name_ + ")") {}

  // Name of the syscall.
  [[nodiscard]] const std::string& name() const { return name_; }

  // Type of the syscall returned value.
  [[nodiscard]] SyscallReturnType return_type() const { return return_type_; }

  // Kind of the syscall.
  SyscallKind kind() const { return kind_; }

  // True if this class describes a regular function and not a syscall.
  bool is_function() const { return kind_ == SyscallKind::kFunction; }

  // True if this class describes a zx_channel_read or zx_channel_read_etc.
  bool is_channel_read() const { return kind_ == SyscallKind::kChannelRead; }

  // True if this class describes a zx_channel_write or zx_channel_write_etc.
  bool is_channel_write() const { return kind_ == SyscallKind::kChannelWrite; }

  // True if this class describes a zx_channel_call.
  bool is_channel_call() const { return kind_ == SyscallKind::kChannelCall; }

  // True if the syscall exchanges at least one FIDL message.
  bool has_fidl_message() const {
    return is_channel_read() || is_channel_write() || is_channel_call();
  }

  // Name of the breakpoint used to watch the syscall.
  [[nodiscard]] const std::string& breakpoint_name() const { return breakpoint_name_; }

  // All arguments for the syscall.
  [[nodiscard]] const std::vector<std::unique_ptr<SyscallArgumentBase>>& arguments() const {
    return arguments_;
  }

  // All the data we want to display at the syscall entry.
  [[nodiscard]] const std::vector<std::unique_ptr<SyscallInputOutputBase>>& inputs() const {
    return inputs_;
  }

  // All the data we want to display at the syscall exit. These data are
  // conditionally displayed depending on the syscall error code.
  [[nodiscard]] const std::vector<std::unique_ptr<SyscallInputOutputBase>>& outputs() const {
    return outputs_;
  }

  const std::vector<std::unique_ptr<fidl_codec::StructMember>>& input_inline_members() const {
    return input_inline_members_;
  }
  const std::vector<std::unique_ptr<fidl_codec::StructMember>>& input_outline_members() const {
    return input_outline_members_;
  }
  const std::vector<std::unique_ptr<fidl_codec::StructMember>>& output_inline_members() const {
    return output_inline_members_;
  }
  const std::vector<std::unique_ptr<fidl_codec::StructMember>>& output_outline_members() const {
    return output_outline_members_;
  }

  // The code to execute when the input is decoded and before the input is displayed.
  // If it exists and returns false, the input is not displayed.
  [[nodiscard]] bool (SyscallDecoderDispatcher::*inputs_decoded_action() const)(int64_t,
                                                                                SyscallDecoder*) {
    return inputs_decoded_action_;
  }
  void set_inputs_decoded_action(bool (SyscallDecoderDispatcher::*inputs_decoded_action)(
      int64_t timestamp, SyscallDecoder* decoder)) {
    inputs_decoded_action_ = inputs_decoded_action;
  }

  [[nodiscard]] void (SyscallDecoderDispatcher::*inference()
                          const)(const OutputEvent*, const fidl_codec::semantic::MethodSemantic*) {
    return inference_;
  }
  void set_inference(void (SyscallDecoderDispatcher::*inference)(
      const OutputEvent*, const fidl_codec::semantic::MethodSemantic*)) {
    inference_ = inference;
  }
  void set_compute_statistics(void (*compute_statistics)(const OutputEvent* event)) {
    compute_statistics_ = compute_statistics;
  }

  // Adds an argument definition to the syscall.
  template <typename Type>
  SyscallArgument<Type>* Argument(SyscallType syscall_type) {
    auto argument = std::make_unique<SyscallArgument<Type>>(arguments_.size(), syscall_type);
    auto result = argument.get();
    arguments_.push_back(std::move(argument));
    return result;
  }

  // Adds a pointer argument definition to the syscall (the actual type of the
  // argument is Type*).
  template <typename Type>
  SyscallPointerArgument<Type>* PointerArgument(SyscallType syscall_type) {
    auto argument = std::make_unique<SyscallPointerArgument<Type>>(arguments_.size(), syscall_type);
    auto result = argument.get();
    arguments_.push_back(std::move(argument));
    return result;
  }

  // Adds an inline input to display.
  template <typename Type>
  SyscallInputOutput<Type>* Input(std::string_view name, std::unique_ptr<Access<Type>> access) {
    auto object = std::make_unique<SyscallInputOutput<Type>>(0, name, std::move(access));
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an indirect input to display.
  template <typename Type, typename FromType>
  SyscallInputOutputIndirect<Type, FromType>* InputIndirect(
      std::string_view name, SyscallType syscall_type, std::unique_ptr<Access<FromType>> buffer) {
    auto object = std::make_unique<SyscallInputOutputIndirect<Type, FromType>>(
        0, name, syscall_type, std::move(buffer));
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an input buffer to display.
  template <typename Type, typename FromType, typename SizeType>
  void InputBuffer(std::string_view name, SyscallType syscall_type,
                   std::unique_ptr<Access<Type>> buffer,
                   std::unique_ptr<Access<SizeType>> elem_size,
                   std::unique_ptr<Access<SizeType>> elem_count = nullptr) {
    inputs_.push_back(std::make_unique<SyscallInputOutputBuffer<Type, FromType, SizeType>>(
        0, name, syscall_type, std::move(buffer), std::move(elem_size), std::move(elem_count)));
  }

  // Adds an input buffer. Each element of the buffer if a pointer to a C string.
  void InputStringBuffer(std::string_view name, std::unique_ptr<Access<char*>> buffer,
                         std::unique_ptr<Access<uint32_t>> count, size_t max_size) {
    inputs_.push_back(std::make_unique<SyscallInputOutputStringBuffer>(0, name, std::move(buffer),
                                                                       std::move(count), max_size));
  }

  // Adds an input string to display.
  template <typename FromType>
  SyscallInputOutputString<FromType>* InputString(std::string_view name,
                                                  std::unique_ptr<Access<FromType>> string,
                                                  std::unique_ptr<Access<size_t>> string_size) {
    auto object = std::make_unique<SyscallInputOutputString<FromType>>(0, name, std::move(string),
                                                                       std::move(string_size));
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds a fixed size input string to display.
  SyscallInputOutputFixedSizeString* InputFixedSizeString(std::string_view name,
                                                          std::unique_ptr<Access<char>> string,
                                                          size_t string_size) {
    auto object = std::make_unique<SyscallInputOutputFixedSizeString>(0, name, std::move(string),
                                                                      string_size);
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an object input to display.
  template <typename ClassType>
  SyscallInputOutputObject<ClassType, size_t>* InputObject(
      std::string_view name, std::unique_ptr<AccessBase> buffer,
      const Class<ClassType>* class_definition) {
    auto object = std::make_unique<SyscallInputOutputObject<ClassType, size_t>>(
        0, name, std::move(buffer), nullptr, class_definition);
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an object input with a dynamic size to display.
  template <typename ClassType, typename SizeType>
  SyscallInputOutputObject<ClassType, SizeType>* InputObject(
      std::string_view name, std::unique_ptr<AccessBase> buffer,
      std::unique_ptr<Access<SizeType>> buffer_size, const Class<ClassType>* class_definition) {
    auto object = std::make_unique<SyscallInputOutputObject<ClassType, SizeType>>(
        0, name, std::move(buffer), std::move(buffer_size), class_definition);
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an object array input to display.
  template <typename ClassType, typename SizeType>
  SyscallInputOutputObjectArray<ClassType, SizeType>* InputObjectArray(
      std::string_view name, std::unique_ptr<AccessBase> buffer,
      std::unique_ptr<Access<SizeType>> buffer_size, const Class<ClassType>* class_definition) {
    auto object = std::make_unique<SyscallInputOutputObjectArray<ClassType, SizeType>>(
        0, name, std::move(buffer), std::move(buffer_size), class_definition);
    auto result = object.get();
    inputs_.push_back(std::move(object));
    return result;
  }

  // Adds an input FIDL message to display.
  void InputFidlMessageHandle(std::string_view name, fidl_codec::SyscallFidlType type,
                              std::unique_ptr<Access<zx_handle_t>> handle,
                              std::unique_ptr<Access<uint32_t>> options,
                              std::unique_ptr<Access<uint8_t>> bytes,
                              std::unique_ptr<Access<uint32_t>> num_bytes,
                              std::unique_ptr<Access<zx_handle_t>> handles,
                              std::unique_ptr<Access<uint32_t>> num_handles) {
    inputs_.push_back(std::make_unique<SyscallFidlMessageHandle>(
        0, name, type, std::move(handle), std::move(options), std::move(bytes),
        std::move(num_bytes), std::move(handles), std::move(num_handles)));
  }

  // Adds an input FIDL message to display.
  void InputFidlMessageHandleDisposition(std::string_view name, fidl_codec::SyscallFidlType type,
                                         std::unique_ptr<Access<zx_handle_t>> handle,
                                         std::unique_ptr<Access<uint32_t>> options,
                                         std::unique_ptr<Access<uint8_t>> bytes,
                                         std::unique_ptr<Access<uint32_t>> num_bytes,
                                         std::unique_ptr<Access<zx_handle_disposition_t>> handles,
                                         std::unique_ptr<Access<uint32_t>> num_handles) {
    inputs_.push_back(std::make_unique<SyscallFidlMessageHandleDisposition>(
        0, name, type, std::move(handle), std::move(options), std::move(bytes),
        std::move(num_bytes), std::move(handles), std::move(num_handles)));
  }

  // Adds an inline output to display.
  template <typename Type>
  SyscallInputOutputPointer<Type>* Output(int64_t error_code, std::string_view name,
                                          std::unique_ptr<Access<Type>> access) {
    auto object =
        std::make_unique<SyscallInputOutputPointer<Type>>(error_code, name, std::move(access));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an inline output to display which is displayed like: actual/requested.
  template <typename Type>
  SyscallInputOutputActualAndRequested<Type>* OutputActualAndRequested(
      int64_t error_code, std::string_view name, std::unique_ptr<Access<Type>> actual,
      std::unique_ptr<Access<Type>> requested) {
    auto object = std::make_unique<SyscallInputOutputActualAndRequested<Type>>(
        error_code, name, std::move(actual), std::move(requested));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an indirect output to display.
  template <typename Type, typename FromType>
  SyscallInputOutputIndirect<Type, FromType>* OutputIndirect(
      int64_t error_code, std::string_view name, SyscallType syscall_type,
      std::unique_ptr<Access<FromType>> buffer) {
    auto object = std::make_unique<SyscallInputOutputIndirect<Type, FromType>>(
        error_code, name, syscall_type, std::move(buffer));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an output buffer to display.
  template <typename Type, typename FromType, typename SizeType>
  SyscallInputOutputBuffer<Type, FromType, SizeType>* OutputBuffer(
      int64_t error_code, std::string_view name, SyscallType syscall_type,
      std::unique_ptr<Access<FromType>> buffer, std::unique_ptr<Access<SizeType>> elem_size,
      std::unique_ptr<Access<SizeType>> elem_count = nullptr) {
    auto object = std::make_unique<SyscallInputOutputBuffer<Type, FromType, SizeType>>(
        error_code, name, syscall_type, std::move(buffer), std::move(elem_size),
        std::move(elem_count));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an output string to display.
  template <typename FromType>
  SyscallInputOutputString<FromType>* OutputString(int64_t error_code, std::string_view name,
                                                   std::unique_ptr<Access<FromType>> string,
                                                   std::unique_ptr<Access<size_t>> string_size) {
    auto object = std::make_unique<SyscallInputOutputString<FromType>>(
        error_code, name, std::move(string), std::move(string_size));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an object output to display.
  template <typename ClassType>
  SyscallInputOutputObject<ClassType, size_t>* OutputObject(
      int64_t error_code, std::string_view name, std::unique_ptr<AccessBase> buffer,
      const Class<ClassType>* class_definition) {
    auto object = std::make_unique<SyscallInputOutputObject<ClassType, size_t>>(
        error_code, name, std::move(buffer), nullptr, class_definition);
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an object array output to display.
  template <typename ClassType, typename SizeType>
  SyscallInputOutputObjectArray<ClassType, SizeType>* OutputObjectArray(
      int64_t error_code, std::string_view name, std::unique_ptr<AccessBase> buffer,
      std::unique_ptr<Access<SizeType>> buffer_size, const Class<ClassType>* class_definition) {
    auto object = std::make_unique<SyscallInputOutputObjectArray<ClassType, SizeType>>(
        error_code, name, std::move(buffer), std::move(buffer_size), class_definition);
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Add an output FIDL message to display.
  void OutputFidlMessageHandle(
      int64_t error_code, std::string_view name, fidl_codec::SyscallFidlType type,
      std::unique_ptr<Access<zx_handle_t>> handle, std::unique_ptr<Access<uint32_t>> options,
      std::unique_ptr<Access<uint8_t>> bytes, std::unique_ptr<Access<uint32_t>> num_bytes,
      std::unique_ptr<Access<zx_handle_t>> handles, std::unique_ptr<Access<uint32_t>> num_handles) {
    outputs_.push_back(std::make_unique<SyscallFidlMessageHandle>(
        error_code, name, type, std::move(handle), std::move(options), std::move(bytes),
        std::move(num_bytes), std::move(handles), std::move(num_handles)));
  }
  void OutputFidlMessageHandleInfo(int64_t error_code, std::string_view name,
                                   fidl_codec::SyscallFidlType type,
                                   std::unique_ptr<Access<zx_handle_t>> handle,
                                   std::unique_ptr<Access<uint32_t>> options,
                                   std::unique_ptr<Access<uint8_t>> bytes,
                                   std::unique_ptr<Access<uint32_t>> num_bytes,
                                   std::unique_ptr<Access<zx_handle_info_t>> handles,
                                   std::unique_ptr<Access<uint32_t>> num_handles) {
    outputs_.push_back(std::make_unique<SyscallFidlMessageHandleInfo>(
        error_code, name, type, std::move(handle), std::move(options), std::move(bytes),
        std::move(num_bytes), std::move(handles), std::move(num_handles)));
  }

  // Computes all the fidl codec types for this syscall.
  void ComputeTypes();

  const fidl_codec::StructMember* SearchInlineMember(const std::string& name, bool invoked) const;
  const fidl_codec::StructMember* SearchInlineMember(uint32_t id, bool invoked) const;
  const fidl_codec::StructMember* SearchOutlineMember(const std::string& name, bool invoked) const;
  const fidl_codec::StructMember* SearchOutlineMember(uint32_t id, bool invoked) const;

  void ComputeStatistics(const OutputEvent* event) const;

  void ComputeAutomation(debug::Arch arch);

  // This function stores the value of the operand passed to it when the invoked breakpoint is hit.
  // Then it modifies the operand so that when the exit breakpoint is hit it will load the stored
  // value.
  void EnsureOutputValue(debug_ipc::AutomationOperand* operand,
                         const std::vector<debug_ipc::AutomationCondition>& conditions) {
    if (operand->kind() == debug_ipc::AutomationOperandKind::kRegister ||
        operand->kind() == debug_ipc::AutomationOperandKind::kStackSlot ||
        operand->kind() == debug_ipc::AutomationOperandKind::kRegisterTimesConstant) {
      debug_ipc::AutomationInstruction store_instr;
      store_instr.InitComputeAndStore(*operand, next_stored_value_index_, conditions);
      AddAutomationInstruction(true, store_instr);
      operand->InitStoredValue(next_stored_value_index_);
      ++next_stored_value_index_;
    }
  }

  void AddAutomationInstruction(bool is_invoked,
                                const debug_ipc::AutomationInstruction& new_instr) {
    if (is_invoked) {
      invoked_bp_instructions_.emplace_back(new_instr);
    } else {
      exit_bp_instructions_.emplace_back(new_instr);
    }
  }

  std::vector<debug_ipc::AutomationInstruction> invoked_bp_instructions() const {
    return invoked_bp_instructions_;
  }
  std::vector<debug_ipc::AutomationInstruction> exit_bp_instructions() const {
    return exit_bp_instructions_;
  }

  bool fully_automated() const { return fully_automated_; }

 private:
  const std::string name_;
  const SyscallReturnType return_type_;
  const SyscallKind kind_;
  const std::string breakpoint_name_;
  std::vector<std::unique_ptr<SyscallArgumentBase>> arguments_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> inputs_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> outputs_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> input_inline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> input_outline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> output_inline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> output_outline_members_;
  bool (SyscallDecoderDispatcher::*inputs_decoded_action_)(int64_t timestamp,
                                                           SyscallDecoder* decoder) = nullptr;
  void (SyscallDecoderDispatcher::*inference_)(
      const OutputEvent* event, const fidl_codec::semantic::MethodSemantic* semantic) = nullptr;
  // Method which can compute statistics for the syscall.
  void (*compute_statistics_)(const OutputEvent* event) = nullptr;

  bool fully_automated_ = false;
  std::vector<debug_ipc::AutomationInstruction> invoked_bp_instructions_;
  std::vector<debug_ipc::AutomationInstruction> exit_bp_instructions_;
  uint32_t next_stored_value_index_ = 0;
};

// Decoder for syscalls. This creates the breakpoints for all the syscalls we
// want to monitor. Then, each time a breakpoint is reached, it creates a
// SyscallDecoder object which will handle the decoding of one syscall.
class SyscallDecoderDispatcher {
 public:
  explicit SyscallDecoderDispatcher(const DecodeOptions& decode_options);
  virtual ~SyscallDecoderDispatcher() = default;

  const DecodeOptions& decode_options() const { return decode_options_; }

  const std::map<std::string, std::unique_ptr<Syscall>>& syscalls() const { return syscalls_; }

  const std::map<zx_koid_t, std::unique_ptr<Process>>& processes() const { return processes_; }
  std::map<zx_koid_t, std::unique_ptr<Process>>& processes() { return processes_; }

  const std::map<zx_koid_t, std::unique_ptr<Thread>>& threads() const { return threads_; }
  std::map<zx_koid_t, std::unique_ptr<Thread>>& threads() { return threads_; }

  const Inference& inference() const { return inference_; }
  Inference& inference() { return inference_; }

  bool display_started() const { return display_started_; }
  void set_display_started() { display_started_ = true; }

  bool has_filter() const { return has_filter_; }

  bool needs_stack_frame() const { return needs_stack_frame_; }

  // True if we need to save the events in memory.
  bool needs_to_save_events() const { return needs_to_save_events_; }
  void set_needs_to_save_events() { needs_to_save_events_ = true; }

  const std::vector<std::shared_ptr<Event>>& decoded_events() const { return decoded_events_; }

  Syscall* SearchSyscall(const std::string& name) const {
    auto result = syscalls_.find(name);
    if (result == syscalls_.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  Process* SearchProcess(zx_koid_t koid) const {
    auto process = processes_.find(koid);
    if (process == processes_.end()) {
      return nullptr;
    }
    return process->second.get();
  }

  Process* CreateProcess(std::string_view name, zx_koid_t koid,
                         fxl::WeakPtr<zxdb::Process> zxdb_process) {
    FX_DCHECK(processes_.find(koid) == processes_.end());
    auto process = std::make_unique<Process>(name, koid, zxdb_process);
    auto returned_value = process.get();
    processes_.emplace(std::make_pair(koid, std::move(process)));
    return returned_value;
  }

  Thread* SearchThread(zx_koid_t koid) const {
    auto thread = threads_.find(koid);
    if (thread == threads_.end()) {
      return nullptr;
    }
    return thread->second.get();
  }

  Thread* CreateThread(zx_koid_t koid, Process* process) {
    FX_DCHECK(threads_.find(koid) == threads_.end());
    bool displayed;
    if (decode_options_.thread_filters.empty()) {
      displayed = true;
    } else {
      displayed = false;
      for (const auto thread_koid : decode_options_.thread_filters) {
        if (thread_koid == koid) {
          displayed = true;
          break;
        }
      }
    }
    auto thread = std::make_unique<Thread>(process, koid, displayed);
    auto returned_value = thread.get();
    threads_.emplace(std::make_pair(koid, std::move(thread)));
    return returned_value;
  }

  Thread* CreateThread(std::string_view process_name, zx_koid_t process_koid, zx_koid_t thread_koid,
                       fxl::WeakPtr<zxdb::Process> zxdb_process) {
    Process* process = SearchProcess(process_koid);
    if (process == nullptr) {
      process = CreateProcess(process_name, process_koid, std::move(zxdb_process));
    }
    Thread* thread = SearchThread(thread_koid);
    if (thread != nullptr) {
      return thread;
    }
    return CreateThread(thread_koid, process);
  }

  // Ensures that the handle has been added to the process this thread belongs to.
  // If this handle has not been added (this is the first time fidlcat monitors it),
  // the handle is added to the process list of handle using |creation_time| and |startup|.
  // |startup| is true if the handle has been given to the process during the process
  // initialization.
  // When created, the handle is first stored within the dispatcher.
  HandleInfo* CreateHandleInfo(Thread* thread, uint32_t handle, int64_t creation_time,
                               bool startup);

  // Decode an intercepted system call.
  // Called when a thread reached a breakpoint on a system call.
  // This will only start the decoding. The display will be done when all the
  // needed information will be gathered.
  void DecodeSyscall(InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
                     Syscall* syscall, uint64_t timestamp);

  // Decode an exception received by a thread.
  void DecodeException(InterceptionWorkflow* workflow, zxdb::Thread* thread, uint64_t timestamp);

  virtual fidl_codec::MessageDecoderDispatcher* MessageDecoderDispatcher() { return nullptr; }

  // Called when we are watching a process we launched.
  virtual void AddLaunchedProcess(uint64_t process_koid) {}

  // Delete a decoder created by DecodeSyscall. Called when the syscall is
  // fully decoded and displayed or the syscalls had an error.
  virtual void DeleteDecoder(SyscallDecoder* decoder);

  // Delete a decoder created by DecodeException. Called when the exception is fully decoded and
  // displayed or the exception had an error.
  virtual void DeleteDecoder(ExceptionDecoder* decoder);

  // Called when a process is launched (by using the run option). If |event->error_message()| is not
  // empty, the the process didn't launch and |event->error_message()| explains why.
  virtual void AddProcessLaunchedEvent(std::shared_ptr<ProcessLaunchedEvent> event) {}

  // Called when a process is monitored. If |event->error_message()| is not empty, we haven't been
  // able to monitor the process.
  virtual void AddProcessMonitoredEvent(std::shared_ptr<ProcessMonitoredEvent> event) {}

  // Called when a process is no longer monitored.
  virtual void AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event);

  // Called when the decoder failed.
  virtual void SyscallDecodingError(const fidlcat::Thread* fidlcat_thread, const Syscall* syscall,
                                    const DecoderError& error) {
    FX_LOGS(ERROR) << error.message();
  }

  // Adds an invoked event.
  virtual void AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) {}

  // Adds an output event.
  virtual void AddOutputEvent(std::shared_ptr<OutputEvent> output_event) {}

  // Adds an exception event.
  virtual void AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) {}

  // Saves a decoded event.
  void SaveEvent(std::shared_ptr<Event> event);

  // The session ended (we don't monitor anything more). Do global processing (like saving the
  // events).
  virtual void SessionEnded();

  // Generate the serialized protobuf session.
  void GenerateProtoSession(proto::Session* session);

 private:
  // Feeds syscalls_ with all the syscalls we can decode.
  void Populate();

  // Computes all the fidl codec types for the syscalls.
  void ComputeTypes();

  // Add a function we want to put a breakpoint on. Used by Populate.
  Syscall* AddFunction(std::string_view name, SyscallReturnType return_type) {
    auto syscall = std::make_unique<Syscall>(name, return_type, SyscallKind::kFunction);
    auto result = syscall.get();
    syscalls_.emplace(std::make_pair(syscall->name(), std::move(syscall)));
    return result;
  }

  // Add a syscall. Used by Populate.
  Syscall* Add(std::string_view name, SyscallReturnType return_type,
               SyscallKind kind = SyscallKind::kRegularSyscall) {
    auto syscall = std::make_unique<Syscall>(name, return_type, kind);
    auto result = syscall.get();
    syscalls_.emplace(std::make_pair(syscall->name(), std::move(syscall)));
    return result;
  }

  // Called when we intercept processargs_extract_handles.
  bool ExtractHandleInfos(int64_t timestamp, SyscallDecoder* decoder) {
    inference_.ExtractHandleInfos(timestamp, decoder);
    return false;
  }

  // Called when we intercept __libc_extensions_init.
  bool LibcExtensionsInit(int64_t timestamp, SyscallDecoder* decoder) {
    inference_.LibcExtensionsInit(timestamp, decoder);
    return false;
  }

  // Called when we intercept zx_channel_create.
  void ZxChannelCreate(const OutputEvent* event,
                       const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.ZxChannelCreate(event);
  }

  // Called when we intercept zx_channel_read or a zx_channel_read_etc.
  void ZxChannelRead(const OutputEvent* event,
                     const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.InferMessage(event, semantic, fidl_codec::semantic::ContextType::kRead);
  }

  // Called when we intercept zx_channel_write.
  void ZxChannelWrite(const OutputEvent* event,
                      const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.InferMessage(event, semantic, fidl_codec::semantic::ContextType::kWrite);
  }

  // Called when we intercept zx_channel_call.
  void ZxChannelCall(const OutputEvent* event,
                     const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.InferMessage(event, semantic, fidl_codec::semantic::ContextType::kCall);
  }

  // Called when we intercept zx_port_create.
  void ZxPortCreate(const OutputEvent* event,
                    const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.ZxPortCreate(event);
  }

  // Called when we intercept zx_timer_create.
  void ZxTimerCreate(const OutputEvent* event,
                     const fidl_codec::semantic::MethodSemantic* semantic) {
    inference_.ZxTimerCreate(event);
  }

  // Decoding options.
  const DecodeOptions& decode_options_;

  // The definition of all the syscalls we can decode.
  std::map<std::string, std::unique_ptr<Syscall>> syscalls_;

  // The intercepted syscalls we are currently decoding.
  std::map<uint64_t, std::unique_ptr<SyscallDecoder>> syscall_decoders_;

  // The intercepted exceptions we are currently decoding.
  std::map<uint64_t, std::unique_ptr<ExceptionDecoder>> exception_decoders_;

  // All the processes created by this dispatcher.
  std::map<zx_koid_t, std::unique_ptr<Process>> processes_;

  // All the threads created by this dispatcher.
  std::map<zx_koid_t, std::unique_ptr<Thread>> threads_;

  // All the handles created by this dispatcher.
  std::vector<std::unique_ptr<HandleInfo>> handle_infos_;

  // All the handles for which we have some information.
  Inference inference_;

  // True if we are now displaying messages and syscalls. If decode_options_.trigger_filters is not
  // empty, it starts with a false value and switchs to true when a message that satisfies one of
  // the filter is found.
  bool display_started_ = true;

  // True if we are filtering messages.
  bool has_filter_ = false;

  // True if we need the stack frame.
  bool needs_stack_frame_ = false;

  // True if we need to save the events in memory.
  bool needs_to_save_events_ = false;

  // All the events we have decoded (only filled if needs_to_save_events_ is true).
  std::vector<std::shared_ptr<Event>> decoded_events_;
};

class SyscallDisplayDispatcher : public SyscallDecoderDispatcher {
 public:
  SyscallDisplayDispatcher(fidl_codec::LibraryLoader* loader, const DecodeOptions& decode_options,
                           const DisplayOptions& display_options, std::ostream& os)
      : SyscallDecoderDispatcher(decode_options),
        message_decoder_dispatcher_(loader, display_options),
        os_(os),
        dump_messages_(display_options.dump_messages) {
    if (!display_options.extra_generation.empty()) {
      set_needs_to_save_events();
    }
  }

  fidl_codec::MessageDecoderDispatcher& message_decoder_dispatcher() {
    return message_decoder_dispatcher_;
  }

  const fidl_codec::Colors& colors() const { return message_decoder_dispatcher_.colors(); }

  int columns() const { return message_decoder_dispatcher_.columns(); }

  bool with_process_info() const { return message_decoder_dispatcher_.with_process_info(); }

  const std::vector<ExtraGeneration>& extra_generation() const {
    return message_decoder_dispatcher_.display_options().extra_generation;
  }

  bool extra_generation_needs_colors() const {
    return message_decoder_dispatcher_.display_options().extra_generation_needs_colors;
  }

  fidl_codec::LibraryLoader* loader() const { return message_decoder_dispatcher_.loader(); }

  const Event* last_displayed_event() const { return last_displayed_event_; }
  void clear_last_displayed_event() { last_displayed_event_ = nullptr; }

  std::ostream& os() const { return os_; }

  bool dump_messages() const { return dump_messages_; }

  uint32_t GetNextInvokedEventId() { return next_invoked_event_id_++; }

  fidl_codec::MessageDecoderDispatcher* MessageDecoderDispatcher() override {
    return &message_decoder_dispatcher_;
  }

  void AddLaunchedProcess(uint64_t process_koid) override {
    message_decoder_dispatcher_.AddLaunchedProcess(process_koid);
  }

  double GetTime(int64_t timestamp);

  void AddProcessLaunchedEvent(std::shared_ptr<ProcessLaunchedEvent> event) override;

  void AddProcessMonitoredEvent(std::shared_ptr<ProcessMonitoredEvent> event) override;

  void AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) override;

  void SyscallDecodingError(const fidlcat::Thread* fidlcat_thread, const Syscall* syscall,
                            const DecoderError& error) override;

  void AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) override;

  void AddOutputEvent(std::shared_ptr<OutputEvent> output_event) override;

  void AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) override;

  void SessionEnded() override;

  // Displays an invoked event.
  virtual void DisplayInvokedEvent(const InvokedEvent* invoked_event);

  // Displays an output event.
  virtual void DisplayOutputEvent(const OutputEvent* output_event);

  // Displays an exception event.
  virtual void DisplayExceptionEvent(const ExceptionEvent* exception_event);

  void DisplaySummary(std::ostream& os);

  void DisplayTop(std::ostream& os);

  void DisplayThreads(std::ostream& os);

  void GenerateTests(const std::string& output_directory);

 private:
  // Class which can decode a FIDL message.
  fidl_codec::MessageDecoderDispatcher message_decoder_dispatcher_;
  // The last event we displayed.
  const Event* last_displayed_event_ = nullptr;
  // The stream which will receive the syscall decodings.
  std::ostream& os_;
  // True if we always display the binary dump of the messages.
  const bool dump_messages_;
  uint32_t next_invoked_event_id_ = 0;
};

class SyscallCompareDispatcher : public SyscallDisplayDispatcher {
 public:
  SyscallCompareDispatcher(fidl_codec::LibraryLoader* loader, const DecodeOptions& decode_options,
                           const DisplayOptions& display_options,
                           std::shared_ptr<Comparator> comparator)
      : SyscallDisplayDispatcher(loader, decode_options, display_options, os_),
        comparator_(comparator) {}

  void SyscallDecodingError(const fidlcat::Thread* fidlcat_thread, const Syscall* syscall,
                            const DecoderError& error) override;

  void DisplayInvokedEvent(const InvokedEvent* invoked_event) override;

  void DisplayOutputEvent(const OutputEvent* output_event) override;

 private:
  std::shared_ptr<Comparator> comparator_;
  std::ostringstream os_;
};

// Generates a fidl codec value for this syscall value.
template <typename ValueType>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(ValueType /*value*/) {
  return std::make_unique<fidl_codec::InvalidValue>();
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(bool value) {
  return std::make_unique<fidl_codec::BoolValue>(value);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(int32_t value) {
  if (value < 0) {
    return std::make_unique<fidl_codec::IntegerValue>(-(static_cast<int64_t>(value)), true);
  }
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(int64_t value) {
  if (value < 0) {
    return std::make_unique<fidl_codec::IntegerValue>(-(static_cast<uint64_t>(value)), true);
  }
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(uint8_t value) {
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(uint16_t value) {
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(uint32_t value) {
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(uint64_t value) {
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}

#ifdef __MACH__
template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(uintptr_t value) {
  return std::make_unique<fidl_codec::IntegerValue>(value, false);
}
#endif

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateValue(zx_uint128_t value) {
  auto struct_value = std::make_unique<fidl_codec::StructValue>(GetUint128StructDefinition());
  struct_value->AddField("low", 0, std::make_unique<fidl_codec::IntegerValue>(value.low, false));
  struct_value->AddField("high", 0, std::make_unique<fidl_codec::IntegerValue>(value.high, false));
  return struct_value;
}

template <typename Type>
inline std::unique_ptr<fidl_codec::Value> GenerateHandleValue(Type handle) {
  return std::make_unique<fidl_codec::InvalidValue>();
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateHandleValue(zx_handle_t handle) {
  zx_handle_disposition_t result;
  result.operation = fidl_codec::kNoHandleDisposition;
  result.handle = handle;
  result.rights = 0;
  result.type = ZX_OBJ_TYPE_NONE;
  result.result = ZX_OK;
  return std::make_unique<fidl_codec::HandleValue>(result);
}

template <typename ClassType, typename Type>
bool ClassFieldCondition<ClassType, Type>::True(const ClassType* object, debug::Arch /*arch*/) {
  return field_->get()(object) == value_;
}

template <typename ClassType, typename Type>
bool ClassFieldMaskedCondition<ClassType, Type>::True(const ClassType* object,
                                                      debug::Arch /*arch*/) {
  return (field_->get()(object) & mask_) == value_;
}

template <typename ClassType, typename Type>
bool ArchCondition<ClassType, Type>::True(const ClassType* /*object*/, debug::Arch arch) {
  return arch_ == arch;
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> ClassField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug::Arch arch) const {
  if (ClassFieldBase<ClassType>::syscall_type() == SyscallType::kHandle) {
    return fidlcat::GenerateHandleValue<Type>(get_(object));
  } else {
    return fidlcat::GenerateValue<Type>(get_(object));
  }
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Type> ClassField<ClassType, Type>::ComputeType() const {
  auto type = SyscallTypeToFidlCodecType(this->syscall_type());
  if (type == nullptr) {
    type = std::make_unique<fidl_codec::InvalidType>();
  }
  return type;
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> ArrayField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug::Arch arch) const {
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  std::pair<const Type*, int> array = get_(object);
  auto syscall_type_ = this->syscall_type();

  for (int i = 0; i < array.second; ++i) {
    if (syscall_type_ == SyscallType::kHandle) {
      vector_value->AddValue(fidlcat::GenerateHandleValue<Type>(array.first[i]));
    } else {
      vector_value->AddValue(fidlcat::GenerateValue<Type>(array.first[i]));
    }
  }

  return vector_value;
}

template <typename ClassType, typename Type, typename SizeType>
std::unique_ptr<fidl_codec::Value> DynamicArrayField<ClassType, Type, SizeType>::GenerateValue(
    const ClassType* object, debug::Arch arch) const {
  std::pair<const Type*, SizeType> vector = get_(object);
  auto syscall_type_ = this->syscall_type();
  if (syscall_type_ == SyscallType::kChar) {
    size_t size = strnlen(reinterpret_cast<const char*>(vector.first), vector.second);
    return std::make_unique<fidl_codec::StringValue>(
        std::string_view(reinterpret_cast<const char*>(vector.first), size));
  }

  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  for (SizeType i = 0; i < vector.second; ++i) {
    if (syscall_type_ == SyscallType::kHandle) {
      vector_value->AddValue(fidlcat::GenerateHandleValue<Type>(vector.first[i]));
    } else {
      vector_value->AddValue(fidlcat::GenerateValue<Type>(vector.first[i]));
    }
  }

  return vector_value;
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> ArrayClassField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug::Arch arch) const {
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  std::pair<const Type*, int> array = get_(object);

  for (int i = 0; i < array.second; ++i) {
    vector_value->AddValue(sub_class_->GenerateValue(array.first + i, arch));
  }

  return vector_value;
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> DynamicArrayClassField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug::Arch arch) const {
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  const Type* array = get_(object);
  uint32_t size = get_size_(object);

  for (uint32_t i = 0; i < size; ++i) {
    vector_value->AddValue(sub_class_->GenerateValue(array + i, arch));
  }

  return vector_value;
}

template <typename Type>
std::unique_ptr<fidl_codec::Value> Access<Type>::GenerateValue(SyscallDecoderInterface* decoder,
                                                               Stage stage) const {
  if (ValueValid(decoder, stage)) {
    if (GetSyscallType() == SyscallType::kHandle) {
      return GenerateHandleValue<Type>(Value(decoder, stage));
    }
    return fidlcat::GenerateValue<Type>(Value(decoder, stage));
  }
  return std::make_unique<fidl_codec::NullValue>();
}

template <typename Type>
bool SyscallInputOutputCondition<Type>::ComputeAutomationCondition(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked, debug::Arch arch,
    Syscall& syscall, std::vector<debug_ipc::AutomationCondition>& condition_vect) const {
  debug_ipc::AutomationCondition condition;
  debug_ipc::AutomationOperand operand = access_->ComputeAutomationOperand(argument_indexes);
  if (!is_invoked) {
    syscall.EnsureOutputValue(&operand, std::vector<debug_ipc::AutomationCondition>());
  }
  condition.InitEquals(operand, static_cast<uint64_t>(value_));
  condition_vect.emplace_back(condition);
  return true;
}

template <typename Type>
bool SyscallInputOutputPointer<Type>::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  debug_ipc::AutomationOperand access_value =
      SyscallInputOutput<Type>::access_ptr()->ComputeAutomationOperand(argument_indexes);
  if (access_value.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }
  if (!is_invoked) {
    syscall.EnsureOutputValue(&access_value, conditions);
  }

  debug_ipc::AutomationOperand size;
  size.InitConstant(static_cast<uint32_t>(sizeof(Type)));
  debug_ipc::AutomationInstruction instr;
  instr.InitLoadMemory(access_value, size, conditions);
  syscall.AddAutomationInstruction(is_invoked, instr);
  return true;
}

template <typename Type, typename FromType>
std::unique_ptr<fidl_codec::Value> SyscallInputOutputIndirect<Type, FromType>::GenerateValue(
    SyscallDecoderInterface* decoder, Stage stage) const {
  const FromType* buffer = buffer_->Content(decoder, stage);
  return fidlcat::GenerateValue<Type>(*reinterpret_cast<const Type*>(buffer));
}

template <typename Type, typename FromType>
bool SyscallInputOutputIndirect<Type, FromType>::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  debug_ipc::AutomationOperand buff_id = buffer_->ComputeAutomationOperand(argument_indexes);
  if (buff_id.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }
  if (!is_invoked) {
    syscall.EnsureOutputValue(&buff_id, conditions);
  }

  debug_ipc::AutomationOperand size;
  size.InitConstant(static_cast<uint32_t>(sizeof(Type)));
  debug_ipc::AutomationInstruction instr;
  instr.InitLoadMemory(buff_id, size, conditions);
  syscall.AddAutomationInstruction(is_invoked, instr);
  return true;
}

template <typename Type, typename FromType, typename SizeType>
std::unique_ptr<fidl_codec::Value>
SyscallInputOutputBuffer<Type, FromType, SizeType>::GenerateValue(SyscallDecoderInterface* decoder,
                                                                  Stage stage) const {
  const FromType* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    return std::make_unique<fidl_codec::NullValue>();
  }
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  size_t buffer_size = elem_size_->Value(decoder, stage);
  if (elem_count_ != nullptr) {
    buffer_size *= elem_count_->Value(decoder, stage);
  }
  for (size_t i = 0; i < buffer_size; ++i) {
    if (syscall_type_ == SyscallType::kHandle) {
      vector_value->AddValue(
          fidlcat::GenerateHandleValue<Type>(reinterpret_cast<const Type*>(buffer)[i]));
    } else {
      vector_value->AddValue(
          fidlcat::GenerateValue<Type>(reinterpret_cast<const Type*>(buffer)[i]));
    }
  }
  return vector_value;
}

template <typename Type, typename FromType, typename SizeType>
bool SyscallInputOutputBuffer<Type, FromType, SizeType>::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  debug_ipc::AutomationOperand buff_id = buffer_->ComputeAutomationOperand(argument_indexes);
  if (buff_id.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }

  debug_ipc::AutomationOperand size;
  if (elem_count_ == nullptr) {
    size = elem_size_->ComputeAutomationOperand(argument_indexes);
  } else {
    // TODO(michaelrj): In this case size needs to be elem_size_ * elem_count_, but there isn't a
    // way to multiply two registers right now. Thankfully, this is only used for zx_fifo_write
    // right now, and so can be handled later.
    return false;
  }

  if (size.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }

  if (!is_invoked) {
    syscall.EnsureOutputValue(&buff_id, conditions);
    syscall.EnsureOutputValue(&size, conditions);
  }

  debug_ipc::AutomationInstruction instr;
  instr.InitLoadMemory(buff_id, size, conditions);
  syscall.AddAutomationInstruction(is_invoked, instr);
  return true;
}

template <typename FromType>
bool SyscallInputOutputString<FromType>::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  debug_ipc::AutomationOperand string_id = string_->ComputeAutomationOperand(argument_indexes);
  debug_ipc::AutomationOperand size = string_size_->ComputeAutomationOperand(argument_indexes);
  if (string_id.kind() == debug_ipc::AutomationOperandKind::kZero ||
      size.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }
  if (!is_invoked) {
    syscall.EnsureOutputValue(&string_id, conditions);
    syscall.EnsureOutputValue(&size, conditions);
  }

  debug_ipc::AutomationInstruction instr;
  instr.InitLoadMemory(string_id, size, conditions);
  syscall.AddAutomationInstruction(is_invoked, instr);
  return true;
}

template <typename ClassType, typename SizeType>
std::unique_ptr<fidl_codec::Value>
SyscallInputOutputObjectArray<ClassType, SizeType>::GenerateValue(SyscallDecoderInterface* decoder,
                                                                  Stage stage) const {
  auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
  if (object == nullptr) {
    return std::make_unique<fidl_codec::NullValue>();
  }
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  SizeType count = buffer_size_->Value(decoder, stage);
  for (SizeType i = 0; i < count; ++i) {
    vector_value->AddValue(class_definition_->GenerateValue(object + i, decoder->arch()));
  }
  return vector_value;
}

template <typename HandleType>
bool SyscallFidlMessage<HandleType>::GetAutomationInstructions(
    const std::vector<debug::RegisterID>& argument_indexes, bool is_invoked,
    const std::vector<debug_ipc::AutomationCondition>& conditions, Syscall& syscall) {
  debug_ipc::AutomationOperand data_id = bytes()->ComputeAutomationOperand(argument_indexes);
  if (data_id.kind() == debug_ipc::AutomationOperandKind::kZero) {
    return false;
  }
  debug_ipc::AutomationOperand data_size = num_bytes()->ComputeAutomationOperand(argument_indexes);
  if (data_id.index() == data_size.index()) {
    return false;
  }

  debug_ipc::AutomationInstruction data_instr;
  if (is_invoked) {
    debug_ipc::AutomationOperand fidl_options =
        options()->ComputeAutomationOperand(argument_indexes);

    debug_ipc::AutomationCondition is_iovec;
    is_iovec.InitMaskAndNotEquals(fidl_options, 0, ZX_CHANNEL_WRITE_USE_IOVEC);

    debug_ipc::AutomationCondition is_not_iovec;
    is_not_iovec.InitMaskAndEquals(fidl_options, 0, ZX_CHANNEL_WRITE_USE_IOVEC);

    std::vector<debug_ipc::AutomationCondition> conditions_iovec;
    conditions_iovec.insert(conditions_iovec.begin(), conditions.begin(), conditions.end());
    conditions_iovec.emplace_back(is_iovec);

    std::vector<debug_ipc::AutomationCondition> conditions_not_iovec;
    conditions_not_iovec.insert(conditions_not_iovec.begin(), conditions.begin(), conditions.end());
    conditions_not_iovec.emplace_back(is_not_iovec);

    data_instr.InitLoadMemory(data_id, data_size, conditions_not_iovec);

    debug_ipc::AutomationOperand iovec_pointer_offset;
    iovec_pointer_offset.InitIndirectUInt64Loop(0);

    debug_ipc::AutomationOperand iovec_length_offset;
    iovec_length_offset.InitIndirectUInt32Loop(8);

    debug_ipc::AutomationInstruction data_instr_iovec;
    data_instr_iovec.InitLoopLoadMemory(data_id, data_size, iovec_pointer_offset,
                                        iovec_length_offset, sizeof(zx_channel_iovec),
                                        conditions_iovec);
    syscall.AddAutomationInstruction(is_invoked, data_instr_iovec);
  } else {
    syscall.EnsureOutputValue(&data_id, conditions);
    data_size.IndirectValue32(0);

    data_instr.InitLoadMemory(data_id, data_size, conditions);
  }

  bool is_fully_valid = true;

  if (data_id.kind() != debug_ipc::AutomationOperandKind::kZero &&
      data_size.kind() != debug_ipc::AutomationOperandKind::kZero) {
    syscall.AddAutomationInstruction(is_invoked, data_instr);
  } else {
    is_fully_valid = false;
  }

  debug_ipc::AutomationOperand handles_id = handles_->ComputeAutomationOperand(argument_indexes);
  debug_ipc::AutomationOperand handles_size =
      num_handles_->ComputeAutomationOperand(argument_indexes);

  if (!is_invoked) {
    syscall.EnsureOutputValue(&handles_id, conditions);
    handles_size.IndirectValue32(0);
  }

  handles_size.MultiplyValue(static_cast<uint32_t>(sizeof(HandleType)));
  debug_ipc::AutomationInstruction handles_instr;
  handles_instr.InitLoadMemory(handles_id, handles_size, conditions);

  if (handles_id.kind() != debug_ipc::AutomationOperandKind::kZero &&
      handles_size.kind() != debug_ipc::AutomationOperandKind::kZero) {
    syscall.AddAutomationInstruction(is_invoked, handles_instr);
  } else {
    is_fully_valid = false;
  }
  return is_fully_valid;
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
