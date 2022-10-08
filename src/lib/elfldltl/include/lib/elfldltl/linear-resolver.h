// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace elfldltl {

template <class Elf, class Module>
class ResolvedDefinition {
 public:
  constexpr bool undefined_weak() const { return !symbol_; }

  constexpr typename Elf::size_type bias() const { return module_->bias(); }

  constexpr const typename Elf::Sym& symbol() const { return *symbol_; }

  constexpr typename Elf::size_type tls_module_id() const { return module_->tls_module_id(); }

  constexpr typename Elf::size_type static_tls_bias() const { return module_->static_tls_bias(); }

  constexpr typename Elf::size_type tls_desc_hook() const { return module_->tls_desc_hook(); }

  constexpr typename Elf::size_type tls_desc_value() const {
    return module_->tls_desc_value(symbol().value);
  }

 private:
  const Module* module_ = nullptr;
  const typename Elf::Sym* symbol_ = nullptr;
};

}  // namespace elfldltl
