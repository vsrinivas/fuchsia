// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/types.h>

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
  virtual bool True(const ClassType* object, debug_ipc::Arch /*arch*/) = 0;
};

// Condition which checks that the field has an expected value.
template <typename ClassType, typename Type>
class ClassFieldCondition : public ClassFieldConditionBase<ClassType> {
 public:
  ClassFieldCondition(const ClassField<ClassType, Type>* field, Type value)
      : field_(field), value_(value) {}

  bool True(const ClassType* object, debug_ipc::Arch /*arch*/) override;

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

  bool True(const ClassType* object, debug_ipc::Arch /*arch*/) override;

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
  explicit ArchCondition(debug_ipc::Arch arch) : arch_(arch) {}

  bool True(const ClassType* object, debug_ipc::Arch /*arch*/) override;

 private:
  // The architecture we check.
  const debug_ipc::Arch arch_;
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
  ClassFieldBase<ClassType>* DisplayIfArch(debug_ipc::Arch arch) {
    conditions_.push_back(std::make_unique<ArchCondition<ClassType, uint8_t>>(arch));
    return this;
  }

  bool ConditionsAreTrue(const ClassType* object, debug_ipc::Arch arch) {
    for (const auto& condition : conditions_) {
      if (!condition->True(object, arch)) {
        return false;
      }
    }
    return true;
  }

  virtual void Display(const ClassType* object, debug_ipc::Arch arch,
                       fidl_codec::PrettyPrinter& printer) const = 0;

  virtual std::unique_ptr<fidl_codec::Type> ComputeType() const {
    return std::make_unique<fidl_codec::InvalidType>();
  }

