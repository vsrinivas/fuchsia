// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_

#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fidl_codec/display_handle.h"
#include "src/lib/fidl_codec/display_options.h"
#include "src/lib/fidl_codec/message_decoder.h"
#include "src/lib/fxl/logging.h"
#include "tools/fidlcat/lib/decode_options.h"
#include "tools/fidlcat/lib/exception_decoder.h"
#include "tools/fidlcat/lib/inference.h"
#include "tools/fidlcat/lib/syscall_decoder.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

template <typename ClassType, typename Type>
class ClassField;

template <typename ClassType>
class Class;

void DisplayString(const fidl_codec::Colors& colors, const char* string, size_t size,
                   std::ostream& os);

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
                       SyscallDecoderDispatcher* dispatcher, const fidl_codec::Colors& colors,
                       std::string_view line_header, int tabs, std::ostream& os) const = 0;

 private:
  std::string name_;
  const SyscallType syscall_type_;
  std::vector<std::unique_ptr<ClassFieldConditionBase<ClassType>>> conditions_;
};

// Define a class field for basic types.
template <typename ClassType, typename Type>
class ClassField : public ClassFieldBase<ClassType> {
 public:
  ClassField(std::string_view name, SyscallType syscall_type, Type (*get)(const ClassType* from))
      : ClassFieldBase<ClassType>(name, syscall_type), get_(get) {}

  Type (*get() const)(const ClassType* from) { return get_; }

  void Display(const ClassType* object, debug_ipc::Arch arch, SyscallDecoderDispatcher* dispatcher,
               const fidl_codec::Colors& colors, std::string_view line_header, int tabs,
               std::ostream& os) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch, SyscallDecoderDispatcher* dispatcher,
               const fidl_codec::Colors& colors, std::string_view line_header, int tabs,
               std::ostream& os) const;

 private:
  // Function which can extract the address of the field for a given object.
  std::pair<const Type*, int> (*get_)(const ClassType* from);
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

  void Display(const ClassType* object, debug_ipc::Arch arch, SyscallDecoderDispatcher* dispatcher,
               const fidl_codec::Colors& colors, std::string_view line_header, int tabs,
               std::ostream& os) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch, SyscallDecoderDispatcher* dispatcher,
               const fidl_codec::Colors& colors, std::string_view line_header, int tabs,
               std::ostream& os) const override;

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

  void Display(const ClassType* object, debug_ipc::Arch arch, SyscallDecoderDispatcher* dispatcher,
               const fidl_codec::Colors& colors, std::string_view line_header, int tabs,
               std::ostream& os) const override;

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

  void DisplayObject(const ClassType* object, debug_ipc::Arch arch,
                     SyscallDecoderDispatcher* dispatcher, const fidl_codec::Colors& colors,
                     std::string_view line_header, int tabs, std::ostream& os) const {
    os << "{\n";
    for (const auto& field : fields_) {
      if (field->ConditionsAreTrue(object, arch)) {
        field->Display(object, arch, dispatcher, colors, line_header, tabs + 1, os);
      }
    }
    os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ') << "}";
  }

  template <typename Type>
  ClassField<ClassType, Type>* AddField(std::unique_ptr<ClassField<ClassType, Type>> field) {
    auto result = field.get();
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ArrayField<ClassType, Type>* AddField(std::unique_ptr<ArrayField<ClassType, Type>> field) {
    auto result = field.get();
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ClassClassField<ClassType, Type>* AddField(
      std::unique_ptr<ClassClassField<ClassType, Type>> field) {
    auto result = field.get();
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  ArrayClassField<ClassType, Type>* AddField(
      std::unique_ptr<ArrayClassField<ClassType, Type>> field) {
    auto result = field.get();
    fields_.push_back(std::move(field));
    return result;
  }

  template <typename Type>
  DynamicArrayClassField<ClassType, Type>* AddField(
      std::unique_ptr<DynamicArrayClassField<ClassType, Type>> field) {
    auto result = field.get();
    fields_.push_back(std::move(field));
    return result;
  }

 protected:
  explicit Class(std::string_view name) : name_(name) {}
  Class(const Class&) = delete;
  Class& operator=(const Class&) = delete;

 private:
  // Name of the class.
  std::string name_;
  // List of all fields in the class. Some fields can be specified several times
  // with different conditions.
  std::vector<std::unique_ptr<ClassFieldBase<ClassType>>> fields_;
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

  // Display the data on a stream (with name and type).
  void Display(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
               std::string_view name, std::ostream& os) const;
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

  // Add a condition which must be true to display the input/output.
  template <typename Type>
  SyscallInputOutputBase* DisplayIfEqual(std::unique_ptr<Access<Type>> access, Type value) {
    conditions_.push_back(
        std::make_unique<SyscallInputOutputCondition<Type>>(std::move(access), value));
    return this;
  }

  // Define the architecture needed to display the input/output.
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

  // Displays small inputs or outputs.
  virtual const char* DisplayInline(SyscallDisplayDispatcher* /*dispatcher*/,
                                    SyscallDecoder* /*decoder*/, Stage /*stage*/,
                                    const char* separator, std::ostream& /*os*/) const {
    return separator;
  }

  // Displays large (multi lines) inputs or outputs.
  virtual void DisplayOutline(SyscallDisplayDispatcher* /*dispatcher*/, SyscallDecoder* /*decoder*/,
                              Stage /*stage*/, std::string_view /*line_header*/, int /*tabs*/,
                              std::ostream& /*os*/) const {}

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
};

// An input/output which only displays an expression (for example, the value of
// an argument). This is always displayed inline.
template <typename Type>
class SyscallInputOutput : public SyscallInputOutputBase {
 public:
  SyscallInputOutput(int64_t error_code, std::string_view name,
                     std::unique_ptr<Access<Type>> access)
      : SyscallInputOutputBase(error_code, name), access_(std::move(access)) {}

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    access_->Load(decoder, stage);
  }

  const char* DisplayInline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                            Stage stage, const char* separator, std::ostream& os) const override {
    os << separator;
    access_->Display(dispatcher, decoder, stage, name(), os);
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

  const char* DisplayInline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                            Stage stage, const char* separator, std::ostream& os) const override;

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

  void Load(SyscallDecoder* decoder, Stage stage) const override {
    SyscallInputOutputBase::Load(decoder, stage);
    buffer_->LoadArray(decoder, stage, sizeof(Type));
  }

  const char* DisplayInline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                            Stage stage, const char* separator, std::ostream& os) const override;

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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;

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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;

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

  const char* DisplayInline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                            Stage stage, const char* separator, std::ostream& os) const override;

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

  const char* DisplayInline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                            Stage stage, const char* separator, std::ostream& os) const override;

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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;

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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;

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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;
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

  void DisplayOutline(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
                      std::string_view line_header, int tabs, std::ostream& os) const override;
};

