// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compute_view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <fbl/algorithm.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/naive_buffer.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/renderer/buffer_cache.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/vk/texture.h"

#include "third_party/shaderc/libshaderc/include/shaderc/shaderc.hpp"

namespace frame_compression {
namespace {

// Inspect values.
constexpr char kView[] = "view";
constexpr char kModifier[] = "modifier";
constexpr char kImage[] = "image";
constexpr char kImageBytes[] = "image_bytes";
constexpr char kImageBytesUsed[] = "image_bytes_used";
constexpr char kWidthInTiles[] = "width_in_tiles";
constexpr char kHeightInTiles[] = "height_in_tiles";

constexpr char kLinearShaderSrc[] = R"GLSL(
#version 450

layout (binding = 0, rgba8) writeonly uniform image2D image;

layout (push_constant) uniform PushConstantBlock {
    uint color_offset;
} params;

void main()
{
    // Linear color space.
    const vec4 kColor0 = vec4(0.991, 0.065, 0.127, 1.0);
    const vec4 kColor1 = vec4(0.831, 0.665, 0.451, 1.0);

    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    imageStore(image, dst, dst.y >= params.color_offset ? kColor0 : kColor1);
}
)GLSL";

struct LinearPushConstantBlock {
  uint32_t color_offset;
};

constexpr char kAfbcShaderSrc[] = R"GLSL(
#version 450

layout (binding = 0, rgba8) writeonly uniform image2D image;

layout(std430, binding = 1) buffer BlockHeader {
    writeonly uint data[];
} header;

layout (push_constant) uniform PushConstantBlock {
    uint color_offset;
    uint base_y;
    uint width_in_tiles;
} params;

void main()
{
    // Linear color space.
    const vec4 kColor0 = vec4(0.991, 0.065, 0.127, 1.0);
    const vec4 kColor1 = vec4(0.831, 0.665, 0.451, 1.0);

    // AFBC constants.
    const uint kAfbcTilePixelWidth = 16;
    const uint kAfbcTilePixelHeight = 16;
    const uint kAfbcHeaderTileWidth = 8;
    const uint kAfbcHeaderTileHeight = 8;
    const uint kAfbcHeaderTileBlocks = kAfbcHeaderTileWidth * kAfbcHeaderTileHeight;
    const uint kAfbcUintsPerBlockHeader = 4;
    const uint kAfbcTilePixels = kAfbcTilePixelWidth * kAfbcTilePixelHeight;

    uint i = gl_GlobalInvocationID.x;
    uint j = gl_GlobalInvocationID.y;

    uint tile_y = j * kAfbcTilePixelHeight;
    uint tile_y_end = tile_y + kAfbcTilePixelHeight;

    uint width_in_superblocks = params.width_in_tiles / kAfbcHeaderTileWidth;

    //
    // Tiled headers are stored in 8x8 superblocks. Each superblock contains
    // four 4x4 blocks, each 4x4 block contains four 2x2 smaller blocks.
    //
    uint superblock_i = i / kAfbcHeaderTileWidth;
    uint superblock_x = i % kAfbcHeaderTileWidth;
    uint block_4x4_i = superblock_x / 4;
    uint block_4x4_x = superblock_x % 4;
    uint block_2x2_i = block_4x4_x / 2;
    uint block_2x2_x = block_4x4_x % 2;

    uint superblock_j = j / kAfbcHeaderTileHeight;
    uint superblock_y = j % kAfbcHeaderTileHeight;
    uint block_4x4_j = superblock_y / 4;
    uint block_4x4_y = superblock_y % 4;
    uint block_2x2_j = block_4x4_y / 2;
    uint block_2x2_y = block_4x4_y % 2;

    uint superblock_idx = superblock_j * width_in_superblocks + superblock_i;

    uint tile_idx = superblock_idx * kAfbcHeaderTileBlocks;
    tile_idx += (block_4x4_j * 2 + block_4x4_i) * 16;
    tile_idx += (block_2x2_j * 2 + block_2x2_i) * 4;
    tile_idx += block_2x2_y * 2 + block_2x2_x;

    uint header_offset = tile_idx * kAfbcUintsPerBlockHeader;

    // Produce solid color tile if possible.
    if (tile_y >= params.color_offset || tile_y_end < params.color_offset)
    {
        // Reset header to zero, except for offset == 2, which is set below.
        header.data[header_offset + 0] = 0;
        header.data[header_offset + 1] = 0;
        header.data[header_offset + 3] = 0;

        // Determine color of tile based on color offset.
        vec4 color = tile_y >= params.color_offset ? kColor0 : kColor1;

        // Solid colors are stored at offset 2 in the block header.
        uint u = (header_offset + 2) % kAfbcTilePixels;
        uint v = (header_offset + 2) / kAfbcTilePixels;
        imageStore(image, ivec2(u, v), color);
    }
    else
    {
        // AFBC sub-tile layout.
        const ivec2 kSubtileOffset[16] = {
            ivec2(4, 4),
            ivec2(0, 4),
            ivec2(0, 0),
            ivec2(4, 0),
            ivec2(8, 0),
            ivec2(12, 0),
            ivec2(12, 4),
            ivec2(8, 4),
            ivec2(8, 8),
            ivec2(12, 8),
            ivec2(12, 12),
            ivec2(8, 12),
            ivec2(4, 12),
            ivec2(0, 12),
            ivec2(0, 8),
            ivec2(4, 8),
        };
        const uint kAfbcSubtileSize = 4;
        const uint kAfbcSubtileNumPixels = 16;
        const uint kAfbcTileNumBytes = kAfbcTilePixels * 4;

        // V coordinate for tile. Each tile occupies one row.
        uint tile_v = params.base_y + tile_idx;

        // Iterate over all 16 sub-tiles.
        for (uint k = 0; k < 16; k++)
        {
            uint u_base = kAfbcSubtileNumPixels * k;

            for (uint yy = 0; yy < kAfbcSubtileSize; yy++)
            {
                uint u = u_base + yy * kAfbcSubtileSize;
                uint y = tile_y + kSubtileOffset[k].y + yy;

                // Determine color of sub-tile row based on color
                // offset.
                vec4 color = y >= params.color_offset ? kColor0 : kColor1;

                // Write sub-tile row.
                for (uint xx = 0; xx < kAfbcSubtileSize; xx++)
                {
                    imageStore(image, ivec2(u + xx, tile_v), color);
                }
            }
        }

        // AFBC body can be found by multiplying |base_y| with the
        // number of bytes per tile.
        uint body_base = params.base_y * kAfbcTileNumBytes;
        uint tile_offset = body_base + kAfbcTileNumBytes * tile_idx;

        // Store offset of uncompressed tile memory at 0.
        header.data[header_offset] = tile_offset;

        // Disable compression for tile memory.
        header.data[header_offset + 1] =
            0x41 << 0 | 0x10 << 8 | 0x04 << 16 | 0x41 << 24;
        header.data[header_offset + 2] =
            0x10 << 0 | 0x04 << 8 | 0x41 << 16 | 0x10 << 24;
        header.data[header_offset + 3] =
            0x04 << 0 | 0x41 << 8 | 0x10 << 16 | 0x04 << 24;
    }
}
)GLSL";

