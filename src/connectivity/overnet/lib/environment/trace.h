// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <sstream>
#include <string>
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

#pragma once

//#define OVERNET_DEBUG_TRACE_SINK_REFS

namespace overnet {

enum class Module {
  NUB,
  ROUTER,
  PACKET_LINK,
  DATAGRAM_STREAM,
  DATAGRAM_STREAM_SEND_OP,
  DATAGRAM_STREAM_RECV_OP,
  DATAGRAM_STREAM_INCOMING_MESSAGE,
  PACKET_PROTOCOL,
  BBR,
  MODULE_COUNT  // Must be last.
};

std::ostream& operator<<(std::ostream& out, Module module);

class Scopes {
 public:
  void* SetScope(Module module, void* instance) {
    assert(module != Module::MODULE_COUNT);
    void** p = &current_module_[static_cast<size_t>(module)];
    void* out = *p;
    *p = instance;
    return out;
  }

  template <class F>
  void Visit(F f) {
    for (size_t i = 0; i < static_cast<size_t>(Module::MODULE_COUNT); i++) {
      if (current_module_[i] != nullptr) {
        f(static_cast<Module>(i), current_module_[i]);
      }
    }
  }

 private:
  void* current_module_[static_cast<size_t>(Module::MODULE_COUNT)];
};

extern thread_local Scopes g_trace_scopes;

template <class T>
class ScopedModule {
 public:
  explicit ScopedModule(T* instance)
      : prev_(g_trace_scopes.SetScope(T::kModule, instance)) {}
  ~ScopedModule() { g_trace_scopes.SetScope(T::kModule, prev_); }
  ScopedModule(const ScopedModule&) = delete;
  ScopedModule& operator=(const ScopedModule&) = delete;

 private:
  void* const prev_;
};

enum class Severity : uint8_t {
  DEBUG,
  TRACE,
  INFO,
  WARNING,
  ERROR,
};

std::ostream& operator<<(std::ostream& out, Severity type);

Optional<Severity> SeverityFromString(const std::string& value);

enum class OpType : uint8_t {
  INVALID,
  OUTGOING_REQUEST,
  INCOMING_PACKET,
  LINEARIZER_INTEGRATION,
};

std::ostream& operator<<(std::ostream& out, OpType type);

class Op {
 public:
  Op() = delete;

  static Op Invalid() { return Op(OpType::INVALID, 0); }
  static Op New(OpType type);

  OpType type() const { return static_cast<OpType>(id_ >> 56); }
  uint64_t somewhat_unique_id() const { return id_ & 0x00ff'ffff'ffff'ffff; }

 private:
  Op(OpType type, uint64_t id)
      : id_((id & 0x00ff'ffff'ffff'ffff) |
            (static_cast<uint64_t>(type) << 56)) {}

  static uint64_t AllocateId() {
    static std::atomic<uint64_t> ctr;
    return ++ctr;
  }

  uint64_t id_;
};

std::ostream& operator<<(std::ostream& out, Op op);

class ScopedOp {
 public:
  explicit ScopedOp(Op op) : prev_(current_) { current_ = op; }
  ~ScopedOp() { current_ = prev_; }
  ScopedOp(const ScopedOp&) = delete;
  ScopedOp& operator=(const ScopedOp&) = delete;
  static Op current() { return current_; }

 private:
  const Op prev_;
  static thread_local Op current_;
};

struct TraceOutput {
  Op op;
  const char* message;
  Scopes scopes;
  const char* file;
  int line;
  Severity severity;
};

class TraceRenderer {
 public:
  virtual void Render(TraceOutput output) = 0;
  virtual void NoteParentChild(Op parent, Op child) = 0;
};

class ScopedRenderer {
 public:
  explicit ScopedRenderer(TraceRenderer* renderer) : prev_(current_) {
    current_ = renderer;
  }
  ~ScopedRenderer() { current_ = prev_; }
  ScopedRenderer(const ScopedRenderer&) = delete;
  ScopedRenderer& operator=(const ScopedRenderer&) = delete;

  static TraceRenderer* current() {
    assert(current_ != nullptr);
    return current_;
  }

 private:
  TraceRenderer* const prev_;
  static thread_local TraceRenderer* current_;
};

class ScopedSeverity {
 public:
  explicit ScopedSeverity(Severity severity) : prev_(current_) {
    current_ = std::max(current_, severity);
  }
  ~ScopedSeverity() { current_ = prev_; }
  ScopedSeverity(const ScopedSeverity&) = delete;
  ScopedSeverity& operator=(const ScopedSeverity&) = delete;
  static Severity current() { return current_; }

 private:
  const Severity prev_;
  static thread_local Severity current_;
};

class Trace : public std::ostringstream {
 public:
  Trace(const char* file, int line, Severity severity)
      : file_(file), line_(line), severity_(severity) {}
  ~Trace() {
    ScopedRenderer::current()->Render(TraceOutput{ScopedOp::current(),
                                                  str().c_str(), g_trace_scopes,
                                                  file_, line_, severity_});
  }

 private:
  const char* const file_;
  const int line_;
  const Severity severity_;
};

inline Op Op::New(OpType type) {
  assert(type != OpType::INVALID);
  auto out = Op(type, AllocateId());
  if (ScopedOp::current().type() != OpType::INVALID) {
    ScopedRenderer::current()->NoteParentChild(ScopedOp::current(), out);
  }
  return out;
}

}  // namespace overnet

#ifdef NDEBUG
// If we have NDEBUG set, we add an if constexpr to eliminate DEBUG level traces
// entirely from the build.
#define OVERNET_TRACE(trace_severity)                  \
  if constexpr (::overnet::Severity::trace_severity == \
                ::overnet::Severity::DEBUG)            \
    ;                                                  \
  else if (::overnet::ScopedSeverity::current() >      \
           ::overnet::Severity::trace_severity)        \
    ;                                                  \
  else                                                 \
    ::overnet::Trace(__FILE__, __LINE__, ::overnet::Severity::trace_severity)
#else
#define OVERNET_TRACE(trace_severity)        \
  if (::overnet::ScopedSeverity::current() > \
      ::overnet::Severity::trace_severity)   \
    ;                                        \
  else                                       \
    ::overnet::Trace(__FILE__, __LINE__, ::overnet::Severity::trace_severity)
#endif