// Defines a syscall we want to decode/display.
class Syscall {
 public:
  Syscall(std::string_view name, SyscallReturnType return_type, bool is_function)
      : name_(name),
        return_type_(return_type),
        is_function_(is_function),
        breakpoint_name_(is_function_ ? name_ : name_ + "@plt") {}

  // Name of the syscall.
  [[nodiscard]] const std::string& name() const { return name_; }

  // Type of the syscall returned value.
  [[nodiscard]] SyscallReturnType return_type() const { return return_type_; }

  // True if this class describes a regular function and not a syscall.
  bool is_function() const { return is_function_; }

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

  // The code to execute when the input is decoded and before the input is displayed.
  // If it exists and returns false, the input is not displayed.
  [[nodiscard]] bool (SyscallDecoderDispatcher::*inputs_decoded_action() const)(SyscallDecoder*) {
    return inputs_decoded_action_;
  }
  void set_inputs_decoded_action(
      bool (SyscallDecoderDispatcher::*inputs_decoded_action)(SyscallDecoder* decoder)) {
    inputs_decoded_action_ = inputs_decoded_action;
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

 private:
  const std::string name_;
  const SyscallReturnType return_type_;
  const bool is_function_;
  const std::string breakpoint_name_;
  std::vector<std::unique_ptr<SyscallArgumentBase>> arguments_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> inputs_;
  std::vector<std::unique_ptr<SyscallInputOutputBase>> outputs_;
  bool (SyscallDecoderDispatcher::*inputs_decoded_action_)(SyscallDecoder* decoder) = nullptr;
};

// Decoder for syscalls. This creates the breakpoints for all the syscalls we
// want to monitor. Then, each time a breakpoint is reached, it creates a
// SyscallDecoder object which will handle the decoding of one syscall.
class SyscallDecoderDispatcher {
 public:
  explicit SyscallDecoderDispatcher(const DecodeOptions& decode_options)
      : decode_options_(decode_options) {
    Populate();
  }
  virtual ~SyscallDecoderDispatcher() = default;

  const DecodeOptions& decode_options() const { return decode_options_; }

  const std::vector<std::unique_ptr<Syscall>>& syscalls() const { return syscalls_; }

  const Inference& inference() const { return inference_; }

  // Display a handle. Also display the data we have inferered for this handle (if any).
  void DisplayHandle(zx_handle_t handle, const fidl_codec::Colors& colors, std::ostream& os);