struct AfbcPushConstantBlock {
  uint32_t color_offset;
  uint32_t base_y;
  uint32_t width_in_tiles;
};

const char* GetShaderSrc(uint64_t modifier) {
  switch (modifier) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER:
      return kAfbcShaderSrc;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR:
      return kLinearShaderSrc;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }
  return nullptr;
}

uint32_t GetPushConstantBlockSize(uint64_t modifier) {
  switch (modifier) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER:
      return sizeof(AfbcPushConstantBlock);
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR:
      return sizeof(LinearPushConstantBlock);
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }
  return 0;
}

const vk::DescriptorSetLayoutCreateInfo& GetDescriptorSetLayoutCreateInfo(uint64_t modifier) {
  static vk::DescriptorSetLayoutCreateInfo* ptr = nullptr;
  switch (modifier) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER: {
      constexpr uint32_t kNumBindings = 2;
      static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
      static vk::DescriptorSetLayoutCreateInfo info;
      if (!ptr) {
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        info.bindingCount = kNumBindings;
        info.pBindings = bindings;
        ptr = &info;
      }
    } break;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR: {
      constexpr uint32_t kNumBindings = 1;
      static vk::DescriptorSetLayoutBinding bindings[kNumBindings];
      static vk::DescriptorSetLayoutCreateInfo info;
      if (!ptr) {
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        info.bindingCount = kNumBindings;
        info.pBindings = bindings;
        ptr = &info;
      }
    } break;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
      break;
  }
  return *ptr;
}

std::vector<uint32_t> CompileToSpirv(shaderc::Compiler* compiler, std::string code,
                                     shaderc_shader_kind kind, std::string name) {
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
  options.SetWarningsAsErrors();

  auto result =
      compiler->CompileGlslToSpv(code.data(), code.size(), kind, name.c_str(), "main", options);
  auto status = result.GetCompilationStatus();
  FX_CHECK(status == shaderc_compilation_status_success);
  return {result.cbegin(), result.cend()};
}

zx::event DuplicateEvent(const zx::event& evt) {
  zx::event dup;
  auto status = evt.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  FX_CHECK(status == ZX_OK);
  return dup;
}

