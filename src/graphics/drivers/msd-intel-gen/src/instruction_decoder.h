// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_INTEL_GEN_SRC_INSTRUCTION_DECODER_H_
#define SRC_GRAPHICS_DRIVERS_MSD_INTEL_GEN_SRC_INSTRUCTION_DECODER_H_

#include <stdint.h>

class InstructionDecoder {
 public:
  enum Id {
    NOOP = 0x0,
    MI_BATCH_BUFFER_END = 0x0500,
    LOAD_REGISTER_IMM = 0x1100,
    _3DSTATE_CLEAR_PARAMS = 0x7804,
    _3DSTATE_DEPTH_BUFFER = 0x7805,
    _3DSTATE_STENCIL_BUFFER = 0x7806,
    _3DSTATE_HIER_DEPTH_BUFFER = 0x7807,
    _3DSTATE_VERTEX_BUFFERS = 0x7808,
    _3DSTATE_VERTEX_ELEMENTS = 0x7809,
    _3DSTATE_MULTISAMPLE = 0x780d,
    _3DSTATE_INDEX_BUFFER = 0x780a,
    _3DSTATE_VF = 0x780c,
    _3DSTATE_SCISSOR_STATE_POINTERS = 0x780f,
    _3DSTATE_VS = 0x7810,
    _3DSTATE_GS = 0x7811,
    _3DSTATE_CLIP = 0x7812,
    _3DSTATE_SF = 0x7813,
    _3DSTATE_WM = 0x7814,
    _3DSTATE_CONSTANT_VS = 0x7815,
    _3DSTATE_CONSTANT_GS = 0x7816,
    _3DSTATE_CONSTANT_PS = 0x7817,
    _3DSTATE_SAMPLE_MASK = 0x7818,
    _3DSTATE_CONSTANT_HS = 0x7819,
    _3DSTATE_CONSTANT_DS = 0x781a,
    _3DSTATE_HS = 0x781b,
    _3DSTATE_TE = 0x781c,
    _3DSTATE_DS = 0x781d,
    _3DSTATE_STREAMOUT = 0x781e,
    _3DSTATE_SBE = 0x781f,
    _3DSTATE_PS = 0x7820,
    _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP = 0x7821,
    _3DSTATE_VIEWPORT_STATE_POINTERS_CC = 0x7823,
    _3DSTATE_BINDING_TABLE_POINTERS_VS = 0x7826,
    _3DSTATE_BINDING_TABLE_POINTERS_HS = 0x7827,
    _3DSTATE_BINDING_TABLE_POINTERS_DS = 0x7828,
    _3DSTATE_BINDING_TABLE_POINTERS_GS = 0x7829,
    _3DSTATE_BINDING_TABLE_POINTERS_PS = 0x782a,
    _3DSTATE_SAMPLER_STATE_POINTERS_PS = 0x782f,
    _3DSTATE_CC_STATE_POINTERS = 0x780e,
    _3DSTATE_BLEND_STATE_POINTERS = 0x7824,
    _3DSTATE_URB_VS = 0x7830,
    _3DSTATE_URB_HS = 0x7831,
    _3DSTATE_URB_DS = 0x7832,
    _3DSTATE_URB_GS = 0x7833,
    _3DSTATE_VF_INSTANCING = 0x7849,
    _3DSTATE_VF_SGVS = 0x784a,
    _3DSTATE_VF_TOPOLOGY = 0x784b,
    _3DSTATE_PS_BLEND = 0x784d,
    _3DSTATE_WM_DEPTH_STENCIL = 0x784e,
    _3DSTATE_PS_EXTRA = 0x784f,
    _3DSTATE_RASTER = 0x7850,
    _3DSTATE_SBE_SWIZ = 0x7851,
    _3DSTATE_WM_HZ_OP = 0x7852,
    _3DSTATE_PUSH_CONSTANT_ALLOC_VS = 0x7912,
    _3DSTATE_PUSH_CONSTANT_ALLOC_HS = 0x7913,
    _3DSTATE_PUSH_CONSTANT_ALLOC_DS = 0x7914,
    _3DSTATE_PUSH_CONSTANT_ALLOC_GS = 0x7915,
    _3DSTATE_PUSH_CONSTANT_ALLOC_PS = 0x7916,
    PIPE_CONTROL = 0x7a00,
    _3DPRIMITIVE = 0x7b00,
    STATE_BASE_ADDRESS = 0x6101,
    PIPELINE_SELECT = 0x6904,
  };

