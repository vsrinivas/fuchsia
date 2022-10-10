// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO(fxbug.dev/104743): when this is upstreamed into ash, this should say `crate::*` instead
    // of `ash::*`.
    ash::{prelude::*, vk, Device, Instance, RawPtr},
    std::mem,
};

#[derive(Clone)]
pub struct BufferCollection {
    handle: vk::Device,
    fp: vk::FuchsiaBufferCollectionFn,
}

impl BufferCollection {
    pub fn new(instance: &Instance, device: &Device) -> Self {
        let handle = device.handle();
        let fp = vk::FuchsiaBufferCollectionFn::load(|name| unsafe {
            mem::transmute(instance.get_device_proc_addr(handle, name.as_ptr()))
        });
        Self { handle, fp }
    }

    /// <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkCreateBufferCollectionFUCHSIA.html>
    pub unsafe fn create_buffer_collection(
        &self,
        create_info: &vk::BufferCollectionCreateInfoFUCHSIA,
        allocation_callbacks: Option<&vk::AllocationCallbacks>,
    ) -> VkResult<vk::BufferCollectionFUCHSIA> {
        let mut buffer_collection = mem::zeroed();
        (self.fp.create_buffer_collection_fuchsia)(
            self.handle,
            create_info,
            allocation_callbacks.as_raw_ptr(),
            &mut buffer_collection,
        )
        .result_with_success(buffer_collection)
    }

    /// <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkSetBufferCollectionImageConstraintsFUCHSIA.html>
    pub unsafe fn set_buffer_collection_image_constraints(
        &self,
        collection: vk::BufferCollectionFUCHSIA,
        info: &vk::ImageConstraintsInfoFUCHSIA,
    ) -> VkResult<()> {
        (self.fp.set_buffer_collection_image_constraints_fuchsia)(
            self.handle,
            collection,
            info as *const vk::ImageConstraintsInfoFUCHSIA,
        )
        .result_with_success(())
    }

    /// <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkSetBufferCollectionBufferConstraintsFUCHSIA.html>
    // TODO(fxbug.dev/104743): remove this #[allow(dead_code)] when we subsequently upstream
    // this extension to ash.
    #[allow(dead_code)]
    pub unsafe fn set_buffer_collection_buffer_constraints(
        &self,
        collection: vk::BufferCollectionFUCHSIA,
        info: &vk::BufferConstraintsInfoFUCHSIA,
    ) -> VkResult<()> {
        (self.fp.set_buffer_collection_buffer_constraints_fuchsia)(
            self.handle,
            collection,
            info as *const vk::BufferConstraintsInfoFUCHSIA,
        )
        .result_with_success(())
    }

    /// <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkDestroyBufferCollectionFUCHSIA.html>
    #[allow(dead_code)]
    pub unsafe fn destroy_buffer_collection(
        &self,
        collection: vk::BufferCollectionFUCHSIA,
        allocation_callbacks: Option<&vk::AllocationCallbacks>,
    ) {
        (self.fp.destroy_buffer_collection_fuchsia)(
            self.handle,
            collection,
            allocation_callbacks.as_raw_ptr(),
        );
    }

    /// <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkGetBufferCollectionPropertiesFUCHSIA.html>
    pub unsafe fn get_buffer_collection_properties(
        &self,
        collection: vk::BufferCollectionFUCHSIA,
    ) -> VkResult<vk::BufferCollectionPropertiesFUCHSIA> {
        let mut props = vk::BufferCollectionPropertiesFUCHSIA { ..Default::default() };
        (self.fp.get_buffer_collection_properties_fuchsia)(self.handle, collection, &mut props)
            .result_with_success(props)
    }
}