escher::GpuMemPtr ImportMemory(vk::Device vk_device,
                               const vk::MemoryAllocateInfo& allocation_info) {
  auto result = vk_device.allocateMemory(allocation_info);
  FX_CHECK(result.result == vk::Result::eSuccess);

  return escher::GpuMem::AdoptVkMemory(vk_device, result.value, allocation_info.allocationSize,
                                       false);
}

escher::GpuMemPtr ImportMemoryFromVmo(vk::Device vk_device, const zx::vmo& vmo,
                                      const vk::MemoryRequirements& memory_requirements) {
  zx::vmo duplicated_vmo;
  auto status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicated_vmo);
  FX_CHECK(status == ZX_OK);

  auto memory_import_info = vk::ImportMemoryZirconHandleInfoFUCHSIA(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA, duplicated_vmo.release());

  vk::MemoryAllocateInfo allocation_info;
  allocation_info.setPNext(&memory_import_info);
  allocation_info.allocationSize = memory_requirements.size;
  allocation_info.memoryTypeIndex = escher::CountTrailingZeros(memory_requirements.memoryTypeBits);

  return ImportMemory(vk_device, allocation_info);
}

escher::BufferPtr CreateBufferFromMemory(escher::Escher* escher, vk::DeviceSize size,
                                         zx::vmo& image_vmo) {
  vk::ExternalMemoryBufferCreateInfo external_buffer_create_info;
  external_buffer_create_info.handleTypes =
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;

  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.pNext = &external_buffer_create_info;
  buffer_create_info.size = size;
  buffer_create_info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

  auto vk_device = escher->vulkan_context().device;

  vk::Buffer vk_buffer;
  {
    auto result = vk_device.createBuffer(buffer_create_info);
    FX_CHECK(result.result == vk::Result::eSuccess);
    vk_buffer = result.value;
  }

  vk::MemoryRequirements buffer_memory_requirements;
  vk_device.getBufferMemoryRequirements(vk_buffer, &buffer_memory_requirements);

  auto buffer_gpu_mem = ImportMemoryFromVmo(vk_device, image_vmo, buffer_memory_requirements);
  return escher::impl::NaiveBuffer::AdoptVkBuffer(
      escher->resource_recycler(), std::move(buffer_gpu_mem), buffer_create_info.size, vk_buffer);
}

vk::PipelineLayout CreatePipelineLayout(escher::Escher* escher, vk::DescriptorSetLayout layout,
                                        uint32_t push_constant_block_size) {
  auto vk_device = escher->vulkan_context().device;

  vk::PushConstantRange push_constant_range;
  push_constant_range.offset = 0;
  push_constant_range.size = push_constant_block_size;
  vk::PipelineLayoutCreateInfo pipeline_layout_info;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &layout;
  pipeline_layout_info.pushConstantRangeCount = push_constant_block_size ? 1 : 0;
  pipeline_layout_info.pPushConstantRanges = &push_constant_range;
  auto result = vk_device.createPipelineLayout(pipeline_layout_info);
  FX_CHECK(result.result == vk::Result::eSuccess);
  return result.value;
}

vk::Pipeline CreatePipeline(escher::Escher* escher, const char* shader_src,
                            vk::PipelineLayout pipeline_layout) {
  auto compiler = escher->shaderc_compiler();
  FX_CHECK(compiler);

  auto vk_device = escher->vulkan_context().device;

  vk::ShaderModule module;
  {
    auto spirv = CompileToSpirv(compiler, shader_src, shaderc_glsl_compute_shader, "ComputeShader");
    vk::ShaderModuleCreateInfo module_info;
    module_info.codeSize = spirv.size() * sizeof(uint32_t);
    module_info.pCode = spirv.data();
    auto result = vk_device.createShaderModule(module_info);
    FX_CHECK(result.result == vk::Result::eSuccess);
    module = result.value;
  }

  vk::Pipeline pipeline;
  {
    vk::PipelineShaderStageCreateInfo stage_info;
    stage_info.stage = vk::ShaderStageFlagBits::eCompute;
    stage_info.module = module;
    stage_info.pName = "main";
    vk::ComputePipelineCreateInfo pipeline_info;
    pipeline_info.stage = stage_info;
    pipeline_info.layout = pipeline_layout;
    auto result = vk_device.createComputePipelines(vk::PipelineCache(), {pipeline_info});
    FX_CHECK(result.result == vk::Result::eSuccess);
    pipeline = result.value[0];
  }

  vk_device.destroyShaderModule(module);

  return pipeline;
}

}  // namespace

