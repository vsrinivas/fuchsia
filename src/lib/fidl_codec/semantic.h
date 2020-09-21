// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_SEMANTIC_H_
#define SRC_LIB_FIDL_CODEC_SEMANTIC_H_

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "src/lib/fidl_codec/printer.h"

namespace fidl_codec {

class Value;
class StructValue;
class Type;

namespace semantic {

class ExpressionValue;
class HandleSemantic;

// The context type (the kind of syscall).
enum class ContextType { kRead, kWrite, kCall };

// Context used during the execution of semantic rules.
class SemanticContext {
 public:
  SemanticContext(const HandleSemantic* handle_semantic, zx_koid_t pid, zx_handle_t handle,
                  const StructValue* request, const StructValue* response)
      : handle_semantic_(handle_semantic),
        pid_(pid),
        handle_(handle),
        request_(request),
        response_(response) {}

  const HandleSemantic* handle_semantic() const { return handle_semantic_; }
  zx_koid_t pid() const { return pid_; }
  zx_handle_t handle() const { return handle_; }
  const StructValue* request() const { return request_; }
  const StructValue* response() const { return response_; }

 private:
  // The semantic rules for the FIDL method.
  const HandleSemantic* const handle_semantic_;
  // The process id.
  const zx_koid_t pid_;
  // The handle we are reading/writing on.
  const zx_handle_t handle_;
  // The request (can be null).
  const StructValue* const request_;
  // The response (can be null).
  const StructValue* const response_;
};

// Context used during the execution of semantic rules.
class AssignmentSemanticContext : public SemanticContext {
 public:
  AssignmentSemanticContext(HandleSemantic* handle_semantic, zx_koid_t pid, zx_koid_t tid,
                            zx_handle_t handle, ContextType type, const StructValue* request,
                            const StructValue* response)
      : SemanticContext(handle_semantic, pid, handle, request, response), tid_(tid), type_(type) {}

  zx_koid_t tid() const { return tid_; }
  ContextType type() const { return type_; }
  HandleSemantic* handle_semantic() const {
    return const_cast<HandleSemantic*>(SemanticContext::handle_semantic());
  }

 private:
  // The thread id.
  const zx_koid_t tid_;
  // The context type.
  const ContextType type_;
};

// Base class for all expressions (for semantic).
class Expression {
 public:
  Expression() = default;
  virtual ~Expression() = default;

  // Dump the expression.
  virtual void Dump(std::ostream& os) const = 0;

  // Execute the expression for a gien context.
  virtual bool Execute(SemanticContext* context, ExpressionValue* result) const = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Expression& expression) {
  expression.Dump(os);
  return os;
}

// Defines a string literal.
class ExpressionStringLiteral : public Expression {
 public:
  ExpressionStringLiteral(std::string value) : value_(std::move(value)) {}

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;

 private:
  const std::string value_;
};

// Defines an expression which accesses the request object.
class ExpressionRequest : public Expression {
 public:
  ExpressionRequest() = default;

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;
};

// Defines an expression which accesses the handle used to read/write the request.
class ExpressionHandle : public Expression {
 public:
  ExpressionHandle() = default;

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;
};

// Defines a handle description definition.
class ExpressionHandleDescription : public Expression {
 public:
  ExpressionHandleDescription(std::unique_ptr<Expression> type, std::unique_ptr<Expression> path)
      : type_(std::move(type)), path_(std::move(path)) {}

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;

 private:
  std::unique_ptr<Expression> type_;
  std::unique_ptr<Expression> path_;
};

// Defines the access to an object field.
class ExpressionFieldAccess : public Expression {
 public:
  ExpressionFieldAccess(std::unique_ptr<Expression> expression, std::string_view field)
      : expression_(std::move(expression)), field_(field) {}

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;