  virtual std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                           debug_ipc::Arch arch) const {
    return std::make_unique<fidl_codec::InvalidValue>();
  }

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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

  std::unique_ptr<fidl_codec::Type> ComputeType() const override {
    return field_class_->ComputeType();
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const override {
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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch,
               fidl_codec::PrettyPrinter& printer) const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const override;

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

  void DisplayObject(const ClassType* object, debug_ipc::Arch arch,
                     fidl_codec::PrettyPrinter& printer) const {
    printer << "{\n";
    {
      fidl_codec::Indent indent(printer);
      for (const auto& field : fields_) {
        if (field->ConditionsAreTrue(object, arch)) {
          field->Display(object, arch, printer);
        }
      }
    }
    printer << '}';
  }

  std::unique_ptr<fidl_codec::Type> ComputeType() const {
    return std::make_unique<fidl_codec::StructType>(struct_definition_, true);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(const ClassType* object,
                                                   debug_ipc::Arch arch) const {
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
  virtual void Load(SyscallDecoder* /*decoder*/, Stage /*stage*/) const {}

  // True if the argument data is available.
  virtual bool Loaded(SyscallDecoder* /*decoder*/, Stage /*stage*/) const { return false; }

  // True if the argument data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoder* /*decoder*/, Stage /*stage*/) const { return false; }

  // The data for the argument.
  virtual Type Value(SyscallDecoder* /*decoder*/, Stage /*stage*/) const { return Type(); }

  // For buffers, ensures that the buffer will be in memory.
  virtual void LoadArray(SyscallDecoder* /*decoder*/, Stage /*stage*/, size_t /*size*/) const {}

  // For buffers, true if the buffer is available.
  virtual bool ArrayLoaded(SyscallDecoder* /*decoder*/, Stage /*stage*/, size_t /*size*/) const {
    return false;
  }

  // For buffers, get a pointer on the buffer data.
  virtual Type* Content(SyscallDecoder* /*decoder*/, Stage /*stage*/) const { return nullptr; }
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

  bool Loaded(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return true; }

  bool ValueValid(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return true; }

  Type Value(SyscallDecoder* decoder, Stage /*stage*/) const override {
    return Type(decoder->ArgumentValue(index()));
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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    decoder->LoadArgument(stage, index(), sizeof(Type));
  }

  bool Loaded(SyscallDecoder* decoder, Stage stage) const override {
    return decoder->ArgumentLoaded(stage, index(), sizeof(Type));
  }

  bool ValueValid(SyscallDecoder* decoder, Stage stage) const override {
    return decoder->ArgumentContent(stage, index()) != nullptr;
  }

  Type Value(SyscallDecoder* decoder, Stage stage) const override {
    uint8_t* content = decoder->ArgumentContent(stage, index());
    if (content == nullptr) {
      return Type();
    }
    return *reinterpret_cast<Type*>(content);
  }

  void LoadArray(SyscallDecoder* decoder, Stage stage, size_t size) const override {
    decoder->LoadArgument(stage, index(), size);
  }

  bool ArrayLoaded(SyscallDecoder* decoder, Stage stage, size_t size) const override {
    return decoder->ArgumentLoaded(stage, index(), size);
  }

  Type* Content(SyscallDecoder* decoder, Stage stage) const override {
    return reinterpret_cast<Type*>(decoder->ArgumentContent(stage, index()));
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
  virtual void LoadArray(SyscallDecoder* decoder, Stage stage, size_t size) = 0;

  // For buffers, true if the buffer is available.
  virtual bool ArrayLoaded(SyscallDecoder* decoder, Stage stage, size_t size) const = 0;

  // For buffers, get a pointer on the buffer data.
  virtual const uint8_t* Uint8Content(SyscallDecoder* decoder, Stage stage) const = 0;
};

// Use to access data for an input or an output.
template <typename Type>
class Access : public AccessBase {
 public:
  Access() = default;

  // Ensures that the data will be in memory.
  virtual void Load(SyscallDecoder* decoder, Stage stage) const = 0;

  // True if the data is available.
  virtual bool Loaded(SyscallDecoder* decoder, Stage stage) const = 0;

  // True if the data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoder* decoder, Stage stage) const = 0;

  // The data.
  virtual Type Value(SyscallDecoder* decoder, Stage stage) const = 0;

  // For buffers, get a pointer on the buffer data.
  virtual const Type* Content(SyscallDecoder* decoder, Stage stage) const = 0;

  const uint8_t* Uint8Content(SyscallDecoder* decoder, Stage stage) const override {
    return reinterpret_cast<const uint8_t*>(Content(decoder, stage));
  }

  // Generates the fidl codec value for this access.
  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder, Stage stage) const;

  // Display the data on a stream (with name and type).
  void Display(SyscallDecoder* decoder, Stage stage, std::string_view name,
               fidl_codec::PrettyPrinter& printer) const;
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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    argument_->Load(decoder, stage);
  }

  bool Loaded(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->Loaded(decoder, stage);
  }

  bool ValueValid(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->ValueValid(decoder, stage);
  }

  Type Value(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->Value(decoder, stage);
  }

  void LoadArray(SyscallDecoder* decoder, Stage stage, size_t size) override {
    argument_->LoadArray(decoder, stage, size);
  }

  bool ArrayLoaded(SyscallDecoder* decoder, Stage stage, size_t size) const override {
    return argument_->ArrayLoaded(decoder, stage, size);
  }

  const Type* Content(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->Content(decoder, stage);
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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    argument_->LoadArray(decoder, stage, sizeof(ClassType));
  }

  bool Loaded(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->ArrayLoaded(decoder, stage, sizeof(ClassType));
  }

  bool ValueValid(SyscallDecoder* decoder, Stage stage) const override {
    return argument_->Content(decoder, stage) != nullptr;
  }

  Type Value(SyscallDecoder* decoder, Stage stage) const override {
    return get_(argument_->Content(decoder, stage));
  }

  void LoadArray(SyscallDecoder* /*decoder*/, Stage /*stage*/, size_t /*size*/) override {}

  bool ArrayLoaded(SyscallDecoder* /*decoder*/, Stage /*stage*/, size_t /*size*/) const override {
    return false;
  }

  const Type* Content(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override {
    return nullptr;
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

  void Load(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override {}

  bool Loaded(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return false; }

  bool ValueValid(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return false; }

  Type Value(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return {}; }

  void LoadArray(SyscallDecoder* decoder, Stage stage, size_t size) override {
    argument_->LoadArray(decoder, stage, sizeof(ClassType));
    ClassType* object = argument_->Content(decoder, stage);
    if (object != nullptr) {
      decoder->LoadBuffer(stage, reinterpret_cast<uint64_t>(get_(object)), size);
    }
  }

  bool ArrayLoaded(SyscallDecoder* decoder, Stage stage, size_t size) const override {
    ClassType* object = argument_->Content(decoder, stage);
    return (object == nullptr) ||
           decoder->BufferLoaded(stage, reinterpret_cast<uint64_t>(get_(object)), size);
  }

  const Type* Content(SyscallDecoder* decoder, Stage stage) const override {
    ClassType* object = argument_->Content(decoder, stage);
    return reinterpret_cast<const Type*>(
        decoder->BufferContent(stage, reinterpret_cast<uint64_t>(get_(object))));
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
  virtual void Load(SyscallDecoder* decoder, Stage stage) const = 0;

  // True if the data is valid (not a null pointer).
  virtual bool ValueValid(SyscallDecoder* decoder, Stage stage) const = 0;

  // True if the condition is satisfied.
  virtual bool True(SyscallDecoder* decoder, Stage stage) const = 0;
};

// Condition that a syscall argument must meet.
template <typename Type>
class SyscallInputOutputCondition : public SyscallInputOutputConditionBase {
 public:
  SyscallInputOutputCondition(std::unique_ptr<Access<Type>> access, Type value)
      : access_(std::move(access)), value_(value) {}

  void Load(SyscallDecoder* decoder, Stage stage) const override { access_->Load(decoder, stage); }

  bool ValueValid(SyscallDecoder* decoder, Stage stage) const override {
    return access_->ValueValid(decoder, stage);
  }

  bool True(SyscallDecoder* decoder, Stage stage) const override {
    return access_->Value(decoder, stage) == value_;
  }

 private:
  // Access to the syscall argument.
  const std::unique_ptr<Access<Type>> access_;
  // Value which is expected.
  Type value_;
};

// Condition which checks that the architecture has an expected value.
class SyscallInputOutputArchCondition : public SyscallInputOutputConditionBase {
 public:
  explicit SyscallInputOutputArchCondition(debug_ipc::Arch arch) : arch_(arch) {}

  void Load(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override {}

  bool ValueValid(SyscallDecoder* /*decoder*/, Stage /*stage*/) const override { return true; }

  bool True(SyscallDecoder* decoder, Stage /*stage*/) const override {
    return decoder->arch() == arch_;
  }

 private:
  // The architecture we check.
  const debug_ipc::Arch arch_;
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
  SyscallInputOutputBase* DisplayIfArch(debug_ipc::Arch arch) {
    conditions_.push_back(std::make_unique<SyscallInputOutputArchCondition>(arch));
    return this;
  }

  // Ensures that all the data needed to display the input/output is available.
  virtual void Load(SyscallDecoder* decoder, Stage stage) const {
    for (const auto& condition : conditions_) {
      condition->Load(decoder, stage);
    }
  }

  // Generates the fidl codec value for this input/output.
  virtual std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                           Stage stage) const;

  // Displays small inputs or outputs.
  virtual const char* DisplayInline(SyscallDecoder* /*decoder*/, Stage /*stage*/,
                                    const char* separator,
                                    fidl_codec::PrettyPrinter& /*printer*/) const {
    return separator;
  }

  // Displays large (multi lines) inputs or outputs.
  virtual void DisplayOutline(SyscallDecoder* /*decoder*/, Stage /*stage*/,
                              fidl_codec::PrettyPrinter& /*printer*/) const {}

  // True if all the conditions are met.
  bool ConditionsAreTrue(SyscallDecoder* decoder, Stage stage) {
    for (const auto& condition : conditions_) {
      if (!condition->True(decoder, stage)) {
        return false;
      }
    }
    return true;
  }

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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    access_->Load(decoder, stage);
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                   Stage stage) const override {
    return access_->GenerateValue(decoder, stage);
  }

  const char* DisplayInline(SyscallDecoder* decoder, Stage stage, const char* separator,
                            fidl_codec::PrettyPrinter& printer) const override {
    printer << separator;
    access_->Display(decoder, stage, name(), printer);
    return ", ";
  }

 private:
  const std::unique_ptr<Access<Type>> access_;
};

// An input/output which displays actual/asked. This is always displayed inline.
template <typename Type>
class SyscallInputOutputActualAndRequested : public SyscallInputOutputBase {
 public:
  SyscallInputOutputActualAndRequested(int64_t error_code, std::string_view name,
                                       std::unique_ptr<Access<Type>> actual,
                                       std::unique_ptr<Access<Type>> asked)
      : SyscallInputOutputBase(error_code, name),
        actual_(std::move(actual)),
        asked_(std::move(asked)) {}

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    actual_->Load(decoder, stage);
    asked_->Load(decoder, stage);
  }

  const char* DisplayInline(SyscallDecoder* decoder, Stage stage, const char* separator,
                            fidl_codec::PrettyPrinter& printer) const override;

 private:
  // Current value.
  const std::unique_ptr<Access<Type>> actual_;
  // Value which has been asked or value that should have been asked.
  const std::unique_ptr<Access<Type>> asked_;
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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    buffer_->LoadArray(decoder, stage, sizeof(Type));
  }

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                   Stage stage) const override;

  const char* DisplayInline(SyscallDecoder* decoder, Stage stage, const char* separator,
                            fidl_codec::PrettyPrinter& printer) const override;

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

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                   Stage stage) const override;

  bool InlineValue() const override { return false; }

  void Load(SyscallDecoder* decoder, Stage stage) const override {
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

  void DisplayOutline(SyscallDecoder* decoder, Stage stage,
                      fidl_codec::PrettyPrinter& printer) const override;

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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
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

  void DisplayOutline(SyscallDecoder* decoder, Stage stage,
                      fidl_codec::PrettyPrinter& printer) const override;

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

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    string_size_->Load(decoder, stage);

    if (string_size_->Loaded(decoder, stage)) {
      size_t value = string_size_->Value(decoder, stage);
      if (value > 0) {
        string_->LoadArray(decoder, stage, value);
      }
    }
  }

  const char* DisplayInline(SyscallDecoder* decoder, Stage stage, const char* separator,
                            fidl_codec::PrettyPrinter& printer) const override;

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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    string_->LoadArray(decoder, stage, string_size_);
  }

  const char* DisplayInline(SyscallDecoder* decoder, Stage stage, const char* separator,
                            fidl_codec::PrettyPrinter& printer) const override;

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

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                   Stage stage) const override {
    const auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
    return class_definition_->GenerateValue(object, decoder->arch());
  }

  bool InlineValue() const override { return false; }

  void Load(SyscallDecoder* decoder, Stage stage) const override {
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

  void DisplayOutline(SyscallDecoder* decoder, Stage stage,
                      fidl_codec::PrettyPrinter& printer) const override;

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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    buffer_size_->Load(decoder, stage);

    if (buffer_size_->Loaded(decoder, stage)) {
      size_t value = buffer_size_->Value(decoder, stage);
      if (value > 0) {
        buffer_->LoadArray(decoder, stage, sizeof(ClassType) * value);
      }
    }
  }

  void DisplayOutline(SyscallDecoder* decoder, Stage stage,
                      fidl_codec::PrettyPrinter& printer) const override;

 private:
  // Access to the buffer (raw data) which contains the object.
  const std::unique_ptr<AccessBase> buffer_;
  // Access to the buffer size.
  const std::unique_ptr<Access<SizeType>> buffer_size_;
  // Class definition for the displayed object.
  const Class<ClassType>* class_definition_;
};