ComputeView::ComputeView(scenic::ViewContext context, escher::EscherWeakPtr weak_escher,
                         uint64_t modifier, uint32_t width, uint32_t height, uint32_t paint_count,
                         FILE* png_fp, inspect::Node inspect_node)
    : BaseView(std::move(context), "Compute View Example", width, height, std::move(inspect_node)),
      escher_(std::move(weak_escher)),
      modifier_(modifier),
      paint_count_(paint_count),
      png_fp_(png_fp),
      descriptor_set_pool_(escher_->GetWeakPtr(), GetDescriptorSetLayoutCreateInfo(modifier)),
      inspect_node_(top_inspect_node_.CreateLazyValues(kView, [this] { return PopulateStats(); })) {
  zx_status_t status = component_context()->svc()->Connect(sysmem_allocator_.NewRequest());
  FX_CHECK(status == ZX_OK);

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr scenic_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  status = local_token->Sync();
  FX_CHECK(status == ZX_OK);

  const uint32_t kBufferId = 1;
  session()->RegisterBufferCollection(kBufferId, std::move(scenic_token));

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                   buffer_collection.NewRequest());
  FX_CHECK(status == ZX_OK);

  //
  // Set buffer collection constraints for compute usage.
  //

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = kNumImages;
  constraints.usage.vulkan = fuchsia::sysmem::VULKAN_IMAGE_USAGE_STORAGE;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  fuchsia::sysmem::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints = fuchsia::sysmem::ImageFormatConstraints();
  image_constraints.min_coded_width = width_;
  image_constraints.min_coded_height = height_;
  image_constraints.max_coded_width = width_;
  image_constraints.max_coded_height = height_;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = modifier_;

  // Force bytes per row to 4 * |width_| when using linear buffer.
  if (modifier_ == fuchsia::sysmem::FORMAT_MODIFIER_LINEAR) {
    image_constraints.min_bytes_per_row = width_ * 4;
    image_constraints.max_bytes_per_row = width_ * 4;
  }

  status = buffer_collection->SetConstraints(true, constraints);
  FX_CHECK(status == ZX_OK);

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_CHECK(status == ZX_OK);
  FX_CHECK(allocation_status == ZX_OK);
  FX_CHECK(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
           image_constraints.pixel_format.type);

  auto vk_device = escher_->vulkan_context().device;

  //
  // Initialize images from allocated buffer collection.
  //

  for (uint32_t i = 0; i < kNumImages; ++i) {
    auto& image = images_[i];

    auto acquire_semaphore_pair = escher::NewSemaphoreEventPair(escher_.get());
    auto release_semaphore_pair = escher::NewSemaphoreEventPair(escher_.get());
    FX_CHECK(acquire_semaphore_pair.first && release_semaphore_pair.first);

    // The release fences should be immediately ready to render, since they are
    // passed to DrawFrame() as the 'framebuffer_ready' semaphore.
    release_semaphore_pair.second.signal(0u, escher::kFenceSignalled);

    image.acquire_semaphore = std::move(acquire_semaphore_pair.first);
    image.release_semaphore = std::move(release_semaphore_pair.first);
    image.acquire_fence = std::move(acquire_semaphore_pair.second);
    image.release_fence = std::move(release_semaphore_pair.second);
    image.image_id = session()->AllocResourceId();
    fuchsia::sysmem::ImageFormat_2 image_format = {};
    image_format.coded_width = width_;
    image_format.coded_height = height_;
    session()->Enqueue(scenic::NewCreateImage2Cmd(image.image_id, width_, height_, kBufferId, i));

    FX_CHECK(buffer_collection_info.buffers[i].vmo != ZX_HANDLE_INVALID);
    zx::vmo& image_vmo = buffer_collection_info.buffers[i].vmo;

    //
    // Import memory for image usage.
    //

    vk::ExternalMemoryImageCreateInfo external_image_create_info;
    external_image_create_info.handleTypes =
        vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;

    vk::ImageCreateInfo image_create_info;
    image_create_info.pNext = &external_image_create_info;
    image_create_info.imageType = vk::ImageType::e2D;
    // Use SRGB format to demonstrate how GPU can be used to convert
    // from linear to sRGB.
    image_create_info.format = vk::Format::eR8G8B8A8Srgb;
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = vk::SampleCountFlagBits::e1;
    image_create_info.tiling = vk::ImageTiling::eLinear;
    image_create_info.usage = vk::ImageUsageFlagBits::eStorage;
    image_create_info.sharingMode = vk::SharingMode::eExclusive;
    image_create_info.initialLayout = vk::ImageLayout::eUndefined;
    image_create_info.flags = vk::ImageCreateFlags();

    switch (modifier) {
      case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER: {
        uint32_t width_in_tiles =
            fbl::round_up(width_, kTiledAfbcWidthAlignment) / kAfbcTilePixelWidth;
        uint32_t height_in_tiles =
            fbl::round_up(height_, kTiledAfbcHeightAlignment) / kAfbcTilePixelHeight;
        uint32_t tile_count = width_in_tiles * height_in_tiles;
        uint32_t tile_num_pixels = kAfbcTilePixelWidth * kAfbcTilePixelHeight;
        uint32_t tile_num_bytes = tile_num_pixels * kTileBytesPerPixel;
        uint32_t body_offset =
            fbl::round_up(tile_count * kAfbcBytesPerBlockHeader, kTiledAfbcBodyAlignment);

        // Create linear image where each tile occupies one row. The block headers are
        // stored on the first rows and must be aligned to the row size.
        FX_CHECK((body_offset % tile_num_bytes) == 0);
        image_create_info.extent =
            vk::Extent3D{tile_num_pixels, body_offset / tile_num_bytes + tile_count, 1};
        image.body_offset = body_offset;
        image.base_y = body_offset / tile_num_bytes;
        image.width_in_tiles = width_in_tiles;
        image.height_in_tiles = height_in_tiles;
      } break;
      case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR:
        image_create_info.extent = vk::Extent3D{width_, height_, 1};
        break;
      default:
        FX_NOTREACHED() << "Modifier not supported.";
    }

    vk::Image vk_image;
    {
      auto result = vk_device.createImage(image_create_info);
      FX_CHECK(result.result == vk::Result::eSuccess);
      vk_image = result.value;
    }

    // Verify |rowPitch| when using linear modifier.
    if (modifier_ == fuchsia::sysmem::FORMAT_MODIFIER_LINEAR) {
      vk::ImageSubresource subresource;
      subresource.arrayLayer = 0;
      subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      subresource.mipLevel = 0;
      auto layout = vk_device.getImageSubresourceLayout(vk_image, subresource);
      FX_CHECK(layout.rowPitch == (width_ * 4));
    }

    vk::MemoryRequirements image_memory_requirements;
    vk_device.getImageMemoryRequirements(vk_image, &image_memory_requirements);

    auto image_gpu_mem = ImportMemoryFromVmo(vk_device, image_vmo, image_memory_requirements);

    escher::ImageInfo image_info;
    image_info.format = image_create_info.format;
    image_info.width = image_create_info.extent.width;
    image_info.height = image_create_info.extent.height;
    image_info.usage = image_create_info.usage;
    image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    image_info.is_external = true;

    auto escher_image = escher::impl::NaiveImage::AdoptVkImage(
        escher_->resource_recycler(), image_info, vk_image, std::move(image_gpu_mem),
        image_create_info.initialLayout);

    image.texture = escher_->NewTexture(std::move(escher_image), vk::Filter::eNearest);

    //
    // Import the same memory for buffer usage.
    //

    image.buffer = CreateBufferFromMemory(
        escher_.get(), buffer_collection_info.settings.buffer_settings.size_bytes, image_vmo);

    image.inspect_node = top_inspect_node_.CreateLazyNode(kImage + std::to_string(i), [this, i] {
      auto& image = images_[i];
      return PopulateImageStats(image);
    });
  }

  buffer_collection->Close();

  // Initialize scratch images for conversion of PNG to packed AFBC.
  if (png_fp_ && modifier_ == fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER) {
    vk::ImageCreateInfo image_create_info;
    image_create_info.imageType = vk::ImageType::e2D;
    image_create_info.format = vk::Format::eR8G8B8A8Srgb;
    image_create_info.extent = vk::Extent3D{width_, height_, 1};
    image_create_info.mipLevels = 1;
    image_create_info.arrayLayers = 1;
    image_create_info.samples = vk::SampleCountFlagBits::e1;
    image_create_info.tiling = vk::ImageTiling::eOptimal;
    image_create_info.usage =
        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    image_create_info.sharingMode = vk::SharingMode::eExclusive;
    image_create_info.initialLayout = vk::ImageLayout::eUndefined;
    image_create_info.flags = vk::ImageCreateFlagBits::eCompactFUCHSIA;

    vk::Image vk_image;
    {
      auto result = vk_device.createImage(image_create_info);
      FX_CHECK(result.result == vk::Result::eSuccess);
      vk_image = result.value;
    }

    vk::MemoryRequirements image_memory_requirements;
    vk_device.getImageMemoryRequirements(vk_image, &image_memory_requirements);

    vk::BufferCreateInfo buffer_create_info;
    buffer_create_info.size = image_memory_requirements.size;
    buffer_create_info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    buffer_create_info.sharingMode = vk::SharingMode::eExclusive;

    {
      auto result = vk_device.createBuffer(buffer_create_info);
      FX_CHECK(result.result == vk::Result::eSuccess);
      scratch_buffer_ = result.value;
    }

    vk::MemoryDedicatedAllocateInfo dedicated_allocate_info;
    dedicated_allocate_info.image = vk_image;
    vk::MemoryAllocateInfo allocation_info;
    allocation_info.setPNext(&dedicated_allocate_info);
    allocation_info.allocationSize = image_memory_requirements.size;
    allocation_info.memoryTypeIndex =
        escher::CountTrailingZeros(image_memory_requirements.memoryTypeBits);

    vk::DeviceMemory vk_memory;
    {
      auto result = vk_device.allocateMemory(allocation_info);
      FX_CHECK(result.result == vk::Result::eSuccess);
      vk_memory = result.value;
    }

    {
      auto result = vk_device.bindBufferMemory(scratch_buffer_, vk_memory, 0);
      FX_CHECK(result == vk::Result::eSuccess);
    }

    auto image_gpu_mem =
        escher::GpuMem::AdoptVkMemory(vk_device, vk_memory, allocation_info.allocationSize, false);

    escher::ImageInfo image_info;
    image_info.format = image_create_info.format;
    image_info.width = image_create_info.extent.width;
    image_info.height = image_create_info.extent.height;
    image_info.usage = image_create_info.usage;
    image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
    image_info.is_external = false;

    scratch_image_ = escher::impl::NaiveImage::AdoptVkImage(
        escher_->resource_recycler(), image_info, vk_image, std::move(image_gpu_mem),
        image_create_info.initialLayout);
  }

  //
  // Compile compute shaders and create pipelines.
  //

  pipeline_layout_ = CreatePipelineLayout(escher_.get(), descriptor_set_pool_.layout(),
                                          GetPushConstantBlockSize(modifier_));

  pipeline_ = CreatePipeline(escher_.get(), GetShaderSrc(modifier_), pipeline_layout_);
}

