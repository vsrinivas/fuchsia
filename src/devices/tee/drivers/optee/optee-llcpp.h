// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_TEE_OPTEE_OPTEE_LLCPP_H_
#define ZIRCON_SYSTEM_DEV_TEE_OPTEE_OPTEE_LLCPP_H_

#include <fuchsia/tee/llcpp/fidl.h>

#include <cinttypes>
#include <optional>
#include <variant>
#include <vector>

// This file contains wrapper types for LLCPP to make extensible types more ergonomic to construct.

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;

class OsRevision {
 public:
  fuchsia_tee::OsRevision to_llcpp() {
    if (major_.has_value()) {
      llcpp_builder_.set_major(fidl::unowned(&major_.value()));
    }
    if (minor_.has_value()) {
      llcpp_builder_.set_minor(fidl::unowned(&minor_.value()));
    }

    return llcpp_builder_.build();
  }

  void set_major(uint32_t major) { major_ = major; }

  void set_minor(uint32_t minor) { minor_ = minor; }

 private:
  fuchsia_tee::OsRevision::UnownedBuilder llcpp_builder_;

  std::optional<uint32_t> major_{};
  std::optional<uint32_t> minor_{};
};

class OsInfo {
 public:
  fuchsia_tee::OsInfo to_llcpp() {
    if (uuid_.has_value()) {
      llcpp_builder_.set_uuid(fidl::unowned(&uuid_.value()));
    }
    if (revision_.has_value()) {
      llcpp_revision_ = revision_->to_llcpp();
      llcpp_builder_.set_revision(fidl::unowned(&llcpp_revision_));
    }
    if (is_global_platform_compliant_.has_value()) {
      llcpp_builder_.set_is_global_platform_compliant(
          fidl::unowned(&is_global_platform_compliant_.value()));
    }

    return llcpp_builder_.build();
  }

  void set_uuid(fuchsia_tee::Uuid uuid) { uuid_ = uuid; }

  void set_revision(OsRevision revision) { revision_ = std::move(revision); }

  void set_is_global_platform_compliant(bool is_global_platform_compliant) {
    is_global_platform_compliant_ = is_global_platform_compliant;
  }

 private:
  fuchsia_tee::OsInfo::UnownedBuilder llcpp_builder_;
  fuchsia_tee::OsRevision llcpp_revision_{};

  std::optional<fuchsia_tee::Uuid> uuid_{};
  std::optional<OsRevision> revision_{};
  std::optional<fidl::aligned<bool>> is_global_platform_compliant_{};
};

class Value {
 public:
  fuchsia_tee::Value to_llcpp() {
    if (direction_.has_value()) {
      llcpp_builder_.set_direction(fidl::unowned(&direction_.value()));
    }
    if (a_.has_value()) {
      llcpp_builder_.set_a(fidl::unowned(&a_.value()));
    }
    if (b_.has_value()) {
      llcpp_builder_.set_b(fidl::unowned(&b_.value()));
    }
    if (c_.has_value()) {
      llcpp_builder_.set_c(fidl::unowned(&c_.value()));
    }

    return llcpp_builder_.build();
  }

  void set_direction(fuchsia_tee::Direction direction) { direction_ = direction; }

  void set_a(uint64_t a) { a_ = a; }

  void set_b(uint64_t b) { b_ = b; }

  void set_c(uint64_t c) { c_ = c; }

 private:
  fuchsia_tee::Value::UnownedBuilder llcpp_builder_;

  std::optional<fuchsia_tee::Direction> direction_{};
  std::optional<uint64_t> a_{};
  std::optional<uint64_t> b_{};
  std::optional<uint64_t> c_{};
};

class Buffer {
 public:
  fuchsia_tee::Buffer to_llcpp() {
    if (direction_.has_value()) {
      llcpp_builder_.set_direction(fidl::unowned(&direction_.value()));
    }
    if (vmo_.has_value()) {
      llcpp_builder_.set_vmo(fidl::unowned(&vmo_.value()));
    }
    if (offset_.has_value()) {
      llcpp_builder_.set_offset(fidl::unowned(&offset_.value()));
    }
    if (size_.has_value()) {
      llcpp_builder_.set_size(fidl::unowned(&size_.value()));
    }

    return llcpp_builder_.build();
  }