 private:
  const std::unique_ptr<Expression> expression_;
  const std::string field_;
};

// Defines the slash operator (used to concatenate two paths).
class ExpressionSlash : public Expression {
 public:
  ExpressionSlash(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
      : left_(std::move(left)), right_(std::move(right)) {}

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;

 private:
  const std::unique_ptr<Expression> left_;
  const std::unique_ptr<Expression> right_;
};

// Defines the colon operator (used to add attributes to a handle).
class ExpressionColon : public Expression {
 public:
  ExpressionColon(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
      : left_(std::move(left)), right_(std::move(right)) {}

  void Dump(std::ostream& os) const override;
  bool Execute(SemanticContext* context, ExpressionValue* result) const override;

 private:
  const std::unique_ptr<Expression> left_;
  const std::unique_ptr<Expression> right_;
};

// Defines an assignment. An assignment is a rule which infers the semantic of one handle
// (destination) using the value of an expression (source).
class Assignment {
 public:
  Assignment(std::unique_ptr<Expression> destination, std::unique_ptr<Expression> source)
      : destination_(std::move(destination)), source_(std::move(source)) {}

  void Dump(std::ostream& os) const;
  void Execute(AssignmentSemanticContext* context) const;

 private:
  const std::unique_ptr<Expression> destination_;
  const std::unique_ptr<Expression> source_;
};

// Defines the semantic associated to a method. When a method is called, all the semantic rules
// (the assignments) are executed and add knowledge about the handles involved.
class MethodSemantic {
 public:
  MethodSemantic() = default;

  void AddAssignment(std::unique_ptr<Expression> destination, std::unique_ptr<Expression> source) {
    assignments_.emplace_back(
        std::make_unique<Assignment>(std::move(destination), std::move(source)));
  }

  void Dump(std::ostream& os) const;
  void ExecuteAssignments(AssignmentSemanticContext* context) const;

 private:
  std::vector<std::unique_ptr<Assignment>> assignments_;
};

// Defines an expression to be displayed.
class DisplayExpression {
 public:
  DisplayExpression() = default;

  void set_header(std::string&& header) { header_ = std::move(header); }

  void set_expression(std::unique_ptr<Expression> expression) {
    expression_ = std::move(expression);
  }

  void set_footer(std::string&& footer) { footer_ = std::move(footer); }

  void Dump(std::ostream& os) const;
  void PrettyPrint(PrettyPrinter& printer, SemanticContext* context);

 private:
  std::string header_;
  std::unique_ptr<Expression> expression_;
  std::string footer_;
};

// Defines what needs to be display for a method. This is used to display short views of methods.
class MethodDisplay {
 public:
  MethodDisplay() = default;

  const std::vector<std::unique_ptr<DisplayExpression>>& inputs() const { return inputs_; }
  const std::vector<std::unique_ptr<DisplayExpression>>& results() const { return results_; }

  void AddInput(std::unique_ptr<DisplayExpression> input) {
    inputs_.emplace_back(std::move(input));
  }

  void AddResult(std::unique_ptr<DisplayExpression> result) {
    results_.emplace_back(std::move(result));
  }

  void Dump(std::ostream& os) const;

 private:
  std::vector<std::unique_ptr<DisplayExpression>> inputs_;
  std::vector<std::unique_ptr<DisplayExpression>> results_;
};

// Holds the information we have inferred for a handle.
// Usually we can associate a type to a handle.
// Depending on the type, we can also associate:
// - a path (for example for directories and files).
// - a file descriptor (for example for sockets).
class InferredHandleInfo {
 public:
  InferredHandleInfo() = default;

  explicit InferredHandleInfo(std::string_view type) : type_(type) {}

  InferredHandleInfo(std::string_view type, int64_t fd, std::string_view attributes)
      : type_(type), fd_(fd), attributes_(attributes) {}

  InferredHandleInfo(std::string_view type, std::string_view path, std::string_view attributes)
      : type_(type), path_(path), attributes_(attributes) {}

  InferredHandleInfo(std::string_view type, int64_t fd, std::string_view path,
                     std::string_view attributes)
      : type_(type), fd_(fd), path_(path), attributes_(attributes) {}