ComputeView::~ComputeView() {
  auto vk_device = escher_->vulkan_context().device;
  vk_device.destroyBuffer(scratch_buffer_);
  vk_device.destroyPipeline(pipeline_);
  vk_device.destroyPipelineLayout(pipeline_layout_);
}

void ComputeView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  uint32_t frame_number = GetNextFrameNumber();
  if (frame_number < paint_count_) {
    auto& image = images_[GetNextImageIndex()];
    zx::event acquire_fence(DuplicateEvent(image.acquire_fence));
    zx::event release_fence(DuplicateEvent(image.release_fence));
    FX_CHECK(acquire_fence && release_fence);
    session()->EnqueueAcquireFence(std::move(acquire_fence));
    session()->EnqueueReleaseFence(std::move(release_fence));
    if (png_fp_) {
      png_infop info_ptr;
      auto png = CreatePngReadStruct(png_fp_, &info_ptr);
      RenderFrameFromPng(image, png, frame_number);
      DestroyPngReadStruct(png, info_ptr);
    } else {
      RenderFrameFromColorOffset(image, GetNextColorOffset(), frame_number);
    }
    material_.SetTexture(image.image_id);
  }

  Animate(presentation_info);

  // The rectangle is constantly animating; invoke InvalidateScene() to guarantee
  // that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