// An input/output which is a FIDL message. This is always displayed outline.
template <typename HandleType>
class SyscallFidlMessage : public SyscallInputOutputBase {
 public:
  SyscallFidlMessage(int64_t error_code, std::string_view name, fidl_codec::SyscallFidlType type,
                     std::unique_ptr<Access<zx_handle_t>> handle,
                     std::unique_ptr<Access<uint8_t>> bytes,
                     std::unique_ptr<Access<uint32_t>> num_bytes,
                     std::unique_ptr<Access<HandleType>> handles,
                     std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallInputOutputBase(error_code, name),
        type_(type),
        handle_(std::move(handle)),
        bytes_(std::move(bytes)),
        num_bytes_(std::move(num_bytes)),
        handles_(std::move(handles)),
        num_handles_(std::move(num_handles)) {}

  fidl_codec::SyscallFidlType type() const { return type_; }
  const Access<zx_handle_t>* handle() const { return handle_.get(); }
  const Access<uint8_t>* bytes() const { return bytes_.get(); }
  const Access<uint32_t>* num_bytes() const { return num_bytes_.get(); }
  const Access<HandleType>* handles() const { return handles_.get(); }
  const Access<uint32_t>* num_handles() const { return num_handles_.get(); }

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    handle_->Load(decoder, stage);
    num_bytes_->Load(decoder, stage);
    num_handles_->Load(decoder, stage);

    if (num_bytes_->Loaded(decoder, stage)) {
      uint32_t value = num_bytes_->Value(decoder, stage);
      if (value > 0) {
        bytes_->LoadArray(decoder, stage, value);
      }
    }

    if (num_handles_->Loaded(decoder, stage)) {
      uint32_t value = num_handles_->Value(decoder, stage);
      if (value > 0) {
        handles_->LoadArray(decoder, stage, value * sizeof(HandleType));
      }
    }
  }

 private:
  const fidl_codec::SyscallFidlType type_;
  const std::unique_ptr<Access<zx_handle_t>> handle_;
  const std::unique_ptr<Access<uint8_t>> bytes_;
  const std::unique_ptr<Access<uint32_t>> num_bytes_;
  const std::unique_ptr<Access<HandleType>> handles_;
  const std::unique_ptr<Access<uint32_t>> num_handles_;
};

class SyscallFidlMessageHandle : public SyscallFidlMessage<zx_handle_t> {
 public:
  SyscallFidlMessageHandle(int64_t error_code, std::string_view name,
                           fidl_codec::SyscallFidlType type,
                           std::unique_ptr<Access<zx_handle_t>> handle,
                           std::unique_ptr<Access<uint8_t>> bytes,
                           std::unique_ptr<Access<uint32_t>> num_bytes,
                           std::unique_ptr<Access<zx_handle_t>> handles,
                           std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessage<zx_handle_t>(error_code, name, type, std::move(handle), std::move(bytes),
                                        std::move(num_bytes), std::move(handles),
                                        std::move(num_handles)) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
                                                   Stage stage) const override;
};