  const std::string& type() const { return type_; }
  int64_t fd() const { return fd_; }
  const std::string& path() const { return path_; }
  const std::string& attributes() const { return attributes_; }

  // Convert a handle type (found in zircon/system/public/zircon/processargs.h) into a string.
  static std::string_view Convert(uint32_t type);

  // Display the information we have about a handle.
  void Display(PrettyPrinter& printer) const;

 private:
  // Type of the handle. This can be a predefined type (when set by Convert) or
  // any string when it is an applicative type.
  const std::string type_;
  // Numerical value associated with the handle. Mostly used by file descriptors.
  const int64_t fd_ = -1;
  // Path associated with the handle. We can have both fd and path defined at the
  // same time.
  const std::string path_;
  // Applicative attributes associated with the handle.
  const std::string attributes_;
};

// Holds the handle semantic for one process. That is all the meaningful information we have been
// able to infer for the handles owned by one process.
struct ProcessSemantic {
  // All the handles for which we have some information.
  std::map<zx_handle_t, std::unique_ptr<InferredHandleInfo>> handles;
  // All the links between handle pairs.
  std::map<zx_handle_t, zx_handle_t> linked_handles;
};

// Object which hold the information we have about handles for all the processes.
class HandleSemantic {
 public:
  HandleSemantic() = default;
  virtual ~HandleSemantic() = default;

  const std::map<zx_koid_t, ProcessSemantic>& process_handles() const { return process_handles_; }
  const std::map<zx_koid_t, zx_koid_t>& linked_koids() const { return linked_koids_; }

  size_t handle_size(zx_koid_t pid) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return 0;
    }
    return process_semantic->second.handles.size();
  }

  const ProcessSemantic* GetProcessSemantic(zx_koid_t pid) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return nullptr;
    }
    return &process_semantic->second;
  }

  InferredHandleInfo* GetInferredHandleInfo(zx_koid_t pid, zx_handle_t handle) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return nullptr;
    }
    const auto& result = process_semantic->second.handles.find(handle);
    if (result == process_semantic->second.handles.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  virtual void CreateHandleInfo(zx_koid_t thread_koid, zx_handle_t handle) {}

  virtual bool NeedsToLoadHandleInfo(zx_koid_t tid, zx_handle_t handle) const { return false; }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle,
                             const InferredHandleInfo* inferred_handle_info) {
    if (inferred_handle_info != nullptr) {
      process_handles_[pid].handles[handle] =
          std::make_unique<InferredHandleInfo>(*inferred_handle_info);
    }
  }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle,
                             std::unique_ptr<InferredHandleInfo> inferred_handle_info) {
    process_handles_[pid].handles[handle] = std::move(inferred_handle_info);
  }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle, std::string_view type) {
    process_handles_[pid].handles[handle] = std::make_unique<InferredHandleInfo>(type);
  }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle, std::string_view type, int64_t fd,
                             std::string_view attributes) {
    process_handles_[pid].handles[handle] =
        std::make_unique<InferredHandleInfo>(type, fd, attributes);
  }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle, std::string_view type,
                             std::string_view path, std::string_view attributes) {
    process_handles_[pid].handles[handle] =
        std::make_unique<InferredHandleInfo>(type, path, attributes);
  }

  void AddInferredHandleInfo(zx_koid_t pid, zx_handle_t handle, uint32_t type) {
    process_handles_[pid].handles[handle] =
        std::make_unique<InferredHandleInfo>(InferredHandleInfo::Convert(type));
  }

  // Returns the handle peer for a channel.
  zx_handle_t GetLinkedHandle(zx_koid_t pid, zx_handle_t handle) const {
    const auto& process_semantic = process_handles_.find(pid);
    if (process_semantic == process_handles_.end()) {
      return ZX_HANDLE_INVALID;
    }
    auto linked = process_semantic->second.linked_handles.find(handle);
    if (linked == process_semantic->second.linked_handles.end()) {
      return ZX_HANDLE_INVALID;
    }
    return linked->second;
  }

  // Associates two channels which have been created by the same zx_channel_create.
  void AddLinkedHandles(zx_koid_t pid, zx_handle_t handle0, zx_handle_t handle1) {
    ProcessSemantic& process_semantic = process_handles_[pid];
    process_semantic.linked_handles.insert(std::make_pair(handle0, handle1));
    process_semantic.linked_handles.insert(std::make_pair(handle1, handle0));
  }

  // Returns the koid of a channel peer given the channel koid).
  zx_koid_t GetLinkedKoid(zx_koid_t koid) const {
    auto linked = linked_koids_.find(koid);
    if (linked == linked_koids_.end()) {
      return ZX_KOID_INVALID;
    }
    return linked->second;
  }

  // Associates two channel koids.
  void AddLinkedKoids(zx_koid_t koid0, zx_koid_t koid1) {
    linked_koids_.insert(std::make_pair(koid0, koid1));
    linked_koids_.insert(std::make_pair(koid1, koid0));
  }

 private:
  std::map<zx_koid_t, ProcessSemantic> process_handles_;
  std::map<zx_koid_t, zx_koid_t> linked_koids_;
};