void ComputeView::RenderFrameFromColorOffset(const Image& image, uint32_t color_offset,
                                             uint32_t frame_number) {
  auto frame =
      escher_->NewFrame("Compute Renderer", frame_number,
                        /* enable_gpu_logging */ false, escher::CommandBuffer::Type::kCompute,
                        /* use_protected_memory */ false);
  auto command_buffer = frame->cmds()->impl();
  auto vk_command_buffer = frame->vk_command_buffer();
  auto vk_device = escher_->vulkan_context().device;

  command_buffer->AddWaitSemaphore(image.release_semaphore, vk::PipelineStageFlagBits::eTopOfPipe);
  if (image.texture->image()->layout() != vk::ImageLayout::eGeneral) {
    command_buffer->TransitionImageLayout(image.texture->image(), image.texture->image()->layout(),
                                          vk::ImageLayout::eGeneral);
  }

  auto descriptor_set = descriptor_set_pool_.Allocate(1, frame->cmds()->impl())->get(0);

  vk::DescriptorImageInfo image_info;
  image_info.sampler = image.texture->sampler()->vk();
  image_info.imageView = image.texture->vk_image_view();
  image_info.imageLayout = vk::ImageLayout::eGeneral;

  switch (modifier_) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER: {
      vk::DescriptorBufferInfo buffer_info;
      buffer_info.buffer = image.buffer->vk();
      buffer_info.offset = 0;
      buffer_info.range = image.buffer->size();
      vk::WriteDescriptorSet write_descriptor_sets[2];
      write_descriptor_sets[0].dstSet = descriptor_set;
      write_descriptor_sets[0].dstBinding = 0;
      write_descriptor_sets[0].dstArrayElement = 0;
      write_descriptor_sets[0].descriptorCount = 1;
      write_descriptor_sets[0].descriptorType = vk::DescriptorType::eStorageImage;
      write_descriptor_sets[0].pImageInfo = &image_info;
      write_descriptor_sets[1].dstSet = descriptor_set;
      write_descriptor_sets[1].dstBinding = 1;
      write_descriptor_sets[1].dstArrayElement = 0;
      write_descriptor_sets[1].descriptorCount = 1;
      write_descriptor_sets[1].descriptorType = vk::DescriptorType::eStorageBuffer;
      write_descriptor_sets[1].pBufferInfo = &buffer_info;
      vk_device.updateDescriptorSets(countof(write_descriptor_sets), write_descriptor_sets, 0,
                                     nullptr);
    } break;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR: {
      vk::WriteDescriptorSet write_descriptor_sets[1];
      write_descriptor_sets[0].dstSet = descriptor_set;
      write_descriptor_sets[0].dstBinding = 0;
      write_descriptor_sets[0].dstArrayElement = 0;
      write_descriptor_sets[0].descriptorCount = 1;
      write_descriptor_sets[0].descriptorType = vk::DescriptorType::eStorageImage;
      write_descriptor_sets[0].pImageInfo = &image_info;
      vk_device.updateDescriptorSets(countof(write_descriptor_sets), write_descriptor_sets, 0,
                                     nullptr);
    } break;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }

  vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline_);
  vk_command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout_, 0, 1,
                                       &descriptor_set, 0, nullptr);

  switch (modifier_) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER: {
      AfbcPushConstantBlock push_constants;
      push_constants.color_offset = color_offset;
      push_constants.base_y = image.base_y;
      push_constants.width_in_tiles = image.width_in_tiles;
      vk_command_buffer.pushConstants(pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0,
                                      sizeof(push_constants), &push_constants);
      vk_command_buffer.dispatch(image.width_in_tiles, image.height_in_tiles, 1);
    } break;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR: {
      LinearPushConstantBlock push_constants;
      push_constants.color_offset = color_offset;
      vk_command_buffer.pushConstants(pipeline_layout_, vk::ShaderStageFlagBits::eCompute, 0,
                                      sizeof(push_constants), &push_constants);
      vk_command_buffer.dispatch(width_, height_, 1);
    } break;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }

  frame->EndFrame(image.acquire_semaphore, nullptr);
}