  // Decode an intercepted system call.
  // Called when a thread reached a breakpoint on a system call.
  // This will only start the decoding. The display will be done when all the
  // needed information will be gathered.
  void DecodeSyscall(InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
                     Syscall* syscall);

  // Decode an exception received by a thread.
  void DecodeException(InterceptionWorkflow* workflow, zxdb::Thread* thread);

  // Called when we are watching a process we launched.
  virtual void AddLaunchedProcess(uint64_t process_koid) {}

  // Create the object which will decode the syscall.
  virtual std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                        zxdb::Thread* thread, uint64_t thread_id,
                                                        const Syscall* syscall) = 0;

  // Delete a decoder created by DecodeSyscall. Called when the syscall is
  // fully decoded and displayed or the syscalls had an error.
  virtual void DeleteDecoder(SyscallDecoder* decoder);

  // Create the object which will decode the exception.
  virtual std::unique_ptr<ExceptionDecoder> CreateDecoder(InterceptionWorkflow* workflow,
                                                          zxdb::Thread* thread,
                                                          uint64_t thread_id) = 0;

  // Delete a decoder created by DecodeException. Called when the exception is fully decoded and
  // displayed or the exception had an error.
  virtual void DeleteDecoder(ExceptionDecoder* decoder);

  // Called when a process is launched (by using the run option). If |error_message| is not empty,
  // the the process didn't launch and |error_message| explains why.
  virtual void ProcessLaunched(const std::string& command, std::string_view error_message) {}

  // Called when a process is monitored. If |error_message| is not empty, we haven't been able to
  // monitor the process.
  virtual void ProcessMonitored(std::string_view name, zx_koid_t koid,
                                std::string_view error_message) {}

  // Called when a process is no longer monitored.
  virtual void StopMonitoring(zx_koid_t koid) {}

 private:
  // Feeds syscalls_ with all the syscalls we can decode.
  void Populate();

  // Add a function we want to put a breakpoint on. Used by Populate.
  Syscall* AddFunction(std::string_view name, SyscallReturnType return_type) {
    auto syscall = std::make_unique<Syscall>(name, return_type, /*is_function=*/true);
    auto result = syscall.get();
    syscalls_.push_back(std::move(syscall));
    return result;
  }

  // Add a syscall. Used by Populate.
  Syscall* Add(std::string_view name, SyscallReturnType return_type) {
    auto syscall = std::make_unique<Syscall>(name, return_type, /*is_function=*/false);
    auto result = syscall.get();
    syscalls_.push_back(std::move(syscall));
    return result;
  }

  // Called when we intercept processargs_extract_handles.
  bool ExtractHandles(SyscallDecoder* decoder) {
    inference_.ExtractHandles(decoder);
    return false;
  }

  // Called when we intercept __libc_extensions_init.
  bool LibcExtensionsInit(SyscallDecoder* decoder) {
    inference_.LibcExtensionsInit(decoder);
    return false;
  }

  // Decoding options.
  const DecodeOptions& decode_options_;

  // The definition of all the syscalls we can decode.
  std::vector<std::unique_ptr<Syscall>> syscalls_;

  // The intercepted syscalls we are currently decoding.
  std::map<uint64_t, std::unique_ptr<SyscallDecoder>> syscall_decoders_;

  // The intercepted exceptions we are currently decoding.
  std::map<uint64_t, std::unique_ptr<ExceptionDecoder>> exception_decoders_;

  // All the handles for which we have some information.
  Inference inference_;
};

class SyscallDisplayDispatcher : public SyscallDecoderDispatcher {
 public:
  SyscallDisplayDispatcher(fidl_codec::LibraryLoader* loader, const DecodeOptions& decode_options,
                           const DisplayOptions& display_options, std::ostream& os)
      : SyscallDecoderDispatcher(decode_options),
        message_decoder_dispatcher_(loader, display_options),
        os_(os) {}

  fidl_codec::MessageDecoderDispatcher& message_decoder_dispatcher() {
    return message_decoder_dispatcher_;
  }

  const fidl_codec::Colors& colors() const { return message_decoder_dispatcher_.colors(); }

  bool with_process_info() const { return message_decoder_dispatcher_.with_process_info(); }

  const SyscallDisplay* last_displayed_syscall() const { return last_displayed_syscall_; }
  void set_last_displayed_syscall(const SyscallDisplay* last_displayed_syscall) {
    last_displayed_syscall_ = last_displayed_syscall;
  }

  void AddLaunchedProcess(uint64_t process_koid) override {
    message_decoder_dispatcher_.AddLaunchedProcess(process_koid);
  }