// Holds the evaluation of an expression. The value depends on kind_.
class ExpressionValue {
 public:
  ExpressionValue() = default;

  enum Kind { kUndefined, kValue, kHandle, kInferredHandleInfo, kString, kInteger };
  void set_value(const Type* value_type, const Value* value) {
    FX_DCHECK(value != nullptr);
    kind_ = kValue;
    value_type_ = value_type;
    value_ = value;
  }
  void set_handle(zx_handle_t handle) {
    kind_ = kHandle;
    handle_ = handle;
  }
  void set_inferred_handle_info(std::string_view type, int64_t fd, std::string_view path,
                                std::string_view attributes) {
    kind_ = kInferredHandleInfo;
    inferred_handle_info_ = std::make_unique<InferredHandleInfo>(type, fd, path, attributes);
  }
  void set_string(const std::string& string) {
    kind_ = kString;
    string_ = string;
  }
  void set_integer(int64_t integer) {
    kind_ = kInteger;
    integer_ = integer;
  }

  Kind kind() const { return kind_; }
  const Type* value_type() const {
    FX_DCHECK(kind_ == kValue);
    return value_type_;
  }
  const Value* value() const {
    FX_DCHECK(kind_ == kValue);
    return value_;
  }
  zx_handle_t handle() const {
    FX_DCHECK(kind_ == kHandle);
    return handle_;
  }
  const InferredHandleInfo* inferred_handle_info() const {
    FX_DCHECK(kind_ == kInferredHandleInfo);
    return inferred_handle_info_.get();
  }
  const std::string& string() const {
    FX_DCHECK(kind_ == kString);
    return string_;
  }
  int64_t integer() const {
    FX_DCHECK(kind_ == kInteger);
    return integer_;
  }

  // If the value is a handle and if the handle is linked then the value is assigned with the
  // linked handle.
  void UseLinkedHandle(const SemanticContext* context);

  void PrettyPrint(PrettyPrinter& printer);

 private:
  Kind kind_ = kUndefined;
  // If kind is kValue, value_ is a FIDL value with the type value_type_.
  const Type* value_type_ = nullptr;
  const Value* value_ = nullptr;
  // If kind is kHandle, handle_ is a handle.
  zx_handle_t handle_ = ZX_HANDLE_INVALID;
  // If kind is kInferredHandleInfo, inferred_handle_info_ is a inferred handle info.
  std::unique_ptr<InferredHandleInfo> inferred_handle_info_;
  // If kind is kString, string_ is a string.
  std::string string_;
  // If kind is kInteger, integer_ is an integer.
  int64_t integer_ = 0;
};

}  // namespace semantic
}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_SEMANTIC_H_
