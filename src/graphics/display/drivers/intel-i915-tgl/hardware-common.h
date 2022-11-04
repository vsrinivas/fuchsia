// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HARDWARE_COMMON_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HARDWARE_COMMON_H_

#include <lib/stdcompat/span.h>

#include <array>

namespace tgl_registers {

enum class Platform {
  kSkylake,
  kKabyLake,
  kTigerLake,
  kTestDevice,
};

}  // namespace tgl_registers

namespace i915_tgl {

enum DdiId {
  DDI_A = 0,
  DDI_B,
  DDI_C,
  DDI_D,
  DDI_E,

  DDI_TC_1 = DDI_D,
  DDI_TC_2,
  DDI_TC_3,
  DDI_TC_4,
  DDI_TC_5,
  DDI_TC_6,
};

namespace internal {

constexpr std::array kDdisKabyLake = {
    DDI_A, DDI_B, DDI_C, DDI_D, DDI_E,
};

constexpr std::array kDdisTigerLake = {
    DDI_A, DDI_B, DDI_C, DDI_TC_1, DDI_TC_2, DDI_TC_3, DDI_TC_4, DDI_TC_5, DDI_TC_6,
};

}  // namespace internal

template <tgl_registers::Platform P>
constexpr cpp20::span<const DdiId> DdiIds() {
  switch (P) {
    case tgl_registers::Platform::kKabyLake:
    case tgl_registers::Platform::kSkylake:
    case tgl_registers::Platform::kTestDevice:
      return internal::kDdisKabyLake;
    case tgl_registers::Platform::kTigerLake:
      return internal::kDdisTigerLake;
  }
}

}  // namespace i915_tgl

namespace tgl_registers {

// TODO(fxbug.dev/109278): Support Transcoder D on Tiger Lake.
enum Trans {
  TRANS_A = 0,
  TRANS_B,
  TRANS_C,
  TRANS_EDP,
};

namespace internal {

constexpr std::array kTranscodersKabyLake = {
    TRANS_A,
    TRANS_B,
    TRANS_C,
    TRANS_EDP,
};

constexpr std::array kTranscodersTigerLake = {
    TRANS_A,
    TRANS_B,
    TRANS_C,
};

}  // namespace internal

template <Platform P>
constexpr cpp20::span<const Trans> Transcoders() {
  switch (P) {
    case Platform::kKabyLake:
    case Platform::kSkylake:
    case Platform::kTestDevice:
      return internal::kTranscodersKabyLake;
    case Platform::kTigerLake:
      return internal::kTranscodersTigerLake;
  }
}

// TODO(fxbug.dev/109278): Support Pipe D on Tiger Lake.
enum Pipe {
  PIPE_A = 0,
  PIPE_B,
  PIPE_C,
  PIPE_INVALID,
};

namespace internal {

constexpr std::array kPipesKabyLake = {
    PIPE_A,
    PIPE_B,
    PIPE_C,
};

constexpr std::array kPipesTigerLake = {
    PIPE_A,
    PIPE_B,
    PIPE_C,
};

}  // namespace internal

template <Platform P>
constexpr cpp20::span<const Pipe> Pipes() {
  switch (P) {
    case Platform::kKabyLake:
    case Platform::kSkylake:
    case Platform::kTestDevice:
      return internal::kPipesKabyLake;
    case Platform::kTigerLake:
      return internal::kPipesTigerLake;
  }
}

enum Dpll {
  DPLL_INVALID = -1,
  DPLL_0 = 0,
  DPLL_1,
  DPLL_2,
  DPLL_3,

  DPLL_TC_1,
  DPLL_TC_2,
  DPLL_TC_3,
  DPLL_TC_4,
  DPLL_TC_5,
  DPLL_TC_6,
};

namespace internal {

constexpr std::array kDpllsKabyLake = {
    DPLL_0,
    DPLL_1,
    DPLL_2,
    DPLL_3,
};

// TODO(fxbug.dev/110351): Add support for DPLL4.
constexpr std::array kDpllsTigerLake = {
    DPLL_0, DPLL_1, DPLL_2, DPLL_TC_1, DPLL_TC_2, DPLL_TC_3, DPLL_TC_4, DPLL_TC_5, DPLL_TC_6,
};

}  // namespace internal

template <Platform P>
constexpr cpp20::span<const Dpll> Dplls() {
  switch (P) {
    case Platform::kSkylake:
    case Platform::kKabyLake:
    case Platform::kTestDevice:
      return internal::kDpllsKabyLake;
    case Platform::kTigerLake:
      return internal::kDpllsTigerLake;
  }
}

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HARDWARE_COMMON_H_
