#!/boot/bin/sh

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
common='/data/vulkancts/executor -s /data/vulkancts/execserver -b /data/vulkancts/deqp-vk --caselistdir=/data/vulkancts'

$common --testset=dEQP-VK.info.*
$common --testset=dEQP-VK.api.smoke.*
$common --testset=dEQP-VK.api.info.*
$common --testset=dEQP-VK.api.device_init.*
$common --testset=dEQP-VK.api.object_management.*
$common --testset=dEQP-VK.api.buffer.*
$common --testset=dEQP-VK.api.buffer_view.*
$common --testset=dEQP-VK.api.command_buffers.*
$common --testset=dEQP-VK.api.copy_and_blit.*
$common --testset=dEQP-VK.api.image_clearing.*
$common --testset=dEQP-VK.api.fill_and_update_buffer.*
$common --testset=dEQP-VK.api.descriptor_pool.*
$common --testset=dEQP-VK.memory.*
$common --testset=dEQP-VK.pipeline.*
$common --testset=dEQP-VK.binding_model.*
$common --testset=dEQP-VK.spirv_assembly.*
$common --testset=dEQP-VK.glsl.*
$common --testset=dEQP-VK.renderpass.*
$common --testset=dEQP-VK.ubo.*
$common --testset=dEQP-VK.ssbo.*
$common --testset=dEQP-VK.query_pool.*
$common --testset=dEQP-VK.draw.*
$common --testset=dEQP-VK.compute.*
$common --testset=dEQP-VK.image.*
$common --testset=dEQP-VK.wsi.*
$common --testset=dEQP-VK.synchronization.*
$common --testset=dEQP-VK.sparse_resources.*
$common --testset=dEQP-VK.tessellation.*
$common --testset=dEQP-VK.rasterization.*
$common --testset=dEQP-VK.clipping.*
