// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/nop.h>
#include <lib/code-patching/code-patching.h>
#include <lib/fitx/result.h>

#include <ktl/string_view.h>

namespace code_patching {

fitx::result<Patcher::Error> Patcher::Init(Bootfs bootfs, ktl::string_view directory) {
  ZX_ASSERT(!directory.empty());
  bootfs_ = std::move(bootfs);
  dir_ = directory;

  auto it = bootfs_.find({dir_, kPatchesBin});
  if (auto result = bootfs_.take_error(); result.is_error()) {
    return result;
  }
  if (it == bootfs_.end()) {
    return fitx::error{Error{.reason = "failed to find patch directives"sv}};
  }

  if (it->data.size() % sizeof(Directive) != 0) {
    fitx::error{Error{
        .reason = "patch directive payload has bad size"sv,
        .filename = it->name,
        .entry_offset = it.dirent_offset(),
    }};
  }

  patches_ = {
      reinterpret_cast<const Directive*>(it->data.data()),
      it->data.size() / sizeof(Directive),
  };
  return fitx::ok();
}

fitx::result<Patcher::Error> Patcher::PatchWithAlternative(ktl::span<ktl::byte> instructions,
                                                           ktl::string_view alternative) {
  auto result = GetPatchAlternative(alternative);
  if (result.is_error()) {
    return result.take_error();
  }

  Bytes bytes = std::move(result).value();
  ZX_ASSERT_MSG(
      instructions.size() >= bytes.size(),
      "instruction range (%zu bytes) is too small for patch alternative \"%.*s\" (%zu bytes)",
      instructions.size(), static_cast<int>(alternative.size()), alternative.data(), bytes.size());

  memcpy(instructions.data(), bytes.data(), bytes.size());
  PrepareToSync(instructions);
  return fitx::ok();
}

void Patcher::NopFill(ktl::span<ktl::byte> instructions) {
  arch::NopFill(instructions);
  PrepareToSync(instructions);
}

fitx::result<Patcher::Error, Patcher::Bytes> Patcher::GetPatchAlternative(ktl::string_view name) {
  auto it = bootfs_.find({dir_, kPatchAlternativeDir, name});
  if (auto result = bootfs_.take_error(); result.is_error()) {
    return result.take_error();
  }
  if (it == bootfs_.end()) {
    return fitx::error{Error{.reason = "failed to find patch alternative"sv}};
  }
  return fitx::ok(it->data);
}

}  // namespace code_patching
