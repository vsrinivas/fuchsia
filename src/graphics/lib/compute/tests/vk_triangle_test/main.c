// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include "tests/common/vk_app_state.h"
#include "tests/common/vk_swapchain.h"
#include "tests/common/vk_swapchain_queue.h"
#include "tests/common/vk_utils.h"

// Include the auto-generated constant arrays containing our compiled shaders.
#include "triangle_shaders.h"

//
// A test that displays a gradiant shaded triangle in a window.
// Used to exercise vk_app_state_t presentation and basic event loop.
// Also how to use the graphics pipeline with a simple render pass.
// NOTE: shaders hard-code everything, so no descriptor sets are used.
//

VkRenderPass
create_render_pass(VkDevice                      device,
                   const VkAllocationCallbacks * allocator,
                   VkFormat                      surface_format)
{
  const VkAttachmentDescription colorAttachment = {
    .format         = surface_format,
    .samples        = VK_SAMPLE_COUNT_1_BIT,
    .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  const VkAttachmentReference colorAttachmentRef = {
    .attachment = 0,
    .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };

  const VkSubpassDescription subpass = {
    .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    .pColorAttachments    = &colorAttachmentRef,
  };

  const VkSubpassDependency dependency = {
    .srcSubpass    = VK_SUBPASS_EXTERNAL,
    .dstSubpass    = 0,
    .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };

  const VkRenderPassCreateInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments    = &colorAttachment,
    .subpassCount    = 1,
    .pSubpasses      = &subpass,
    .dependencyCount = 1,
    .pDependencies   = &dependency,
  };

  VkRenderPass render_pass;
  vk(CreateRenderPass(device, &renderPassInfo, allocator, &render_pass));
  return render_pass;
}

//
//
//

static VkPipelineLayout
create_pipeline_layout(VkDevice device, const VkAllocationCallbacks * allocator)
{
  // Empty pipeline layout, since we don't pass uniform to shaders for now.
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };

  VkPipelineLayout layout;
  vk(CreatePipelineLayout(device, &pipelineLayoutInfo, allocator, &layout));
  return layout;
}

static VkPipeline
create_graphics_pipeline(VkDevice                      device,
                         const VkAllocationCallbacks * allocator,
                         VkExtent2D                    extent,
                         VkRenderPass                  render_pass,
                         VkPipelineLayout              pipeline_layout)
{
  // Create shader modules.
  VkShaderModule vertexShader;
  VkShaderModule fragmentShader;

  {
    VkShaderModuleCreateInfo createInfo = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(triangle_vert_data),
      .pCode    = triangle_vert_data,
    };
    vk(CreateShaderModule(device, &createInfo, allocator, &vertexShader));
  }

  {
    VkShaderModuleCreateInfo createInfo = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(triangle_frag_data),
      .pCode    = triangle_frag_data,
    };
    vk(CreateShaderModule(device, &createInfo, allocator, &fragmentShader));
  }

  // Describe how they're going to be used by the graphics pipeline.
  const VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage  = VK_SHADER_STAGE_VERTEX_BIT,
    .module = vertexShader,
    .pName  = "main",
  };

  const VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
    .module = fragmentShader,
    .pName  = "main",
  };

  const VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo,
                                                           fragShaderStageInfo };

  // Format of the vertex data passed to the vertex shader.
  const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  // What kind of primitives are being drawn.
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
  };

  // Setup viewport and scissor to draw on the full window.
  const VkViewport viewport = {
    .x        = 0.0f,
    .y        = 0.0f,
    .width    = (float)extent.width,
    .height   = (float)extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  const VkRect2D scissor = {
    .offset = { 0, 0 },
    .extent = extent,
  };

  const VkPipelineViewportStateCreateInfo viewportState = {
    .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports    = &viewport,
    .scissorCount  = 1,
    .pScissors     = &scissor,
  };

  // Rasterizer setup.
  const VkPipelineRasterizationStateCreateInfo rasterizer = {
    .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable        = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode             = VK_POLYGON_MODE_FILL,
    .lineWidth               = 1.0f,
    .cullMode                = VK_CULL_MODE_BACK_BIT,
    .frontFace               = VK_FRONT_FACE_CLOCKWISE,
    .depthBiasEnable         = VK_FALSE,
  };

  // No need for multisampling for now.
  const VkPipelineMultisampleStateCreateInfo multisampling = {
    .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .sampleShadingEnable  = VK_FALSE,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  // Color blending.
  const VkPipelineColorBlendAttachmentState colorBlendAttachment = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    .blendEnable = VK_FALSE,
  };

  const VkPipelineColorBlendStateCreateInfo colorBlending = {
    .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable   = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments    = &colorBlendAttachment,
  };

  // Finally, create the final pipeline.
  const VkGraphicsPipelineCreateInfo pipelineInfo = {
    .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount          = 2,
    .pStages             = shaderStages,
    .pVertexInputState   = &vertexInputInfo,
    .pInputAssemblyState = &inputAssembly,
    .pViewportState      = &viewportState,
    .pRasterizationState = &rasterizer,
    .pMultisampleState   = &multisampling,
    .pDepthStencilState  = NULL,
    .pColorBlendState    = &colorBlending,
    .pDynamicState       = NULL,
    .layout              = pipeline_layout,
    .renderPass          = render_pass,
    .subpass             = 0,
    .basePipelineHandle  = VK_NULL_HANDLE,
    .basePipelineIndex   = -1,
  };

  VkPipeline pipeline;
  vk(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, allocator, &pipeline));

  // No need for these anymore.
  vkDestroyShaderModule(device, vertexShader, allocator);
  vkDestroyShaderModule(device, fragmentShader, allocator);

  return pipeline;
}