void ComputeView::RenderFrameFromPng(Image& image, png_structp png, uint32_t frame_number) {
  auto frame =
      escher_->NewFrame("Compute Renderer", frame_number,
                        /* enable_gpu_logging */ false, escher::CommandBuffer::Type::kCompute,
                        /* use_protected_memory */ false);
  auto command_buffer = frame->cmds()->impl();
  auto vk_command_buffer = frame->vk_command_buffer();
  auto vk_device = escher_->vulkan_context().device;
  auto vk_loader = escher_->device()->dispatch_loader();
  uint32_t stride = width_ * 4;

  // Create host buffer for transfer if it doesn't exist.
  if (!image.host_buffer) {
    image.host_buffer = escher_->buffer_cache()->NewHostBuffer(height_ * stride);
    FX_CHECK(image.host_buffer);
  }

  row_pointers_.clear();
  for (uint32_t y = 0; y < height_; ++y) {
    row_pointers_.push_back(reinterpret_cast<png_bytep>(image.host_buffer->host_ptr()) +
                            y * stride);
  }

  {
    TRACE_DURATION("gfx", "ComputeView::RenderFrameFromPng::ReadImage");
    png_read_image(png, row_pointers_.data());
  }

  command_buffer->AddWaitSemaphore(image.release_semaphore, vk::PipelineStageFlagBits::eTopOfPipe);

  switch (modifier_) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER: {
      // Create auxiliary buffer if it doesn't exist, or insert pipeline barrier.
      if (!image.aux_buffer) {
        // 4 bytes for compact image size.
        uint32_t aux_buffer_size = 4;
        image.aux_buffer = escher_->gpu_allocator()->AllocateBuffer(
            escher_->resource_recycler(), aux_buffer_size,
            vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlags());
      }

      if (scratch_image_->layout() != vk::ImageLayout::eTransferDstOptimal) {
        command_buffer->TransitionImageLayout(scratch_image_, scratch_image_->layout(),
                                              vk::ImageLayout::eTransferDstOptimal);
      }

      vk::ImageSubresourceLayers subresource;
      subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      subresource.mipLevel = 0;
      subresource.baseArrayLayer = 0;
      subresource.layerCount = 1;

      vk::BufferImageCopy region;
      region.bufferOffset = 0;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource = subresource;
      region.imageOffset.x = 0;
      region.imageOffset.y = 0;
      region.imageOffset.z = 0;
      region.imageExtent.width = width_;
      region.imageExtent.height = height_;
      region.imageExtent.depth = 1;
      vk_command_buffer.copyBufferToImage(image.host_buffer->vk(), scratch_image_->vk(),
                                          vk::ImageLayout::eTransferDstOptimal, 1, &region);

      // Transition to compact image layout.
      command_buffer->TransitionImageLayout(scratch_image_, scratch_image_->layout(),
                                            vk::ImageLayout::eTransferSrcOptimal);

      // Write compact image size to aux buffer.
      vk_command_buffer.writeCompactImageMemorySizeFUCHSIA(
          scratch_image_->vk(), vk::ImageLayout::eTransferSrcOptimal, image.aux_buffer->vk(), 0,
          &subresource, vk_loader);

      if (image.texture->image()->layout() != vk::ImageLayout::eGeneral) {
        command_buffer->TransitionImageLayout(
            image.texture->image(), image.texture->image()->layout(), vk::ImageLayout::eGeneral);
      }

      // Copy compact image data to texture.
      // TODO(reveman): Improve the performnce of this by using a compute
      // shader to only copy the compact image size.
      {
        vk::BufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = image.buffer->size();
        vk_command_buffer.copyBuffer(scratch_buffer_, image.buffer->vk(), 1, &region);
      }
    } break;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR: {
      if (image.texture->image()->layout() != vk::ImageLayout::eTransferDstOptimal) {
        command_buffer->TransitionImageLayout(image.texture->image(),
                                              image.texture->image()->layout(),
                                              vk::ImageLayout::eTransferDstOptimal);
      }

      vk::BufferImageCopy region;
      region.bufferOffset = 0;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      region.imageSubresource.mipLevel = 0;
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 1;
      region.imageOffset.x = 0;
      region.imageOffset.y = 0;
      region.imageOffset.z = 0;
      region.imageExtent.width = width_;
      region.imageExtent.height = height_;
      region.imageExtent.depth = 1;
      vk_command_buffer.copyBufferToImage(image.host_buffer->vk(), image.texture->image()->vk(),
                                          vk::ImageLayout::eTransferDstOptimal, 1, &region);
    } break;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }

  frame->EndFrame(image.acquire_semaphore, nullptr);
}