class SyscallFidlMessageHandleInfo : public SyscallFidlMessage<zx_handle_info_t> {
 public:
  SyscallFidlMessageHandleInfo(int64_t error_code, std::string_view name,
                               fidl_codec::SyscallFidlType type,
                               std::unique_ptr<Access<zx_handle_t>> handle,
                               std::unique_ptr<Access<uint8_t>> bytes,
                               std::unique_ptr<Access<uint32_t>> num_bytes,
                               std::unique_ptr<Access<zx_handle_info_t>> handles,
                               std::unique_ptr<Access<uint32_t>> num_handles)
      : SyscallFidlMessage<zx_handle_info_t>(error_code, name, type, std::move(handle),
                                             std::move(bytes), std::move(num_bytes),
                                             std::move(handles), std::move(num_handles)) {}

  bool InlineValue() const override { return false; }

  std::unique_ptr<fidl_codec::Type> ComputeType() const override;

  std::unique_ptr<fidl_codec::Value> GenerateValue(SyscallDecoder* decoder,
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

  bool fidl_codec_values_ready() const { return fidl_codec_values_ready_; }
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
  [[nodiscard]] bool (SyscallDecoderDispatcher::*inputs_decoded_action() const)(SyscallDecoder*) {
    return inputs_decoded_action_;
  }
  void set_inputs_decoded_action(
      bool (SyscallDecoderDispatcher::*inputs_decoded_action)(SyscallDecoder* decoder)) {
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
  void InputFidlMessage(std::string_view name, fidl_codec::SyscallFidlType type,
                        std::unique_ptr<Access<zx_handle_t>> handle,
                        std::unique_ptr<Access<uint8_t>> bytes,
                        std::unique_ptr<Access<uint32_t>> num_bytes,
                        std::unique_ptr<Access<zx_handle_t>> handles,
                        std::unique_ptr<Access<uint32_t>> num_handles) {
    inputs_.push_back(std::make_unique<SyscallFidlMessageHandle>(
        0, name, type, std::move(handle), std::move(bytes), std::move(num_bytes),
        std::move(handles), std::move(num_handles)));
  }

  // Adds an inline output to display.
  template <typename Type>
  SyscallInputOutput<Type>* Output(int64_t error_code, std::string_view name,
                                   std::unique_ptr<Access<Type>> access) {
    auto object = std::make_unique<SyscallInputOutput<Type>>(error_code, name, std::move(access));
    auto result = object.get();
    outputs_.push_back(std::move(object));
    return result;
  }

  // Adds an inline output to display which is displayed like: actual/asked.
  template <typename Type>
  SyscallInputOutputActualAndRequested<Type>* OutputActualAndRequested(
      int64_t error_code, std::string_view name, std::unique_ptr<Access<Type>> actual,
      std::unique_ptr<Access<Type>> asked) {
    auto object = std::make_unique<SyscallInputOutputActualAndRequested<Type>>(
        error_code, name, std::move(actual), std::move(asked));
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
  void OutputFidlMessageHandle(int64_t error_code, std::string_view name,
                               fidl_codec::SyscallFidlType type,
                               std::unique_ptr<Access<zx_handle_t>> handle,
                               std::unique_ptr<Access<uint8_t>> bytes,
                               std::unique_ptr<Access<uint32_t>> num_bytes,
                               std::unique_ptr<Access<zx_handle_t>> handles,
                               std::unique_ptr<Access<uint32_t>> num_handles) {
    outputs_.push_back(std::make_unique<SyscallFidlMessageHandle>(
        error_code, name, type, std::move(handle), std::move(bytes), std::move(num_bytes),
        std::move(handles), std::move(num_handles)));
  }
  void OutputFidlMessageHandleInfo(int64_t error_code, std::string_view name,
                                   fidl_codec::SyscallFidlType type,
                                   std::unique_ptr<Access<zx_handle_t>> handle,
                                   std::unique_ptr<Access<uint8_t>> bytes,
                                   std::unique_ptr<Access<uint32_t>> num_bytes,
                                   std::unique_ptr<Access<zx_handle_info_t>> handles,
                                   std::unique_ptr<Access<uint32_t>> num_handles) {
    outputs_.push_back(std::make_unique<SyscallFidlMessageHandleInfo>(
        error_code, name, type, std::move(handle), std::move(bytes), std::move(num_bytes),
        std::move(handles), std::move(num_handles)));
  }

  // Computes all the fidl codec types for this syscall.
  void ComputeTypes();

  const fidl_codec::StructMember* SearchInlineMember(const std::string& name, bool invoked) const;
  const fidl_codec::StructMember* SearchInlineMember(uint32_t id, bool invoked) const;
  const fidl_codec::StructMember* SearchOutlineMember(const std::string& name, bool invoked) const;
  const fidl_codec::StructMember* SearchOutlineMember(uint32_t id, bool invoked) const;

  void ComputeStatistics(const OutputEvent* event) const;

 private:
  const std::string name_;
  const SyscallReturnType return_type_;
  const SyscallKind kind_;
  const std::string breakpoint_name_;
  std::vector<std::unique_ptr<SyscallArgumentBase>> arguments_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> inputs_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> outputs_;
  bool fidl_codec_values_ready_ = false;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> input_inline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> input_outline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> output_inline_members_;
  std::vector<std::unique_ptr<fidl_codec::StructMember>> output_outline_members_;
  bool (SyscallDecoderDispatcher::*inputs_decoded_action_)(SyscallDecoder* decoder) = nullptr;
  void (SyscallDecoderDispatcher::*inference_)(
      const OutputEvent* event, const fidl_codec::semantic::MethodSemantic* semantic) = nullptr;
  // Method which can compute statistics for the syscall.
  void (*compute_statistics_)(const OutputEvent* event) = nullptr;
};

// Decoder for syscalls. This creates the breakpoints for all the syscalls we
// want to monitor. Then, each time a breakpoint is reached, it creates a
// SyscallDecoder object which will handle the decoding of one syscall.
class SyscallDecoderDispatcher {
 public:
  explicit SyscallDecoderDispatcher(const DecodeOptions& decode_options);
  virtual ~SyscallDecoderDispatcher() = default;

  const DecodeOptions& decode_options() const { return decode_options_; }

  int64_t startup_timestamp() const { return startup_timestamp_; }

  const std::map<std::string, std::unique_ptr<Syscall>>& syscalls() const { return syscalls_; }

  const std::map<zx_koid_t, std::unique_ptr<Process>>& processes() const { return processes_; }

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
    auto thread = std::make_unique<Thread>(process, koid);
    auto returned_value = thread.get();
    threads_.emplace(std::make_pair(koid, std::move(thread)));
    return returned_value;
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
                     Syscall* syscall);

  // Decode an exception received by a thread.
  void DecodeException(InterceptionWorkflow* workflow, zxdb::Thread* thread);

  virtual fidl_codec::MessageDecoderDispatcher* MessageDecoderDispatcher() { return nullptr; }

  // Called when we are watching a process we launched.
  virtual void AddLaunchedProcess(uint64_t process_koid) {}

  // Create the object which will decode the syscall.
  virtual std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                        zxdb::Thread* thread,
                                                        const Syscall* syscall) = 0;

  // Delete a decoder created by DecodeSyscall. Called when the syscall is
  // fully decoded and displayed or the syscalls had an error.
  virtual void DeleteDecoder(SyscallDecoder* decoder);

  // Create the object which will decode the exception.
  virtual std::unique_ptr<ExceptionDecoder> CreateDecoder(InterceptionWorkflow* workflow,
                                                          zxdb::Thread* thread) = 0;

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
  bool ExtractHandleInfos(SyscallDecoder* decoder) {
    inference_.ExtractHandleInfos(decoder);
    return false;
  }

  // Called when we intercept __libc_extensions_init.
  bool LibcExtensionsInit(SyscallDecoder* decoder) {
    inference_.LibcExtensionsInit(decoder);
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

  // When fidlcat has started.
  const int64_t startup_timestamp_;

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

  const SyscallDisplay* last_displayed_syscall() const { return last_displayed_syscall_; }
  void set_last_displayed_syscall(const SyscallDisplay* last_displayed_syscall) {
    last_displayed_syscall_ = last_displayed_syscall;
  }

  const Event* last_displayed_event() const { return last_displayed_event_; }
  void clear_last_displayed_event() { last_displayed_event_ = nullptr; }

  bool dump_messages() const { return dump_messages_; }

  uint32_t GetNextInvokedEventId() { return next_invoked_event_id_++; }

  fidl_codec::MessageDecoderDispatcher* MessageDecoderDispatcher() override {
    return &message_decoder_dispatcher_;
  }

  void AddLaunchedProcess(uint64_t process_koid) override {
    message_decoder_dispatcher_.AddLaunchedProcess(process_koid);
  }

  std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                zxdb::Thread* thread,
                                                const Syscall* syscall) override;

  std::unique_ptr<ExceptionDecoder> CreateDecoder(InterceptionWorkflow* workflow,
                                                  zxdb::Thread* thread) override;

  void AddProcessLaunchedEvent(std::shared_ptr<ProcessLaunchedEvent> event) override;

  void AddProcessMonitoredEvent(std::shared_ptr<ProcessMonitoredEvent> event) override;

  void AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) override;