  static const char* name(Id id) {
    switch (id) {
      case _3DSTATE_VERTEX_BUFFERS:
        return "3DSTATE_VERTEX_BUFFERS";
      case _3DSTATE_VERTEX_ELEMENTS:
        return "3DSTATE_VERTEX_ELEMENTS";
      case LOAD_REGISTER_IMM:
        return "LOAD_REGISTER_IMM";
      case PIPE_CONTROL:
        return "PIPE_CONTROL";
      case PIPELINE_SELECT:
        return "PIPELINE_SELECT";
      case STATE_BASE_ADDRESS:
        return "STATE_BASE_ADDRESS";
      case _3DSTATE_VF_SGVS:
        return "3DSTATE_VF_SGVS";
      case _3DSTATE_VF_INSTANCING:
        return "3DSTATE_VF_INSTANCING";
      case _3DSTATE_VF_TOPOLOGY:
        return "3DSTATE_VF_TOPOLOGY";
      case _3DSTATE_URB_VS:
        return "3DSTATE_URB_VS";
      case _3DSTATE_URB_HS:
        return "3DSTATE_URB_HS";
      case _3DSTATE_URB_DS:
        return "3DSTATE_URB_DS";
      case _3DSTATE_URB_GS:
        return "3DSTATE_URB_GS";
      case _3DSTATE_BLEND_STATE_POINTERS:
        return "3DSTATE_BLEND_STATE_POINTERS";
      case _3DSTATE_PS_BLEND:
        return "3DSTATE_PS_BLEND";
      case _3DSTATE_CC_STATE_POINTERS:
        return "3DSTATE_CC_STATE_POINTERS";
      case _3DSTATE_WM_DEPTH_STENCIL:
        return "3DSTATE_WM_DEPTH_STENCIL";
      case _3DSTATE_CONSTANT_VS:
        return "3DSTATE_CONSTANT_VS";
      case _3DSTATE_CONSTANT_HS:
        return "3DSTATE_CONSTANT_HS";
      case _3DSTATE_CONSTANT_DS:
        return "3DSTATE_CONSTANT_DS";
      case _3DSTATE_CONSTANT_GS:
        return "3DSTATE_CONSTANT_GS";
      case _3DSTATE_CONSTANT_PS:
        return "3DSTATE_CONSTANT_PS";
      case _3DSTATE_BINDING_TABLE_POINTERS_VS:
        return "3DSTATE_BINDING_TABLE_POINTERS_VS";
      case _3DSTATE_BINDING_TABLE_POINTERS_HS:
        return "3DSTATE_BINDING_TABLE_POINTERS_HS";
      case _3DSTATE_BINDING_TABLE_POINTERS_DS:
        return "3DSTATE_BINDING_TABLE_POINTERS_DS";
      case _3DSTATE_BINDING_TABLE_POINTERS_GS:
        return "3DSTATE_BINDING_TABLE_POINTERS_GS";
      case _3DSTATE_BINDING_TABLE_POINTERS_PS:
        return "3DSTATE_BINDING_TABLE_POINTERS_PS";
      case _3DSTATE_SAMPLER_STATE_POINTERS_PS:
        return "3DSTATE_SAMPLER_STATE_POINTERS_PS";
      case _3DSTATE_MULTISAMPLE:
        return "3DSTATE_MULTISAMPLE";
      case _3DSTATE_SAMPLE_MASK:
        return "3DSTATE_SAMPLE_MASK";
      case _3DSTATE_VS:
        return "3DSTATE_VS";
      case _3DSTATE_HS:
        return "3DSTATE_HS";
      case _3DSTATE_TE:
        return "3DSTATE_TE";
      case _3DSTATE_DS:
        return "3DSTATE_DS";
      case _3DSTATE_STREAMOUT:
        return "3DSTATE_STREAMOUT";
      case _3DSTATE_GS:
        return "3DSTATE_GS";
      case _3DSTATE_CLIP:
        return "3DSTATE_CLIP";
      case _3DSTATE_SF:
        return "3DSTATE_SF";
      case _3DSTATE_RASTER:
        return "3DSTATE_RASTER";
      case _3DSTATE_SBE:
        return "3DSTATE_SBE";
      case _3DSTATE_WM:
        return "3DSTATE_WM";
      case _3DSTATE_PS:
        return "3DSTATE_PS";
      case _3DSTATE_PS_EXTRA:
        return "3DSTATE_PS_EXTRA";
      case _3DSTATE_VIEWPORT_STATE_POINTERS_CC:
        return "3DSTATE_VIEWPORT_STATE_POINTERS_CC";
      case _3DSTATE_DEPTH_BUFFER:
        return "3DSTATE_DEPTH_BUFFER";
      case _3DSTATE_HIER_DEPTH_BUFFER:
        return "3DSTATE_HIER_DEPTH_BUFFER";
      case _3DSTATE_STENCIL_BUFFER:
        return "3DSTATE_STENCIL_BUFFER";
      case _3DSTATE_CLEAR_PARAMS:
        return "3DSTATE_CLEAR_PARAMS";
      case _3DPRIMITIVE:
        return "3DPRIMITIVE";
      case _3DSTATE_INDEX_BUFFER:
        return "3DSTATE_INDEX_BUFFER";
      case _3DSTATE_SBE_SWIZ:
        return "3DSTATE_SBE_SWIZ";
      case _3DSTATE_PUSH_CONSTANT_ALLOC_VS:
        return "3DSTATE_PUSH_CONSTANT_ALLOC_VS";
      case _3DSTATE_PUSH_CONSTANT_ALLOC_HS:
        return "3DSTATE_PUSH_CONSTANT_ALLOC_HS";
      case _3DSTATE_PUSH_CONSTANT_ALLOC_DS:
        return "3DSTATE_PUSH_CONSTANT_ALLOC_DS";
      case _3DSTATE_PUSH_CONSTANT_ALLOC_GS:
        return "3DSTATE_PUSH_CONSTANT_ALLOC_GS";
      case _3DSTATE_PUSH_CONSTANT_ALLOC_PS:
        return "3DSTATE_PUSH_CONSTANT_ALLOC_PS";
      case _3DSTATE_WM_HZ_OP:
        return "3DSTATE_WM_HZ_OP";
      case _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP:
        return "3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP";
      case _3DSTATE_SCISSOR_STATE_POINTERS:
        return "3DSTATE_SCISSOR_STATE_POINTERS";
      case _3DSTATE_VF:
        return "3DSTATE_VF";
      case MI_BATCH_BUFFER_END:
        return "MI_BATCH_BUFFER_END";
      case NOOP:
        return "NOOP";
    }
    return "UNKNOWN";
  }