  std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                zxdb::Thread* thread, uint64_t thread_id,
                                                const Syscall* syscall) override;

  std::unique_ptr<ExceptionDecoder> CreateDecoder(InterceptionWorkflow* workflow,
                                                  zxdb::Thread* thread,
                                                  uint64_t thread_id) override;

  void ProcessLaunched(const std::string& command, std::string_view error_message) override;

  void ProcessMonitored(std::string_view name, zx_koid_t koid,
                        std::string_view error_message) override;

  void StopMonitoring(zx_koid_t koid) override;

 private:
  // Class which can decode a FIDL message.
  fidl_codec::MessageDecoderDispatcher message_decoder_dispatcher_;
  // The last syscall we displayed the inputs on the stream.
  const SyscallDisplay* last_displayed_syscall_ = nullptr;
  // The stream which will receive the syscall decodings.
  std::ostream& os_;
};

// Display a value on a stream.
template <typename ValueType>
void DisplayValue(SyscallDecoderDispatcher* /*dispatcher*/, const fidl_codec::Colors& /*colors*/,
                  SyscallType type, ValueType /*value*/, std::ostream& os) {
  os << "unimplemented generic value " << static_cast<uint32_t>(type);
}

template <>
inline void DisplayValue<bool>(SyscallDecoderDispatcher* /*dispatcher*/,
                               const fidl_codec::Colors& colors, SyscallType type, bool value,
                               std::ostream& os) {
  switch (type) {
    case SyscallType::kBool:
      os << colors.blue << (value ? "true" : "false") << colors.reset;
      break;
    default:
      os << "unimplemented bool value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const char*, size_t>>(SyscallDecoderDispatcher* /*dispatcher*/,
                                                         const fidl_codec::Colors& colors,
                                                         SyscallType type,
                                                         std::pair<const char*, size_t> value,
                                                         std::ostream& os) {
  switch (type) {
    case SyscallType::kCharArray:
      os << colors.red << '"';
      for (size_t i = 0; i < value.second; ++i) {
        if (value.first[i] == 0) {
          break;
        }
        os << value.first[i];
      }
      os << '"' << colors.reset;
      break;
    default:
      os << "unimplemented char array value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<int32_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                  const fidl_codec::Colors& colors, SyscallType type, int32_t value,
                                  std::ostream& os) {
  switch (type) {
    case SyscallType::kInt32:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kFutex:
      os << colors.red << value << colors.reset;
      break;
    case SyscallType::kStatus:
      StatusName(colors, value, os);
      break;
    default:
      os << "unimplemented int32_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<int64_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                  const fidl_codec::Colors& colors, SyscallType type, int64_t value,
                                  std::ostream& os) {
  switch (type) {
    case SyscallType::kInt64:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kDuration:
      os << DisplayDuration(colors, value);
      break;
    case SyscallType::kFutex:
      os << colors.red << value << colors.reset;
      break;
    case SyscallType::kMonotonicTime:
      os << DisplayDuration(colors, value);
      break;
    case SyscallType::kTime:
      os << DisplayTime(colors, value);
      break;
    default:
      os << "unimplemented int64_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint8_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                  const fidl_codec::Colors& colors, SyscallType type, uint8_t value,
                                  std::ostream& os) {
  switch (type) {
    case SyscallType::kUint8:
      os << colors.blue << static_cast<uint32_t>(value) << colors.reset;
      break;
    case SyscallType::kUint8Hexa: {
      std::vector<char> buffer(sizeof(uint8_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%02x", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kPacketGuestVcpuType:
      os << colors.blue;
      PacketGuestVcpuTypeName(value, os);
      os << colors.reset;
      break;
    default:
      os << "unimplemented uint8_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const uint8_t*, int>>(SyscallDecoderDispatcher* dispatcher,
                                                         const fidl_codec::Colors& colors,
                                                         SyscallType type,
                                                         std::pair<const uint8_t*, int> value,
                                                         std::ostream& os) {
  switch (type) {
    case SyscallType::kUint8ArrayDecimal:
    case SyscallType::kUint8ArrayHexa: {
      const char* separator = "";
      for (int i = 0; i < value.second; ++i) {
        os << separator;
        DisplayValue(
            dispatcher, colors,
            (type == SyscallType::kUint8ArrayHexa) ? SyscallType::kUint8Hexa : SyscallType::kUint8,
            value.first[i], os);
        separator = ", ";
      }
      break;
    }
    default:
      os << "unimplemented uint8_t array value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint16_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                   const fidl_codec::Colors& colors, SyscallType type,
                                   uint16_t value, std::ostream& os) {
  switch (type) {
    case SyscallType::kUint16:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kUint16Hexa: {
      std::vector<char> buffer(sizeof(uint16_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%04x", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kPacketPageRequestCommand:
      os << colors.blue;
      PacketPageRequestCommandName(value, os);
      os << colors.reset;
      break;
    default:
      os << "unimplemented uint16_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const uint16_t*, int>>(SyscallDecoderDispatcher* dispatcher,
                                                          const fidl_codec::Colors& colors,
                                                          SyscallType type,
                                                          std::pair<const uint16_t*, int> value,
                                                          std::ostream& os) {
  switch (type) {
    case SyscallType::kUint16ArrayDecimal:
    case SyscallType::kUint16ArrayHexa: {
      const char* separator = "";
      for (int i = 0; i < value.second; ++i) {
        os << separator;
        DisplayValue(dispatcher, colors,
                     (type == SyscallType::kUint16ArrayHexa) ? SyscallType::kUint16Hexa
                                                             : SyscallType::kUint16,
                     value.first[i], os);
        separator = ", ";
      }
      break;
    }
    default:
      os << "unimplemented uint16_t array value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint32_t>(SyscallDecoderDispatcher* dispatcher,
                                   const fidl_codec::Colors& colors, SyscallType type,
                                   uint32_t value, std::ostream& os) {
  switch (type) {
    case SyscallType::kUint32:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kUint32Hexa: {
      std::vector<char> buffer(sizeof(uint32_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%08x", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kBtiPerm:
      os << colors.blue;
      BtiPermName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kCachePolicy:
      os << colors.red;
      CachePolicyName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kClock:
      os << colors.red;
      ClockName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kExceptionChannelType:
      os << colors.blue;
      ExceptionChannelTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kExceptionState:
      os << colors.blue;
      ExceptionStateName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kFeatureKind:
      os << colors.red;
      FeatureKindName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kGuestTrap:
      os << colors.red;
      GuestTrapName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kHandle:
      dispatcher->DisplayHandle(value, colors, os);
      break;
    case SyscallType::kInfoMapsType:
      os << colors.red;
      InfoMapsTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kInterruptFlags:
      os << colors.red;
      InterruptFlagsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kIommuType:
      os << colors.red;
      IommuTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kKtraceControlAction:
      os << colors.blue;
      KtraceControlActionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kObjectInfoTopic:
      os << colors.blue;
      TopicName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kObjProps:
      os << colors.blue;
      ObjPropsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kObjType:
      os << colors.blue;
      fidl_codec::ObjTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPciBarType:
      os << colors.blue;
      PciBarTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPolicyAction:
      os << colors.blue;
      PolicyActionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPolicyCondition:
      os << colors.blue;
      PolicyConditionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPolicyTopic:
      os << colors.blue;
      PolicyTopicName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPortPacketType:
      os << colors.blue;
      PortPacketTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kProfileInfoFlags:
      os << colors.blue;
      ProfileInfoFlagsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kPropType:
      os << colors.blue;
      PropTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kRights:
      os << colors.blue;
      fidl_codec::RightsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kRsrcKind:
      os << colors.blue;
      RsrcKindName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSignals:
      os << colors.blue;
      SignalName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSocketCreateOptions:
      os << colors.blue;
      SocketCreateOptionsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSocketReadOptions:
      os << colors.blue;
      SocketReadOptionsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSocketShutdownOptions:
      os << colors.blue;
      SocketShutdownOptionsName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSystemEventType:
      os << colors.blue;
      SystemEventTypeName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kSystemPowerctl:
      os << colors.blue;
      SystemPowerctlName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kThreadState:
      os << colors.blue;
      ThreadStateName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kThreadStateTopic:
      os << colors.blue;
      ThreadStateTopicName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kTimerOption:
      os << colors.blue;
      TimerOptionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVcpu:
      os << colors.red;
      VcpuName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVmOption:
      os << colors.red;
      VmOptionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVmoCreationOption:
      os << colors.blue;
      VmoCreationOptionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVmoOp:
      os << colors.blue;
      VmoOpName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVmoOption:
      os << colors.blue;
      VmoOptionName(value, os);
      os << colors.reset;
      break;
    case SyscallType::kVmoType:
      os << colors.blue;
      VmoTypeName(value, os);
      os << colors.reset;
      break;
    default:
      os << "unimplemented uint32_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const uint32_t*, int>>(SyscallDecoderDispatcher* dispatcher,
                                                          const fidl_codec::Colors& colors,
                                                          SyscallType type,
                                                          std::pair<const uint32_t*, int> value,
                                                          std::ostream& os) {
  switch (type) {
    case SyscallType::kUint32ArrayDecimal:
    case SyscallType::kUint32ArrayHexa: {
      const char* separator = "";
      for (int i = 0; i < value.second; ++i) {
        os << separator;
        DisplayValue(dispatcher, colors,
                     (type == SyscallType::kUint32ArrayHexa) ? SyscallType::kUint32Hexa
                                                             : SyscallType::kUint32,
                     value.first[i], os);
        separator = ", ";
      }
      break;
    }
    default:
      os << "unimplemented uint32_t array value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<uint64_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                   const fidl_codec::Colors& colors, SyscallType type,
                                   uint64_t value, std::ostream& os) {
  switch (type) {
    case SyscallType::kUint64:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kUint64Hexa: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
#ifndef __MACH__
    case SyscallType::kGpAddr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
#endif
    case SyscallType::kKoid:
      os << colors.red << value << colors.reset;
      break;
#ifndef __MACH__
    case SyscallType::kSize:
      os << colors.blue << value << colors.reset;
      break;
#endif
    case SyscallType::kTime:
      os << DisplayTime(colors, value);
      break;
    case SyscallType::kPaddr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
#ifndef __MACH__
    case SyscallType::kUintptr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kVaddr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
#endif
    default:
      os << "unimplemented uint64_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const uint64_t*, int>>(SyscallDecoderDispatcher* dispatcher,
                                                          const fidl_codec::Colors& colors,
                                                          SyscallType type,
                                                          std::pair<const uint64_t*, int> value,
                                                          std::ostream& os) {
  switch (type) {
    case SyscallType::kUint64ArrayDecimal:
    case SyscallType::kUint64ArrayHexa: {
      const char* separator = "";
      for (int i = 0; i < value.second; ++i) {
        os << separator;
        DisplayValue(dispatcher, colors,
                     (type == SyscallType::kUint64ArrayHexa) ? SyscallType::kUint64Hexa
                                                             : SyscallType::kUint64,
                     value.first[i], os);
        separator = ", ";
      }
      break;
    }
    default:
      os << "unimplemented uint64_t array value " << static_cast<uint32_t>(type);
      break;
  }
}

#ifdef __MACH__
template <>
inline void DisplayValue<uintptr_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                    const fidl_codec::Colors& colors, SyscallType type,
                                    uintptr_t value, std::ostream& os) {
  switch (type) {
    case SyscallType::kGpAddr: {
      std::vector<char> buffer(sizeof(uintptr_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kSize:
      os << colors.blue << value << colors.reset;
      break;
    case SyscallType::kPaddr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kUintptr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    case SyscallType::kVaddr: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value);
      os << colors.blue << buffer.data() << colors.reset;
      break;
    }
    default:
      os << "unimplemented uintptr_t value " << static_cast<uint32_t>(type);
      break;
  }
}
#endif

template <>
inline void DisplayValue<zx_uint128_t>(SyscallDecoderDispatcher* /*dispatcher*/,
                                       const fidl_codec::Colors& colors, SyscallType type,
                                       zx_uint128_t value, std::ostream& os) {
  switch (type) {
    case SyscallType::kUint128Hexa: {
      std::vector<char> buffer(sizeof(uint64_t) * kCharactersPerByte + 1);
      snprintf(buffer.data(), buffer.size(), "%016lx", value.low);
      os << colors.blue << "{ low = " << buffer.data();
      snprintf(buffer.data(), buffer.size(), "%016lx", value.high);
      os << ", high = " << buffer.data() << " }" << colors.reset;
      break;
    }
    default:
      os << "unimplemented zx_uint128_t value " << static_cast<uint32_t>(type);
      break;
  }
}

template <>
inline void DisplayValue<std::pair<const zx_uint128_t*, int>>(
    SyscallDecoderDispatcher* dispatcher, const fidl_codec::Colors& colors, SyscallType type,
    std::pair<const zx_uint128_t*, int> value, std::ostream& os) {
  switch (type) {
    case SyscallType::kUint128ArrayHexa: {
      const char* separator = "";
      for (int i = 0; i < value.second; ++i) {
        os << separator;
        DisplayValue(dispatcher, colors, SyscallType::kUint128Hexa, value.first[i], os);
        separator = ", ";
      }
      break;
    }
    default:
      os << "unimplemented zx_uint128_t array value " << static_cast<uint32_t>(type);
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
                                          SyscallDecoderDispatcher* dispatcher,
                                          const fidl_codec::Colors& colors,
                                          std::string_view line_header, int tabs,
                                          std::ostream& os) const {
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ')
     << ClassFieldBase<ClassType>::name();
  DisplayType(colors, ClassFieldBase<ClassType>::syscall_type(), os);
  DisplayValue<Type>(dispatcher, colors, ClassFieldBase<ClassType>::syscall_type(), get_(object),
                     os);
  os << '\n';
}

template <typename ClassType, typename Type>
void ArrayField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch /*arch*/,
                                          SyscallDecoderDispatcher* dispatcher,
                                          const fidl_codec::Colors& colors,
                                          std::string_view line_header, int tabs,
                                          std::ostream& os) const {
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ')
     << ClassFieldBase<ClassType>::name();
  DisplayType(colors, ClassFieldBase<ClassType>::syscall_type(), os);
  os << "[]: {";
  const char* separator = " ";
  std::pair<const Type*, int> array = get_(object);
  for (int i = 0; i < array.second; ++i) {
    os << separator;
    DisplayValue<Type>(dispatcher, colors, ClassFieldBase<ClassType>::syscall_type(),
                       array.first[i], os);
    separator = ", ";
  }
  os << " }\n";
}

template <typename ClassType, typename Type>
void ClassClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                               SyscallDecoderDispatcher* dispatcher,
                                               const fidl_codec::Colors& colors,
                                               std::string_view line_header, int tabs,
                                               std::ostream& os) const {
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ')
     << ClassFieldBase<ClassType>::name() << ':' << colors.green << field_class_->name()
     << colors.reset << ": ";
  const Type* sub_object = get_(object);
  field_class_->DisplayObject(sub_object, arch, dispatcher, colors, line_header, tabs, os);
  os << '\n';
}

template <typename ClassType, typename Type>
void ArrayClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                               SyscallDecoderDispatcher* dispatcher,
                                               const fidl_codec::Colors& colors,
                                               std::string_view line_header, int tabs,
                                               std::ostream& os) const {
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ')
     << ClassFieldBase<ClassType>::name() << ':' << colors.green << sub_class_->name()
     << colors.reset << "[]: {\n";
  std::pair<const Type*, int> array = get_(object);
  for (int i = 0; i < array.second; ++i) {
    os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ');
    sub_class_->DisplayObject(array.first + i, arch, dispatcher, colors, line_header, tabs + 1, os);
    os << '\n';
  }
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ') << "}\n";
}

template <typename ClassType, typename Type>
void DynamicArrayClassField<ClassType, Type>::Display(const ClassType* object, debug_ipc::Arch arch,
                                                      SyscallDecoderDispatcher* dispatcher,
                                                      const fidl_codec::Colors& colors,
                                                      std::string_view line_header, int tabs,
                                                      std::ostream& os) const {
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ')
     << ClassFieldBase<ClassType>::name() << ':' << colors.green << sub_class_->name()
     << colors.reset << "[]: {\n";
  const Type* array = get_(object);
  uint32_t size = get_size_(object);
  for (uint32_t i = 0; i < size; ++i) {
    os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ');
    sub_class_->DisplayObject(array + i, arch, dispatcher, colors, line_header, tabs + 1, os);
    os << '\n';
  }
  os << line_header << std::string(tabs * fidl_codec::kTabSize, ' ') << "}\n";
}

template <typename Type>
void Access<Type>::Display(SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder,
                           Stage stage, std::string_view name, std::ostream& os) const {
  const fidl_codec::Colors& colors = dispatcher->colors();
  os << name;
  DisplayType(colors, GetSyscallType(), os);
  if (ValueValid(decoder, stage)) {
    DisplayValue<Type>(dispatcher, colors, GetSyscallType(), Value(decoder, stage), os);
  } else {
    os << colors.red << "(nullptr)" << colors.reset;
  }
}

template <typename Type>
const char* SyscallInputOutputActualAndRequested<Type>::DisplayInline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    const char* separator, std::ostream& os) const {
  os << separator;
  actual_->Display(dispatcher, decoder, stage, name(), os);
  os << "/";
  const fidl_codec::Colors& colors = dispatcher->colors();
  if (asked_->ValueValid(decoder, stage)) {
    DisplayValue<Type>(dispatcher, colors, asked_->GetSyscallType(), asked_->Value(decoder, stage),
                       os);
  } else {
    os << colors.red << "(nullptr)" << colors.reset;
  }
  return ", ";
}

template <typename Type, typename FromType>
const char* SyscallInputOutputIndirect<Type, FromType>::DisplayInline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    const char* separator, std::ostream& os) const {
  os << separator << name();
  const fidl_codec::Colors& colors = dispatcher->colors();
  DisplayType(colors, syscall_type_, os);
  const FromType* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    os << colors.red << "nullptr" << colors.reset;
  } else {
    DisplayValue<Type>(dispatcher, colors, syscall_type_, *reinterpret_cast<const Type*>(buffer),
                       os);
  }
  return ", ";
}

template <typename Type, typename FromType, typename SizeType>
void SyscallInputOutputBuffer<Type, FromType, SizeType>::DisplayOutline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    std::string_view line_header, int tabs, std::ostream& os) const {
  os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ') << name();
  const fidl_codec::Colors& colors = dispatcher->colors();
  DisplayType(colors, syscall_type_, os);
  const FromType* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    os << colors.red << "nullptr" << colors.reset;
  } else {
    size_t buffer_size = elem_size_->Value(decoder, stage);
    if (elem_count_ != nullptr) {
      buffer_size *= elem_count_->Value(decoder, stage);
    }
    if (buffer_size == 0) {
      os << "empty\n";
      return;
    }
    const char* separator = "";
    for (size_t i = 0; i < buffer_size; ++i) {
      os << separator;
      DisplayValue<Type>(dispatcher, colors, syscall_type_,
                         reinterpret_cast<const Type*>(buffer)[i], os);
      separator = ", ";
    }
  }
  os << '\n';
}

template <>
inline void SyscallInputOutputBuffer<uint8_t, uint8_t, size_t>::DisplayOutline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    std::string_view line_header, int tabs, std::ostream& os) const {
  os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ') << name();
  const fidl_codec::Colors& colors = dispatcher->colors();
  DisplayType(colors, syscall_type_, os);
  const uint8_t* buffer = buffer_->Content(decoder, stage);
  if (buffer == nullptr) {
    os << colors.red << "nullptr" << colors.reset;
  } else {
    size_t buffer_size = elem_size_->Value(decoder, stage);
    if (elem_count_ != nullptr) {
      buffer_size *= elem_count_->Value(decoder, stage);
    }
    if (buffer_size == 0) {
      os << "empty\n";
      return;
    }
    for (size_t i = 0;; ++i) {
      if (i == buffer_size) {
        os << colors.red << '"';
        for (size_t i = 0; i < buffer_size; ++i) {
          char value = reinterpret_cast<const char*>(buffer)[i];
          switch (value) {
            case 0:
              break;
            case '\\':
              os << "\\\\";
              break;
            case '\n':
              os << "\\n";
              break;
            default:
              os << value;
              break;
          }
        }
        os << '"' << colors.reset << '\n';
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
      os << separator;
      DisplayValue<uint8_t>(dispatcher, colors, buffer_->GetSyscallType(), buffer[i], os);
      separator = ", ";
    }
  }
  os << '\n';
}

template <typename FromType>
const char* SyscallInputOutputString<FromType>::DisplayInline(SyscallDisplayDispatcher* dispatcher,
                                                              SyscallDecoder* decoder, Stage stage,
                                                              const char* separator,
                                                              std::ostream& os) const {
  const fidl_codec::Colors& colors = dispatcher->colors();
  os << separator;
  os << name() << ':' << colors.green << "string" << colors.reset << ": ";
  const char* string = reinterpret_cast<const char*>(string_->Content(decoder, stage));
  size_t string_size = string_size_->Value(decoder, stage);
  DisplayString(colors, string, string_size, os);
  return ", ";
}

template <typename ClassType, typename SizeType>
void SyscallInputOutputObject<ClassType, SizeType>::DisplayOutline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    std::string_view line_header, int tabs, std::ostream& os) const {
  const fidl_codec::Colors& colors = dispatcher->colors();
  os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ') << name() << ":"
     << colors.green << class_definition_->name() << colors.reset << ": ";
  auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
  if (object == nullptr) {
    os << colors.red << "nullptr" << colors.reset;
  } else {
    class_definition_->DisplayObject(object, decoder->arch(), dispatcher, colors, line_header,
                                     tabs + 1, os);
  }
  os << '\n';
}

template <typename ClassType, typename SizeType>
void SyscallInputOutputObjectArray<ClassType, SizeType>::DisplayOutline(
    SyscallDisplayDispatcher* dispatcher, SyscallDecoder* decoder, Stage stage,
    std::string_view line_header, int tabs, std::ostream& os) const {
  const fidl_codec::Colors& colors = dispatcher->colors();
  os << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ') << name() << ":"
     << colors.green << class_definition_->name() << colors.reset << "[]: ";
  auto object = reinterpret_cast<const ClassType*>(buffer_->Uint8Content(decoder, stage));
  if (object == nullptr) {
    os << colors.red << "nullptr" << colors.reset;
  } else {
    os << " {";
    SizeType count = buffer_size_->Value(decoder, stage);
    const char* separator = "\n";
    for (SizeType i = 0; i < count; ++i) {
      os << separator << line_header << std::string((tabs + 2) * fidl_codec::kTabSize, ' ');
      class_definition_->DisplayObject(object + i, decoder->arch(), dispatcher, colors, line_header,
                                       tabs + 2, os);
      separator = ",\n";
    }
    os << '\n' << line_header << std::string((tabs + 1) * fidl_codec::kTabSize, ' ') << '}';
  }
  os << '\n';
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_DISPATCHER_H_
