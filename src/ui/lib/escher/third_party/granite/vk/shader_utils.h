/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Based on the following files from the Granite rendering engine:
// - vulkan/shader.cpp

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_UTILS_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_UTILS_H_

#include <array>

#include "src/ui/lib/escher/util/enum_count.h"
#include "src/ui/lib/escher/vk/sampler.h"
#include "src/ui/lib/escher/vk/shader_module.h"
#include "src/ui/lib/escher/vk/shader_stage.h"

namespace escher {
namespace impl {

// Reflect on the provided SPIR-V to generate a ShaderModuleResourceLayout,
// which can be used by clients to automate some aspects of pipeline
// generation.
struct ShaderModuleResourceLayout;
void GenerateShaderModuleResourceLayoutFromSpirv(std::vector<uint32_t> spirv, ShaderStage stage,
                                                 ShaderModuleResourceLayout* layout_out);

// Generate a PipelineLayoutSpec using each non-null ShaderStage's
// ShaderModuleResourceLayout.
struct PipelineLayoutSpec;
PipelineLayoutSpec GeneratePipelineLayoutSpec(
    const std::array<ShaderModulePtr, EnumCount<ShaderStage>()>& shader_modules,
    const SamplerPtr& immutable_sampler);

// Given an array of raw push constants, consolidate overlapping and equivalent ranges.
// The resulting array of push constants may therefore have fewer ranges and with each
// range potentially having more than one associated shader stage flag.
//
// Any two (or more) ranges that overlap will be merged into a single range, with the
// resulting range's stage flags containing each of the flags for the ranges that it
// was created from.
std::vector<vk::PushConstantRange> ConsolidatePushConstantRanges(
    const std::vector<vk::PushConstantRange>& ranges);

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_UTILS_H_
