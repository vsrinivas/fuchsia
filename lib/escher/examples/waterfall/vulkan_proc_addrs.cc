// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vulkan_proc_addrs.h"

#include "ftl/logging.h"

namespace {

template<typename FuncT>
inline FuncT GetInstanceProcAddr(
    vk::Instance inst, const char* func_name) {
  FuncT func = reinterpret_cast<FuncT>(inst.getProcAddr(func_name));
  FTL_CHECK(func);
  return func;
}

template<typename FuncT>
inline FuncT GetDeviceProcAddr(
    vk::Device device, const char* func_name) {
  FuncT func = reinterpret_cast<FuncT>(device.getProcAddr(func_name));
  FTL_CHECK(func);
  return func;
}

#define GET_INSTANCE_PROC_ADDR(XXX) XXX = GetInstanceProcAddr<PFN_vk##XXX>(instance, "vk" #XXX)
#define GET_DEVICE_PROC_ADDR(XXX) XXX = GetDeviceProcAddr<PFN_vk##XXX>(device, "vk" #XXX)

}  // anonymous namespace

InstanceProcAddrs::InstanceProcAddrs() :
    CreateDebugReportCallbackEXT(nullptr),
    DestroyDebugReportCallbackEXT(nullptr),
    GetPhysicalDeviceSurfaceSupportKHR(nullptr) {}

InstanceProcAddrs::InstanceProcAddrs(vk::Instance instance) {
  GET_INSTANCE_PROC_ADDR(CreateDebugReportCallbackEXT);
  GET_INSTANCE_PROC_ADDR(DestroyDebugReportCallbackEXT);
  GET_INSTANCE_PROC_ADDR(GetPhysicalDeviceSurfaceSupportKHR);
}

DeviceProcAddrs::DeviceProcAddrs() :
    CreateSwapchainKHR(nullptr),
    DestroySwapchainKHR(nullptr),
    GetSwapchainImagesKHR(nullptr),
    AcquireNextImageKHR(nullptr),
    QueuePresentKHR(nullptr) {}

DeviceProcAddrs::DeviceProcAddrs(vk::Device device) {
  GET_DEVICE_PROC_ADDR(CreateSwapchainKHR);
  GET_DEVICE_PROC_ADDR(DestroySwapchainKHR);
  GET_DEVICE_PROC_ADDR(GetSwapchainImagesKHR);
  GET_DEVICE_PROC_ADDR(AcquireNextImageKHR);
  GET_DEVICE_PROC_ADDR(QueuePresentKHR);
}
