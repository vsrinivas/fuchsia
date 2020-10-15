// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
// See kernel/lib/boot-options/README.md for details.
//
// These are the x86-specific boot options.

DEFINE_OPTION("kernel.x86.disable_spec_mitigations", bool, disable_spec_mitigations, {false}, R"""(
TODO(53593)
)""")
DEFINE_OPTION("kernel.x86.md_clear_on_user_return", bool, md_clear_on_user_return, {true}, R"""(
TODO(53593)
)""")
DEFINE_OPTION("kernel.x86.pti.enable", uint32_t, pti_enable, {2}, R"""(
TODO(53593)
)""")
DEFINE_OPTION("kernel.x86.spec_store_bypass_disable", bool, spec_store_bypass_disable, {false},
              R"""(
TODO(53593)
)""")