fit::promise<inspect::Inspector> ComputeView::PopulateStats() const {
  inspect::Inspector inspector;

  inspector.GetRoot().CreateUint(kModifier, modifier_, &inspector);

  return fit::make_ok_promise(std::move(inspector));
}

fit::promise<inspect::Inspector> ComputeView::PopulateImageStats(const Image& image) {
  inspect::Inspector inspector;

  inspector.GetRoot().CreateUint(kImageBytes, image.buffer->size(), &inspector);

  if (image.aux_buffer) {
    auto frame =
        escher_->NewFrame("Aux Readback", 0,
                          /* enable_gpu_logging */ false, escher::CommandBuffer::Type::kCompute,
                          /* use_protected_memory */ false);
    auto vk_command_buffer = frame->vk_command_buffer();

    auto buffer = escher_->buffer_cache()->NewHostBuffer(image.aux_buffer->size());
    FX_CHECK(buffer);

    vk::BufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = image.aux_buffer->size();
    vk_command_buffer.copyBuffer(image.aux_buffer->vk(), buffer->vk(), 1, &region);
    frame->EndFrame(escher::SemaphorePtr(), nullptr);
    escher_->vk_device().waitIdle();

    uint32_t image_size = reinterpret_cast<uint32_t*>(buffer->host_ptr())[0];
    inspector.GetRoot().CreateUint(kImageBytesUsed, image_size, &inspector);
  } else if (image.host_buffer) {
    inspector.GetRoot().CreateUint(kImageBytesUsed, image.buffer->size(), &inspector);
  } else {
    inspector.GetRoot().CreateUint(kImageBytesUsed, 0, &inspector);
  }

  if (modifier_ == fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16_YUV_TILED_HEADER) {
    inspector.GetRoot().CreateUint(kWidthInTiles, image.width_in_tiles, &inspector);
    inspector.GetRoot().CreateUint(kHeightInTiles, image.height_in_tiles, &inspector);
  }

  return fit::make_ok_promise(std::move(inspector));
}

}  // namespace frame_compression
