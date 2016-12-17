#!/boot/bin/sh

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
common='/data/vulkancts/executor -s /data/vulkancts/execserver -b /data/vulkancts/deqp-vk --caselistdir=/data/vulkancts'

command_buffers_blacklist="dEQP-VK.api.command_buffers.record_simul_use_primary,dEQP-VK.api.command_buffers.record_simul_use_secondary,dEQP-VK.api.command_buffers.secondary_execute_twice"
image_clearing_blacklist="dEQP-VK.api.image_clearing.clear_color_image.2d_r8g8b8_unorm,dEQP-VK.api.image_clearing.clear_color_image.2d_r16g16b16_unorm,dEQP-VK.api.image_clearing.clear_color_image.2d_e5b9g9r9_ufloat_pack32,dEQP-VK.api.image_clearing.clear_color_image.3d_r8g8b8_unorm,dEQP-VK.api.image_clearing.clear_color_image.3d_r16g16b16_unorm,dEQP-VK.api.image_clearing.clear_color_image.3d_e5b9g9r9_ufloat_pack32"
memory_blacklist="dEQP-VK.memory.pipeline_barrier.all.1048576"
spirv_assembly_blacklist="dEQP-VK.spirv_assembly.instruction.compute.opatomic.iadd,dEQP-VK.spirv_assembly.instruction.compute.opatomic.iinc,dEQP-VK.spirv_assembly.instruction.compute.opatomic.isub,dEQP-VK.spirv_assembly.instruction.compute.opatomic.idec,dEQP-VK.spirv_assembly.instruction.compute.opatomic.compex"
synchronization_blacklist="dEQP-VK.synchronization.smoke.events,dEQP-VK.synchronization.basic.event.host_set_device_wait"

$common --testset=dEQP-VK.info.*
$common --testset=dEQP-VK.api.smoke.*
$common --testset=dEQP-VK.api.info.*
$common --testset=dEQP-VK.api.device_init.*
$common --testset=dEQP-VK.api.object_management.*
$common --testset=dEQP-VK.api.buffer.*
$common --testset=dEQP-VK.api.buffer_view.*
$common --testset=dEQP-VK.api.command_buffers.* --exclude=$command_buffers_blacklist
$common --testset=dEQP-VK.api.copy_and_blit.*
$common --testset=dEQP-VK.api.image_clearing.* --exclude=$image_clearing_blacklist
$common --testset=dEQP-VK.api.fill_and_update_buffer.*
$common --testset=dEQP-VK.api.descriptor_pool.*
$common --testset=dEQP-VK.memory.* --exclude=$memory_blacklist
$common --testset=dEQP-VK.pipeline.*
$common --testset=dEQP-VK.binding_model.*
$common --testset=dEQP-VK.spirv_assembly.* --exclude=$spirv_assembly_blacklist
$common --testset=dEQP-VK.glsl.*
$common --testset=dEQP-VK.renderpass.*
$common --testset=dEQP-VK.ubo.*
$common --testset=dEQP-VK.ssbo.*
$common --testset=dEQP-VK.querypool.*
$common --testset=dEQP-VK.draw.*
$common --testset=dEQP-VK.compute.*
$common --testset=dEQP-VK.image.*
$common --testset=dEQP-VK.wsi.*
$common --testset=dEQP-VK.synchronization.* --exclude=$synchronization_blacklist
$common --testset=dEQP-VK.sparse_resources.*
$common --testset=dEQP-VK.tessellation.*
$common --testset=dEQP-VK.rasterization.*
$common --testset=dEQP-VK.clipping.*