  void set_direction(fuchsia_tee::Direction direction) { direction_ = direction; }

  void set_vmo(zx::vmo vmo) { vmo_ = std::move(vmo); }

  void set_offset(uint64_t offset) { offset_ = offset; }

  void set_size(uint64_t size) { size_ = size; }

 private:
  fuchsia_tee::Buffer::UnownedBuilder llcpp_builder_;

  std::optional<fuchsia_tee::Direction> direction_{};
  std::optional<zx::vmo> vmo_{};
  std::optional<uint64_t> offset_{};
  std::optional<uint64_t> size_{};
};

class Parameter {
 public:
  fuchsia_tee::Parameter to_llcpp() {
    if (std::holds_alternative<fuchsia_tee::None>(data_)) {
      llcpp_data_ = std::get<fuchsia_tee::None>(data_);
      return fuchsia_tee::Parameter::WithNone(&std::get<fuchsia_tee::None>(llcpp_data_));
    }
    if (std::holds_alternative<Value>(data_)) {
      llcpp_data_ = std::get<Value>(data_).to_llcpp();
      return fuchsia_tee::Parameter::WithValue(&std::get<fuchsia_tee::Value>(llcpp_data_));
    }
    if (std::holds_alternative<Buffer>(data_)) {
      llcpp_data_ = std::get<Buffer>(data_).to_llcpp();
      return fuchsia_tee::Parameter::WithBuffer(&std::get<fuchsia_tee::Buffer>(llcpp_data_));
    }

    return fuchsia_tee::Parameter();
  }

  void set_none() { data_ = fuchsia_tee::None{}; }

  void set_value(Value value) { data_ = std::move(value); }

  void set_buffer(Buffer buffer) { data_ = std::move(buffer); }

 private:
  std::variant<std::monostate, fuchsia_tee::None, fuchsia_tee::Value, fuchsia_tee::Buffer>
      llcpp_data_{};

  std::variant<std::monostate, fuchsia_tee::None, Value, Buffer> data_{};
};

class ParameterSet {
 public:
  fidl::VectorView<fuchsia_tee::Parameter> to_llcpp() {
    ZX_DEBUG_ASSERT(parameters_.has_value());

    llcpp_parameters_.clear();
    llcpp_parameters_.reserve(parameters_->size());
    for (auto& parameter : parameters_.value()) {
      llcpp_parameters_.push_back(parameter.to_llcpp());
    }

    return fidl::VectorView(llcpp_parameters_);
  }

  void set_parameters(std::vector<Parameter> parameters) { parameters_ = std::move(parameters); }

 private:
  std::vector<fuchsia_tee::Parameter> llcpp_parameters_;

  std::optional<std::vector<Parameter>> parameters_;
};

class OpResult {
 public:
  fuchsia_tee::OpResult to_llcpp() {
    if (return_code_.has_value()) {
      llcpp_builder_.set_return_code(fidl::unowned(&return_code_.value()));
    }

    if (return_origin_.has_value()) {
      llcpp_builder_.set_return_origin(fidl::unowned(&return_origin_.value()));
    }

    if (parameter_set_.has_value()) {
      llcpp_parameter_set_ = parameter_set_->to_llcpp();
      llcpp_builder_.set_parameter_set(fidl::unowned(&llcpp_parameter_set_));
    }

    return llcpp_builder_.build();
  }

  void set_return_code(uint64_t return_code) { return_code_ = return_code; }

  void set_return_origin(fuchsia_tee::ReturnOrigin return_origin) {
    return_origin_ = return_origin;
  }

  void set_parameter_set(ParameterSet parameter_set) { parameter_set_ = std::move(parameter_set); }

 private:
  fuchsia_tee::OpResult::UnownedBuilder llcpp_builder_;
  fidl::VectorView<fuchsia_tee::Parameter> llcpp_parameter_set_;

  std::optional<uint64_t> return_code_{};
  std::optional<fuchsia_tee::ReturnOrigin> return_origin_{};
  std::optional<ParameterSet> parameter_set_{};
};

}  // namespace optee

#endif  // ZIRCON_SYSTEM_DEV_TEE_OPTEE_OPTEE_LLCPP_H_