  void AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) override;

  // Displays an invoked event.
  void DisplayInvokedEvent(const InvokedEvent* invoked_event);

  void AddOutputEvent(std::shared_ptr<OutputEvent> output_event) override;

  void AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) override;

  void SessionEnded() override;

  void DisplaySummary(std::ostream& os);

  void DisplayTop(std::ostream& os);

  void GenerateTests(const std::string& output_directory);

 private:
  // Class which can decode a FIDL message.
  fidl_codec::MessageDecoderDispatcher message_decoder_dispatcher_;
  // The last syscall we displayed the inputs on the stream.
  const SyscallDisplay* last_displayed_syscall_ = nullptr;
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

  std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                zxdb::Thread* thread,
                                                const Syscall* syscall) override;

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

template <typename Type>
inline std::unique_ptr<fidl_codec::Value> GenerateHandleValue(Type handle) {
  return std::make_unique<fidl_codec::InvalidValue>();
}

template <>
inline std::unique_ptr<fidl_codec::Value> GenerateHandleValue(zx_handle_t handle) {
  zx_handle_info_t info;
  info.handle = handle;
  info.type = ZX_OBJ_TYPE_NONE;
  info.rights = 0;
  return std::make_unique<fidl_codec::HandleValue>(info);
}

// Display a value on a stream.
template <typename ValueType>
void DisplayValue(SyscallType type, ValueType /*value*/, fidl_codec::PrettyPrinter& printer) {
  printer << "unimplemented generic value " << static_cast<uint32_t>(type);
}