  static bool Decode(uint32_t dword, Id* id_out, uint32_t* dword_count_out) {
    if (dword == 0) {
      *id_out = NOOP;
      *dword_count_out = 1;
      return true;
    }

    uint16_t id = dword >> 16;
    switch (id) {
      case PIPELINE_SELECT:
      case MI_BATCH_BUFFER_END:
        *dword_count_out = 1;
        break;
      case LOAD_REGISTER_IMM:
        *dword_count_out = 3;
        break;
      case _3DSTATE_BLEND_STATE_POINTERS:
      case _3DSTATE_CC_STATE_POINTERS:
      case _3DSTATE_VIEWPORT_STATE_POINTERS_CC:
        *dword_count_out = 2;
        break;
      case _3DSTATE_VF:
      case _3DSTATE_SCISSOR_STATE_POINTERS:
      case _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP:
      case _3DSTATE_PUSH_CONSTANT_ALLOC_PS:
      case _3DSTATE_PUSH_CONSTANT_ALLOC_GS:
      case _3DSTATE_PUSH_CONSTANT_ALLOC_DS:
      case _3DSTATE_PUSH_CONSTANT_ALLOC_HS:
      case _3DSTATE_PUSH_CONSTANT_ALLOC_VS:
      case _3DSTATE_SBE_SWIZ:
      case _3DSTATE_INDEX_BUFFER:
      case _3DPRIMITIVE:
      case _3DSTATE_CLEAR_PARAMS:
      case _3DSTATE_STENCIL_BUFFER:
      case _3DSTATE_HIER_DEPTH_BUFFER:
      case _3DSTATE_DEPTH_BUFFER:
      case _3DSTATE_PS_EXTRA:
      case _3DSTATE_PS:
      case _3DSTATE_WM:
      case _3DSTATE_SBE:
      case _3DSTATE_RASTER:
      case _3DSTATE_SF:
      case _3DSTATE_CLIP:
      case _3DSTATE_GS:
      case _3DSTATE_STREAMOUT:
      case _3DSTATE_DS:
      case _3DSTATE_TE:
      case _3DSTATE_VS:
      case _3DSTATE_HS:
      case _3DSTATE_SAMPLE_MASK:
      case _3DSTATE_MULTISAMPLE:
      case _3DSTATE_SAMPLER_STATE_POINTERS_PS:
      case _3DSTATE_BINDING_TABLE_POINTERS_PS:
      case _3DSTATE_BINDING_TABLE_POINTERS_GS:
      case _3DSTATE_BINDING_TABLE_POINTERS_DS:
      case _3DSTATE_BINDING_TABLE_POINTERS_HS:
      case _3DSTATE_BINDING_TABLE_POINTERS_VS:
      case _3DSTATE_CONSTANT_PS:
      case _3DSTATE_CONSTANT_GS:
      case _3DSTATE_CONSTANT_DS:
      case _3DSTATE_CONSTANT_HS:
      case _3DSTATE_CONSTANT_VS:
      case _3DSTATE_WM_DEPTH_STENCIL:
      case _3DSTATE_PS_BLEND:
      case _3DSTATE_URB_GS:
      case _3DSTATE_URB_DS:
      case _3DSTATE_URB_HS:
      case _3DSTATE_URB_VS:
      case _3DSTATE_VF_TOPOLOGY:
      case _3DSTATE_VF_INSTANCING:
      case _3DSTATE_VF_SGVS:
      case _3DSTATE_VERTEX_BUFFERS:
      case _3DSTATE_VERTEX_ELEMENTS:
      case _3DSTATE_WM_HZ_OP:
      case PIPE_CONTROL:
      case STATE_BASE_ADDRESS:
        *dword_count_out = (dword & 0xFF) + 2;
        break;
      default:
        return false;
    }
    *id_out = static_cast<Id>(id);
    return true;
  }
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_INTEL_GEN_SRC_INSTRUCTION_DECODER_H_
