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

namespace semantic {

class ExpressionValue;
class HandleSemantic;

// The context type (the kind of syscall).
enum class ContextType { kRead, kWrite, kCall };

// Context used during the execution of semantic rules.
class SemanticContext {
 public:
  SemanticContext(HandleSemantic* handle_semantic, zx_koid_t pid, zx_koid_t tid, zx_handle_t handle,
                  ContextType type, const StructValue* request, const StructValue* response)
      : handle_semantic_(handle_semantic),
        pid_(pid),
        tid_(tid),
        handle_(handle),
        type_(type),
        request_(request),
        response_(response) {}

  HandleSemantic* handle_semantic() const { return handle_semantic_; }
  zx_koid_t pid() const { return pid_; }
  zx_koid_t tid() const { return tid_; }
  zx_handle_t handle() const { return handle_; }
  ContextType type() const { return type_; }
  const StructValue* request() const { return request_; }
  const StructValue* response() const { return response_; }

 private:
  // The semantic rules for the FIDL method.
  HandleSemantic* const handle_semantic_;
  // The process id.
  const zx_koid_t pid_;
  // The thread id.
  const zx_koid_t tid_;
  // The handle we are reading/writing on.
  const zx_handle_t handle_;
  // The context type.
  const ContextType type_;
  // The request (can be null).
  const StructValue* const request_;
  // The response (can be null).
  const StructValue* const response_;
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
  void Execute(SemanticContext* context) const;

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
  void ExecuteAssignments(SemanticContext* context) const;

 private:
  std::vector<std::unique_ptr<Assignment>> assignments_;
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

// Holds the evaluation of an expression. Only one of the three fields can be defined.
class ExpressionValue {
 public:
  ExpressionValue() : inferred_handle_info_() {}

  void set(const Value* value) { value_ = value; }
  void set(zx_handle_t handle) { handle_ = handle; }
  void set(std::string_view type, int64_t fd, std::string_view path, std::string_view attributes) {
    inferred_handle_info_ = std::make_unique<InferredHandleInfo>(type, fd, path, attributes);
  }
  void set(const std::string& string) { string_ = string; }

  const Value* value() const { return value_; }
  zx_handle_t handle() const { return handle_; }
  const InferredHandleInfo* inferred_handle_info() const { return inferred_handle_info_.get(); }
  const std::optional<std::string>& string() const { return string_; }

 private:
  // If not null, the value is a FIDL value.
  const Value* value_ = nullptr;
  // If not ZX_HANDLE_INVALID, the value is a handle.
  zx_handle_t handle_ = ZX_HANDLE_INVALID;
  // If not null, the value is a inferred handle info.
  std::unique_ptr<InferredHandleInfo> inferred_handle_info_;
  // A string.
  std::optional<std::string> string_;
};

}  // namespace semantic
}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_SEMANTIC_H_