template <>
inline void DisplayValue<bool>(SyscallType type, bool value, fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kBool:
      printer << fidl_codec::Blue << (value ? "true" : "false") << fidl_codec::ResetColor;
      break;
    default:
      printer << "unimplemented bool value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<int32_t>(SyscallType type, int32_t value,
                                  fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kInt32:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kFutex:
      printer << fidl_codec::Red << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kStatus:
      printer.DisplayStatus(value);
      break;
    default:
      printer << "unimplemented int32_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<int64_t>(SyscallType type, int64_t value,
                                  fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kInt64:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kDuration:
      printer << DisplayDuration(value);
      break;
    case SyscallType::kFutex:
      printer << fidl_codec::Red << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kMonotonicTime:
      printer << DisplayDuration(value);
      break;
    case SyscallType::kTime:
      printer << DisplayTime(value);
      break;
    default:
      printer << "unimplemented int64_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint8_t>(SyscallType type, uint8_t value,
                                  fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kUint8:
      printer << fidl_codec::Blue << static_cast<uint32_t>(value) << fidl_codec::ResetColor;
      break;
    case SyscallType::kUint8Hexa:
      printer.DisplayHexa8(value);
      break;
    case SyscallType::kPacketGuestVcpuType:
      printer.DisplayPacketGuestVcpuType(value);
      break;
    default:
      printer << "unimplemented uint8_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint16_t>(SyscallType type, uint16_t value,
                                   fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kUint16:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kUint16Hexa:
      printer.DisplayHexa16(value);
      break;
    case SyscallType::kPacketPageRequestCommand:
      printer.DisplayPacketPageRequestCommand(value);
      break;
    default:
      printer << "unimplemented uint16_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint32_t>(SyscallType type, uint32_t value,
                                   fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kUint32:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kUint32Hexa:
      printer.DisplayHexa32(value);
      break;
    case SyscallType::kBtiPerm:
      printer.DisplayBtiPerm(value);
      break;
    case SyscallType::kCachePolicy:
      printer.DisplayCachePolicy(value);
      break;
    case SyscallType::kClock:
      printer.DisplayClock(value);
      break;
    case SyscallType::kExceptionChannelType:
      printer << fidl_codec::Blue;
      ExceptionChannelTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kExceptionState:
      printer.DisplayExceptionState(value);
      break;
    case SyscallType::kFeatureKind:
      printer << fidl_codec::Red;
      FeatureKindName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kGuestTrap:
      printer << fidl_codec::Red;
      GuestTrapName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kHandle: {
      zx_handle_info_t handle_info;
      handle_info.handle = value;
      handle_info.type = ZX_OBJ_TYPE_NONE;
      handle_info.rights = 0;
      printer.DisplayHandle(handle_info);
      break;
    }
    case SyscallType::kInfoMapsType:
      printer << fidl_codec::Red;
      InfoMapsTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kInterruptFlags:
      printer << fidl_codec::Red;
      InterruptFlagsName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kIommuType:
      printer << fidl_codec::Red;
      IommuTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kKtraceControlAction:
      printer << fidl_codec::Blue;
      KtraceControlActionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kObjectInfoTopic:
      printer.DisplayObjectInfoTopic(value);
      break;
    case SyscallType::kObjProps:
      printer << fidl_codec::Blue;
      ObjPropsName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kObjType:
      printer << fidl_codec::Blue;
      fidl_codec::ObjTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kPciBarType:
      printer.DisplayPciBarType(value);
      break;
    case SyscallType::kPolicyAction:
      printer << fidl_codec::Blue;
      PolicyActionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kPolicyCondition:
      printer << fidl_codec::Blue;
      PolicyConditionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kPolicyTopic:
      printer << fidl_codec::Blue;
      PolicyTopicName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kPortPacketType:
      printer.DisplayPortPacketType(value);
      break;
    case SyscallType::kProfileInfoFlags:
      printer.DisplayProfileInfoFlags(value);
      break;
    case SyscallType::kPropType:
      printer.DisplayPropType(value);
      break;
    case SyscallType::kRights:
      printer.DisplayRights(value);
      break;
    case SyscallType::kRsrcKind:
      printer << fidl_codec::Blue;
      RsrcKindName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kSignals:
      printer.DisplaySignals(value);
      break;
    case SyscallType::kSocketCreateOptions:
      printer << fidl_codec::Blue;
      SocketCreateOptionsName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kSocketReadOptions:
      printer << fidl_codec::Blue;
      SocketReadOptionsName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kSocketShutdownOptions:
      printer << fidl_codec::Blue;
      SocketShutdownOptionsName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kSystemEventType:
      printer << fidl_codec::Blue;
      SystemEventTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kSystemPowerctl:
      printer << fidl_codec::Blue;
      SystemPowerctlName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kThreadState:
      printer << fidl_codec::Blue;
      ThreadStateName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kThreadStateTopic:
      printer << fidl_codec::Blue;
      ThreadStateTopicName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kTimerOption:
      printer << fidl_codec::Blue;
      TimerOptionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVcpu:
      printer << fidl_codec::Red;
      VcpuName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVmOption:
      printer << fidl_codec::Red;
      VmOptionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVmoCreationOption:
      printer << fidl_codec::Blue;
      VmoCreationOptionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVmoOp:
      printer << fidl_codec::Blue;
      VmoOpName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVmoOption:
      printer << fidl_codec::Blue;
      VmoOptionName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    case SyscallType::kVmoType:
      printer << fidl_codec::Blue;
      VmoTypeName(value, printer);
      printer << fidl_codec::ResetColor;
      break;
    default:
      printer << "unimplemented uint32_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint64_t>(SyscallType type, uint64_t value,
                                   fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kUint64:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kUint64Hexa:
      printer.DisplayHexa64(value);
      break;
#ifndef __MACH__
    case SyscallType::kGpAddr:
      printer.DisplayGpAddr(value);
      break;
#endif
    case SyscallType::kKoid:
      printer << fidl_codec::Red << value << fidl_codec::ResetColor;
      break;
#ifndef __MACH__
    case SyscallType::kSize:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
#endif
    case SyscallType::kTime:
      printer << DisplayTime(value);
      break;
    case SyscallType::kPaddr:
      printer.DisplayPaddr(value);
      break;
#ifndef __MACH__
    case SyscallType::kUintptr:
      printer.DisplayUintptr(value);
      break;
    case SyscallType::kVaddr:
      printer.DisplayVaddr(value);
      break;
#endif
    default:
      printer << "unimplemented uint64_t value " << static_cast<uint32_t>(type);
      break;
  }
}

#ifdef __MACH__
template <>
inline void DisplayValue<uintptr_t>(SyscallType type, uintptr_t value,
                                    fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kGpAddr:
      printer.DisplayGpAddr(value);
      break;
    case SyscallType::kSize:
      printer << fidl_codec::Blue << value << fidl_codec::ResetColor;
      break;
    case SyscallType::kPaddr:
      printer.DisplayPaddr(value);
      break;
    case SyscallType::kUintptr:
      printer.DisplayUintptr(value);
      break;
    case SyscallType::kVaddr:
      printer.DisplayVaddr(value);
      break;
    default:
      printer << "unimplemented uintptr_t value " << static_cast<uint32_t>(type);
      break;
  }
}
#endif

