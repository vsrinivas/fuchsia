// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <sstream>
#include <string>

#pragma once

//#define OVERNET_DEBUG_TRACE_SINK_REFS

namespace overnet {

enum class Severity {
  DEBUG,
  INFO,
  WARNING,
  ERROR,
};

struct TraceOutput {
  std::string message;
  const char* file;
  int line;
  Severity severity;
};

class TraceSinkInterface {
 public:
  void Ref() {
#ifdef OVERNET_DEBUG_TRACE_SINK_REFS
    std::ostringstream msg;
    msg << "TSI:" << this << ":REF:" << refs_ << "->" << (refs_ + 1);
    Trace(TraceOutput{msg.str(), __FILE__, __LINE__, Severity::DEBUG});
#endif

    ++refs_;
  }
  void Unref() {
#ifdef OVERNET_DEBUG_TRACE_SINK_REFS
    std::ostringstream msg;
    msg << "TSI:" << this << ":UNREF:" << refs_ << "->" << (refs_ - 1);
    Trace(TraceOutput{msg.str(), __FILE__, __LINE__, Severity::DEBUG});
#endif

    if (0 == --refs_)
      Done();
  }

  virtual void Done() = 0;
  virtual void Trace(TraceOutput output) = 0;

 private:
  int refs_ = 0;
};

class TraceSink {
 public:
  TraceSink() : severity_(Severity::DEBUG), impl_(nullptr) {}
  TraceSink(Severity severity, TraceSinkInterface* impl)
      : severity_(severity), impl_(impl) {
    impl_->Ref();
  }
  TraceSink(const TraceSink& other)
      : severity_(other.severity_), impl_(other.impl_) {
    if (impl_ != nullptr)
      impl_->Ref();
  }
  TraceSink(TraceSink&& other)
      : severity_(other.severity_), impl_(other.impl_) {
    other.impl_ = nullptr;
  }
  TraceSink& operator=(TraceSink other) {
    std::swap(impl_, other.impl_);
    std::swap(severity_, other.severity_);
    return *this;
  }
  TraceSink& operator=(TraceSink&& other) {
    std::swap(impl_, other.impl_);
    std::swap(severity_, other.severity_);
    return *this;
  }
  ~TraceSink() {
    if (impl_ != nullptr) {
      impl_->Unref();
    }
  }

  Severity severity() const { return severity_; }
  TraceSinkInterface* get() const { return impl_; }

  template <class F>
  TraceSink Decorate(F fn) const {
    if (impl_ == nullptr) {
      return TraceSink();
    }
    class Impl final : public TraceSinkInterface {
     public:
      Impl(TraceSink parent, F fn)
          : fn_(std::move(fn)), parent_(std::move(parent)) {}

      void Done() override { delete this; }
      void Trace(TraceOutput output) override {
        output.message = fn_(output.message);
        parent_.get()->Trace(std::move(output));
      }

     private:
      F fn_;
      TraceSink parent_;
    };
    return TraceSink(severity_, new Impl(*this, std::move(fn)));
  }

 private:
  Severity severity_;
  TraceSinkInterface* impl_;
};

class Trace : public std::ostringstream {
 public:
  Trace(TraceSinkInterface* output, const char* file, int line,
        Severity severity)
      : output_(output), file_(file), line_(line), severity_(severity) {}
  ~Trace() { output_->Trace(TraceOutput{str(), file_, line_, severity_}); }

 private:
  TraceSinkInterface* const output_;
  const char* const file_;
  const int line_;
  const Severity severity_;
};

}  // namespace overnet

#define OVERNET_TRACE(trace_severity, sink)                         \
  if ((sink).get() == nullptr)                                      \
    ;                                                               \
  else if ((sink).severity() > ::overnet::Severity::trace_severity) \
    ;                                                               \
  else                                                              \
    ::overnet::Trace((sink).get(), __FILE__, __LINE__,              \
                     ::overnet::Severity::trace_severity)
