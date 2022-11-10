// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>

#include <phys/stack.h>
#include <phys/symbolize.h>

MainSymbolize::MainSymbolize(const char* name) : Symbolize(name) {
  gSymbolize = this;

#ifdef __ELF__
  static constexpr Symbolize::Stack<BootStack> kBootStacks[] = {
      {boot_stack, "boot"},
      {phys_exception_stack, "exception"},
  };
  set_stacks(ktl::span(kBootStacks));

#if __has_feature(shadow_call_stack)
  static constexpr Symbolize::Stack<BootShadowCallStack> kBootShadowCallStacks[] = {
      {boot_shadow_call_stack, "boot"},
      {phys_exception_shadow_call_stack, "exception"},
  };
  set_shadow_call_stacks(ktl::span(kBootShadowCallStacks));
#endif  // __has_feature(shadow_call_stack)
#endif  // __ELF__

  if (!gBootOptions || gBootOptions->phys_verbose) {
    Context();
  }
}