template <>
inline void DisplayValue<zx_uint128_t>(SyscallType type, zx_uint128_t value,
                                       fidl_codec::PrettyPrinter& printer) {
  switch (type) {
    case SyscallType::kUint128Hexa: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016" PRIx64, value.low);
      printer << fidl_codec::Blue << "{ low = " << buffer.data();
      snprintf(buffer.data(), buffer.size(), "%016" PRIx64, value.high);
      printer << ", high = " << buffer.data() << " }" << fidl_codec::ResetColor;
      break;
    }
    default:
      printer << "unimplemented zx_uint128_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <typename ClassType, typename Type>
bool ClassFieldCondition<ClassType, Type>::True(const ClassType* object, debug_ipc::Arch /*arch*/) {
  return field_->get()(object) == value_;
}

template <typename ClassType, typename Type>
bool ClassFieldMaskedCondition<ClassType, Type>::True(const ClassType* object,
                                                      debug_ipc::Arch /*arch*/) {
  return (field_->get()(object) & mask_) == value_;
}

template <typename ClassType, typename Type>
bool ArchCondition<ClassType, Type>::True(const ClassType* /*object*/, debug_ipc::Arch arch) {
  return arch_ == arch;
}

template <typename ClassType, typename Type>
void ClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch /*arch*/,
                                          fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name();
  DisplayType(ClassFieldBase<ClassType>::syscall_type(), printer);
  DisplayValue<Type>(ClassFieldBase<ClassType>::syscall_type(), get_(object), printer);
  printer << '\n';
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> ClassField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug_ipc::Arch arch) const {
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
void ArrayField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch /*arch*/,
                                          fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name() << ": array<" << fidl_codec::Green
          << TypeName(ClassFieldBase<ClassType>::syscall_type()) << fidl_codec::ResetColor
          << "> = [";
  const char* separator = " ";
  std::pair<const Type*, int> array = get_(object);
  for (int i = 0; i < array.second; ++i) {
    printer << separator;
    DisplayValue<Type>(ClassFieldBase<ClassType>::syscall_type(), array.first[i], printer);
    separator = ", ";
  }
  printer << " ]\n";
}

template <typename Type, typename SizeType>
inline void DisplayArrayValue(fidl_codec::PrettyPrinter& printer, SyscallType syscall_type,
                              const Type* vector, const SizeType size) {
  printer << "[";
  const char* separator = " ";
  for (SizeType i = 0; i < size; ++i) {
    printer << separator;
    DisplayValue<Type>(syscall_type, vector[i], printer);
    separator = ", ";
  }
  printer << " ]\n";
}

template <>
inline void DisplayArrayValue<char, size_t>(fidl_codec::PrettyPrinter& printer,
                                            SyscallType syscall_type, const char* vector,
                                            const size_t size) {
  printer.DisplayString(std::string_view(vector, size));
  printer << "\n";
}

template <typename ClassType, typename Type, typename SizeType>
void DynamicArrayField<ClassType, Type, SizeType>::Display(
    const ClassType* object, debug_ipc::Arch /*arch*/, fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name() << ": ";
  printer << "vector<";
  printer << fidl_codec::Green;
  printer << TypeName(ClassFieldBase<ClassType>::syscall_type());
  printer << fidl_codec::ResetColor << "> = ";

  std::pair<const Type*, SizeType> vector_and_size = get_(object);
  DisplayArrayValue<Type, SizeType>(printer, ClassFieldBase<ClassType>::syscall_type(),
                                    vector_and_size.first, vector_and_size.second);
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> ArrayField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug_ipc::Arch arch) const {
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
    const ClassType* object, debug_ipc::Arch arch) const {
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  std::pair<const Type*, SizeType> vector = get_(object);
  auto syscall_type_ = this->syscall_type();

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
void ClassClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                               fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name() << ": " << fidl_codec::Green << field_class_->name()
          << fidl_codec::ResetColor << " = ";
  const Type* sub_object = get_(object);
  field_class_->DisplayObject(sub_object, arch, printer);
  printer << '\n';
}

template <typename ClassType, typename Type>
void ArrayClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                               fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name() << ": array<" << fidl_codec::Green
          << sub_class_->name() << fidl_codec::ResetColor << "> = [\n";
  {
    fidl_codec::Indent indent(printer);
    std::pair<const Type*, int> array = get_(object);
    for (int i = 0; i < array.second; ++i) {
      sub_class_->DisplayObject(array.first + i, arch, printer);
      printer << '\n';
    }
  }
  printer << "]\n";
}

template <typename ClassType, typename Type>
void DynamicArrayClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                                      fidl_codec::PrettyPrinter& printer) const {
  printer << ClassFieldBase<ClassType>::name() << ": vector<" << fidl_codec::Green
          << sub_class_->name() << fidl_codec::ResetColor << "> = [\n";
  {
    fidl_codec::Indent indent(printer);
    const Type* array = get_(object);
    uint32_t size = get_size_(object);
    for (uint32_t i = 0; i < size; ++i) {
      sub_class_->DisplayObject(array + i, arch, printer);
      printer << '\n';
    }
  }
  printer << "]\n";
}

template <typename ClassType, typename Type>
std::unique_ptr<fidl_codec::Value> DynamicArrayClassField<ClassType, Type>::GenerateValue(
    const ClassType* object, debug_ipc::Arch arch) const {
  auto vector_value = std::make_unique<fidl_codec::VectorValue>();
  const Type* array = get_(object);
  uint32_t size = get_size_(object);

  for (uint32_t i = 0; i < size; ++i) {
    vector_value->AddValue(sub_class_->GenerateValue(array + i, arch));
  }

  return vector_value;
}