//
//
//

//
//
//

int
main(int argc, char const * argv[])
{
  vk_app_state_config_t app_config = {
    .app_name              = "vk_triangle_test",
    .enable_validation     = true,
    .enable_pipeline_cache = true,
    .enable_debug_report   = true,
    .enable_amd_statistics = true,

    .device_config = {
      .required_queues = VK_QUEUE_GRAPHICS_BIT,
      .vendor_id       = (argc <= 2) ? 0 : strtoul(argv[1], NULL, 16),
      .device_id       = (argc <= 3) ? 0 : strtoul(argv[2], NULL, 16),
    },

    .require_swapchain      = true,
  };

  vk_app_state_t app_state = {};

  if (!vk_app_state_init(&app_state, &app_config))
    {
      fprintf(stderr, "FAILURE\n");
      return EXIT_FAILURE;
    }

  vk_app_state_print(&app_state);

  vk_swapchain_t * swapchain = vk_swapchain_create(&(const vk_swapchain_config_t){
    .instance        = app_state.instance,
    .device          = app_state.d,
    .physical_device = app_state.pd,
    .allocator       = app_state.ac,

    .present_queue_family  = app_state.qfi,
    .present_queue_index   = 0,
    .graphics_queue_family = app_state.qfi,
    .graphics_queue_index  = 0,

    .surface_khr = vk_app_state_create_surface(&app_state, 800, 600),
    .max_frames  = 2,
  });

  VkDevice                      device         = app_state.d;
  const VkAllocationCallbacks * allocator      = app_state.ac;
  VkExtent2D                    surface_extent = vk_swapchain_get_extent(swapchain);
  VkFormat                      surface_format = vk_swapchain_get_format(swapchain).format;

  VkRenderPass     render_pass     = create_render_pass(device, allocator, surface_format);
  VkPipelineLayout pipeline_layout = create_pipeline_layout(device, allocator);

  VkPipeline graphics_pipeline =
    create_graphics_pipeline(device, allocator, surface_extent, render_pass, pipeline_layout);

  // Setup command buffers and framebuffers for this application.

  vk_swapchain_queue_t * swapchain_queue =
    vk_swapchain_queue_create(&(const vk_swapchain_queue_config_t){
      .swapchain    = swapchain,
      .queue_family = app_state.qfi,
      .queue_index  = 0,
      .device       = app_state.d,
      .allocator    = app_state.ac,

      .enable_framebuffers = render_pass,
    });

  uint32_t image_count = vk_swapchain_get_image_count(swapchain);

  for (uint32_t nn = 0; nn < image_count; ++nn)
    {
      const vk_swapchain_queue_image_t * image  = vk_swapchain_queue_get_image(swapchain_queue, nn);
      VkCommandBuffer                    buffer = image->command_buffer;

      const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
      };
      vk(BeginCommandBuffer(buffer, &beginInfo));

      const VkRenderPassBeginInfo renderPassInfo = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = render_pass,
        .framebuffer = image->framebuffer,
        .renderArea = {
            .offset = {0, 0},
            .extent = surface_extent,
        },
        .clearValueCount = 1,
        .pClearValues = &(const VkClearValue){.color = {{0.0f, 0.0f, 0.0f, 1.0f}}},
      };
      vkCmdBeginRenderPass(buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
      {
        vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        vkCmdDraw(buffer, 3, 1, 0, 0);
      }
      vkCmdEndRenderPass(buffer);
      vk(EndCommandBuffer(buffer));
    }

  // Main loop.
  uint32_t counter = 0;

  while (vk_app_state_poll_events(&app_state))
    {
      if (!vk_swapchain_queue_acquire_next_image(swapchain_queue))
        {
          // Window was resized! For now just exit!!
          // TODO(digit): Handle resize!!
          break;
        }

      // Nothing to do here, just submit the current image's command buffer!
      vk_swapchain_queue_submit_and_present_image(swapchain_queue);

      // Print a small tick every two seconds (assuming a 60hz swapchain) to
      // check that everything is working, even if the image is static at this
      // point.
      if (++counter == 60 * 2)
        {
          printf("!");
          fflush(stdout);
          counter = 0;
        }
    }

  //printf("FINAL_WAIT_IDLE!\n");
  vkDeviceWaitIdle(device);

  //printf("DONE!\n");

  //
  // Dispose of Vulkan resources
  //

  vk_swapchain_queue_destroy(swapchain_queue);
  vk_swapchain_destroy(swapchain);

  vkDestroyPipeline(device, graphics_pipeline, allocator);
  vkDestroyPipelineLayout(device, pipeline_layout, allocator);
  vkDestroyRenderPass(device, render_pass, allocator);

  vk_app_state_destroy(&app_state);
  printf("DONE\n");

  return EXIT_SUCCESS;
}

//
//
//
