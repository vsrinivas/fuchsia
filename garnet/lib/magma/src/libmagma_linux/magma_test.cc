// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is intended to be run manually from within biscotti_guest.

#include "magma.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("Check Failed (%s): \"%s\"\n", #x, strerror(errno));                            \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

// TODO(MA-520): this should be part of vm initialization, not a magma API
extern bool magma_write_driver_to_filesystem(int32_t file_descriptor);

PFN_vkVoidFunction get_vkCreateInstance(void* driver)
{
    // This method emulates a small part of the initialization logic in the Vulkan loader.

    static const char* vulkan_main_entrypoint = "vk_icdGetInstanceProcAddr";
    printf("dlsym for Address of Symbol %s\n", vulkan_main_entrypoint);
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(driver, vulkan_main_entrypoint);
    CHECK(vkGetInstanceProcAddr != nullptr);
    printf("Address Acquired\n");

    static const char* vulkan_instance_entrypoint = "vkCreateInstance";
    printf("vkGetInstanceProcAddr for Address of Entrypoint %s\n", vulkan_instance_entrypoint);
    PFN_vkVoidFunction addr_vkCreateInstance =
        vkGetInstanceProcAddr(nullptr, vulkan_instance_entrypoint);
    CHECK(addr_vkCreateInstance != nullptr);
    printf("Address Acquired\n");

    return addr_vkCreateInstance;
}

int main(int argc, char* argv[])
{
    static const char* device_path = "/dev/wl0";
    printf("Open Device %s\n", device_path);
    int fd = open(device_path, O_NONBLOCK);
    CHECK(fd != -1);
    printf("Device Opened\n");

    uint64_t device_id = 0;
    printf("Query Device ID 0x%08X\n", MAGMA_QUERY_DEVICE_ID);
    magma_status_t status = magma_query(fd, MAGMA_QUERY_DEVICE_ID, &device_id);
    CHECK(status == MAGMA_STATUS_OK);
    printf("Device ID: 0x%016lX\n", device_id);

    printf("Create Connection\n");
    auto connection = magma_create_connection(fd, 0);
    CHECK(connection != nullptr);
    printf("Connection Created\n");

    printf("Release Connection\n");
    magma_release_connection(connection);
    printf("Connection Released\n");

    printf("Write Driver to FS\n");
    bool ok = magma_write_driver_to_filesystem(fd);
    CHECK(ok);
    printf("Driver Written to FS\n");

    static const char* driver_path = "/libvulkan_magma.so";
    printf("Load Driver %s\n", driver_path);
    void* driver = dlopen(driver_path, RTLD_NOW);
    CHECK(driver != nullptr);
    printf("Driver Loaded\n");

    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)get_vkCreateInstance(driver);

    printf("Creating Vulkan Instance\n");
    VkApplicationInfo application_info{};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "magma_test";
    application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.pEngineName = "no-engine";
    application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application_info.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_create_info{};
    instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.pApplicationInfo = &application_info;
    VkInstance instance{};
    VkResult result = vkCreateInstance(&instance_create_info, nullptr, &instance);
    CHECK(result == VK_SUCCESS);
    printf("Vulkan Instance Created\n");

    return 0;
}