template <typename Type>
std::unique_ptr<fidl_codec::Value> Access<Type>::GenerateValue(SyscallDecoder* decoder,
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
void Access<Type>::Display(SyscallDecoder* decoder, Stage stage, std::string_view name,
                           fidl_codec::PrettyPrinter& printer) const {
  printer << name;
  DisplayType(GetSyscallType(), printer);
  if (ValueValid(decoder, stage)) {
    DisplayValue<Type>(GetSyscallType(), Value(decoder, stage), printer);
  } else {
    printer << fidl_codec::Red << "(nullptr)" << fidl_codec::ResetColor;
  }
}

template <typename Type>
const char* SyscallInputOutputActualAndRequested<Type>::DisplayInline(
    SyscallDecoder* decoder, Stage stage, const char* separator,
    fidl_codec::PrettyPrinter& printer) const {
  printer << separator;
  actual_->Display(decoder, stage, name(), printer);
  printer << "/";
  if (asked_->ValueValid(decoder, stage)) {
    DisplayValue<Type>(asked_->GetSyscallType(), asked_->Value(decoder, stage), printer);
  } else {
    printer << fidl_codec::Red << "(nullptr)" << fidl_codec::ResetColor;
  }
  return ", ";
}

template <typename Type, typename FromType>
const char* SyscallInputOutputIndirect<Type, FromType>::DisplayInline(
    SyscallDecoder* decoder, Stage stage, const char* separator,
    fidl_codec::PrettyPrinter& printer) const {
  printer << separator << name();
  DisplayType(syscall_type_, printer);
  const FromType* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    DisplayValue<Type>(syscall_type_, *reinterpret_cast<const Type*>(buffer), printer);
  }
  return ", ";
}

template <typename Type, typename FromType>
std::unique_ptr<fidl_codec::Value> SyscallInputOutputIndirect<Type, FromType>::GenerateValue(
    SyscallDecoder* decoder, Stage stage) const {
  const FromType* buffer = buffer_->Content(decoder, stage);
  return fidlcat::GenerateValue<Type>(*reinterpret_cast<const Type*>(buffer));
}

template <typename Type, typename FromType, typename SizeType>
void SyscallInputOutputBuffer<Type, FromType, SizeType>::DisplayOutline(
    SyscallDecoder* decoder, Stage stage, fidl_codec::PrettyPrinter& printer) const {
  fidl_codec::Indent indent(printer);
  printer << name();
  DisplayType(syscall_type_, printer);
  const FromType* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    size_t buffer_size = elem_size_->Value(decoder, stage);
    if (elem_count_ != nullptr) {
      buffer_size *= elem_count_->Value(decoder, stage);
    }
    if (buffer_size == 0) {
      printer << "empty\n";
      return;
    }
    const char* separator = "";
    for (size_t i = 0; i < buffer_size; ++i) {
      printer << separator;
      DisplayValue<Type>(syscall_type_, reinterpret_cast<const Type*>(buffer)[i], printer);
      separator = ", ";
    }
  }
  printer << '\n';
}

template <>
inline void SyscallInputOutputBuffer<uint8_t, uint8_t, size_t>::DisplayOutline(
    SyscallDecoder* decoder, Stage stage, fidl_codec::PrettyPrinter& printer) const {
  fidl_codec::Indent indent(printer);
  printer << name();
  DisplayType(syscall_type_, printer);
  const uint8_t* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    size_t buffer_size = elem_size_->Value(decoder, stage);
    if (elem_count_ != nullptr) {
      buffer_size *= elem_count_->Value(decoder, stage);
    }
    if (buffer_size == 0) {
      printer << "empty\n";
      return;
    }
    for (size_t i = 0;; ++i) {
      if (i == buffer_size) {
        printer << fidl_codec::Red << '"';
        for (size_t i = 0; i < buffer_size; ++i) {
          char value = reinterpret_cast<const char*>(buffer)[i];
          switch (value) {
            case 0:
              break;
            case '\\':
              printer << "\\\\";
              break;
            case '\n':
              printer << "\\n";
              break;
            default:
              printer << value;
              break;
          }
        }
        printer << '"' << fidl_codec::ResetColor << '\n';
        return;
      }
      if ((buffer[i] == 0) && (i != buffer_size - 1)) {
        break;
      }
      if (!std::isprint(buffer[i]) && (buffer[i] != '\n')) {
        break;
      }
    }
    const char* separator = "";
    for (size_t i = 0; i < buffer_size; ++i) {
      printer << separator;
      DisplayValue<uint8_t>(buffer_->GetSyscallType(), buffer[i], printer);
      separator = ", ";
    }
  }
  printer << '\n';
}

template <typename Type, typename FromType, typename SizeType>
std::unique_ptr<fidl_codec::Value>
SyscallInputOutputBuffer<Type, FromType, SizeType>::GenerateValue(SyscallDecoder* decoder,
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

template <typename FromType>
const char* SyscallInputOutputString<FromType>::DisplayInline(
    SyscallDecoder* decoder, Stage stage, const char* separator,
    fidl_codec::PrettyPrinter& printer) const {
  printer << separator;
  printer << name() << ": " << fidl_codec::Green << "string" << fidl_codec::ResetColor << " = ";
  const char* string = reinterpret_cast<const char*>(string_->Content(decoder, stage));
  size_t string_size = string_size_->Value(decoder, stage);
  printer.DisplayString(std::string_view(string, string_size));
  return ", ";
}

template <typename ClassType, typename SizeType>
void SyscallInputOutputObject<ClassType, SizeType>::DisplayOutline(
    SyscallDecoder* decoder, Stage stage, fidl_codec::PrettyPrinter& printer) const {
  fidl_codec::Indent indent(printer);
  printer << name() << ": " << fidl_codec::Green << class_definition_->name()
          << fidl_codec::ResetColor << " = ";
  auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
  if (object == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    class_definition_->DisplayObject(object, decoder->arch(), printer);
  }
  printer << '\n';
}

template <typename ClassType, typename SizeType>
void SyscallInputOutputObjectArray<ClassType, SizeType>::DisplayOutline(
    SyscallDecoder* decoder, Stage stage, fidl_codec::PrettyPrinter& printer) const {
  fidl_codec::Indent indent(printer);
  printer << name() << ": vector<" << fidl_codec::Green << class_definition_->name()
          << fidl_codec::ResetColor << "> = ";
  auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
  if (object == nullptr) {
    printer << fidl_codec::Red << "nullptr" << fidl_codec::ResetColor;
  } else {
    printer << " [";
    SizeType count = buffer_size_->Value(decoder, stage);
    const char* separator = "\n";
    for (SizeType i = 0; i < count; ++i) {
      printer << separator;
      fidl_codec::Indent indent(printer);
      class_definition_->DisplayObject(object + i, decoder->arch(), printer);
      separator = ",\n";
    }
    printer << '\n';
    printer << ']';
  }
  printer << '\n';
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
