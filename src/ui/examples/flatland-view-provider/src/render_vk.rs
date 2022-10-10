// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ash_extensions::fuchsia,
    crate::render::Renderer,
    anyhow::Error,
    ash::{extensions::ext, vk},
    fidl::endpoints::{create_endpoints, ClientEnd, Proxy},
    fidl_fuchsia_sysmem as fsysmem, fidl_fuchsia_ui_composition as fland,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_runtime,
    fuchsia_scenic::{
        duplicate_buffer_collection_import_token, duplicate_buffer_collection_token,
        BufferCollectionTokenPair,
    },
    fuchsia_zircon::{AsHandleRef, HandleBased},
    std::ffi::{c_char, c_void, CString},
};

// Constants
pub const APPLICATION_VERSION: u32 = vk::make_api_version(0, 1, 0, 0);
pub const ENGINE_VERSION: u32 = vk::make_api_version(0, 1, 0, 0);
pub const API_VERSION: u32 = vk::make_api_version(0, 1, 3, 92);
// SRGB is the only color space compatible with R8G8B8A8_UNORM.
pub const FRAMEBUFFER_FORMAT: vk::Format = vk::Format::R8G8B8A8_UNORM;
pub const FRAMEBUFFER_COLOR_SPACE: fsysmem::ColorSpaceType = fsysmem::ColorSpaceType::Srgb;

// Holds the state needed to implement the `crate::render::Renderer` trait.
pub struct VulkanRenderer {
    _entry: ash::Entry,
    instance: ash::Instance,
    device: ash::Device,
    queue: vk::Queue,
    queue_family_index: u32,
    command_pool: vk::CommandPool,
    command_buffers: Vec<vk::CommandBuffer>,
    framebuffers: Vec<ImageMemory>,
    fence: vk::Fence,
    import_token: fland::BufferCollectionImportToken,
}

// Encapsulates a `VkImage` and the `VkDeviceMemory` that it is bound to.  This makes it obvious to
// destroy them both at the same time.
struct ImageMemory {
    image: vk::Image,
    device_memory: vk::DeviceMemory,
}

// Represents a `VkPhysicalDevice` which has all of the necessary characteristics to support this
// application, and also a suitable queue family associated with the device.
struct SuitablePhysicalDevice {
    device: vk::PhysicalDevice,
    graphics_queue_family_index: u32,
}

impl SuitablePhysicalDevice {
    // Considers the candidate physical device.  If it meets all criteria, returns a fully-initialized
    // `SuitablePhysicalDevice`.  If it fails to meet even one of the criteria, return None.
    fn consider(
        instance: &ash::Instance,
        physical_device: vk::PhysicalDevice,
    ) -> Option<SuitablePhysicalDevice> {
        // Pick the first queue family which supports graphics.  Return None if none exists.
        let graphics_queue_family_index = {
            let queue_families =
                unsafe { instance.get_physical_device_queue_family_properties(physical_device) };

            let mut graphics_queue_family_index: Option<u32> = None;
            let mut index = 0;
            for family in queue_families.iter() {
                if family.queue_count > 0 && family.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
                    graphics_queue_family_index = Some(index);
                    break;
                }
                index += 1;
            }
            // If it's still `None` then the candidate device is unsuitable.
            graphics_queue_family_index?
        };

        // Check if the device supports the desired format, and bail if it isn't.
        // TODO(fxbug.dev/104692): instead of hardcoding a single format, we should filter a list of
        // acceptable formats, and only bail if none of them are supported.
        let format_properties = unsafe {
            instance.get_physical_device_format_properties(physical_device, FRAMEBUFFER_FORMAT)
        };
        if !format_properties.linear_tiling_features.contains(vk::FormatFeatureFlags::TRANSFER_SRC)
            || !format_properties
                .optimal_tiling_features
                .contains(vk::FormatFeatureFlags::TRANSFER_DST)
        {
            return None;
        }

        // More sophisticated apps might want to consider other criteria here.
        let _device_features = unsafe { instance.get_physical_device_features(physical_device) };

        // Success!  This physical device meets all criteria.
        Some(SuitablePhysicalDevice { device: physical_device, graphics_queue_family_index })
    }
}

impl Drop for VulkanRenderer {
    fn drop(&mut self) {
        let device = &self.device;
        unsafe {
            device.device_wait_idle().expect("failed to wait for idle device");
            for im in &self.framebuffers {
                device.destroy_image(im.image, None);
                device.free_memory(im.device_memory, None);
            }
            device.free_command_buffers(self.command_pool, self.command_buffers.as_slice());
            device.destroy_command_pool(self.command_pool, None);
            device.destroy_fence(self.fence, None);
            device.destroy_device(None);
            self.instance.destroy_instance(None);
        }
    }
}

impl Renderer for VulkanRenderer {
    fn duplicate_buffer_collection_import_token(&self) -> fland::BufferCollectionImportToken {
        duplicate_buffer_collection_import_token(&self.import_token).unwrap()
    }

    // TODO(fxbug.dev/104692): there are a number of shortcomings in the implementation of this
    // function; see the bug report for more details.  The main shortcoming is that this fills the
    // whole view with a single color, instead of filling each quadrant with a different color.
    // This is a temporary hack which avoids the need to create pipelines and draw geometry.
    // The "hacky" part is that we don't consider whether it is necessary to use semaphores.
    fn render_rgba(&self, buffer_index: usize, colors: [[u8; 4]; 4]) {
        let device = &self.device;
        let command_buffer = self.command_buffers[buffer_index];

        let command_buffer_begin_info = vk::CommandBufferBeginInfo {
            flags: vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT,
            ..Default::default()
        };
        unsafe {
            device
                .begin_command_buffer(command_buffer, &command_buffer_begin_info)
                .expect("failed to begin command buffer");
        }

        let first_color = &colors[0];
        let first_color = [
            first_color[0] as f32 / 255.0,
            first_color[1] as f32 / 255.0,
            first_color[2] as f32 / 255.0,
            first_color[3] as f32 / 255.0,
        ];
        let clear_color_value = vk::ClearColorValue { float32: first_color };
        let image = self.framebuffers[buffer_index].image;
        let image_barrier_for_clear = vk::ImageMemoryBarrier::builder()
            // empty() because it was last used by foreign queue (i.e. Scenic).
            .src_access_mask(vk::AccessFlags::empty())
            .dst_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            .new_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
            .src_queue_family_index(vk::QUEUE_FAMILY_FOREIGN_EXT)
            .dst_queue_family_index(self.queue_family_index)
            .image(image)
            .subresource_range(VulkanRenderer::standard_image_subresource_range())
            .build();
        let image_barrier_for_present = vk::ImageMemoryBarrier::builder()
            .src_access_mask(vk::AccessFlags::TRANSFER_WRITE)
            // empty() because next use will be by foreign queue (i.e. Scenic).
            .dst_access_mask(vk::AccessFlags::empty())
            .old_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL)
            .new_layout(vk::ImageLayout::GENERAL)
            .src_queue_family_index(self.queue_family_index)
            .dst_queue_family_index(vk::QUEUE_FAMILY_FOREIGN_EXT)
            .image(image)
            .subresource_range(VulkanRenderer::standard_image_subresource_range())
            .build();
        unsafe {
            device.cmd_pipeline_barrier(
                command_buffer,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[image_barrier_for_clear],
            );

            device.cmd_clear_color_image(
                command_buffer,
                image,
                vk::ImageLayout::TRANSFER_DST_OPTIMAL,
                &clear_color_value,
                &[VulkanRenderer::standard_image_subresource_range()],
            );

            device.cmd_pipeline_barrier(
                command_buffer,
                vk::PipelineStageFlags::TRANSFER,
                vk::PipelineStageFlags::TRANSFER,
                vk::DependencyFlags::empty(),
                &[],
                &[],
                &[image_barrier_for_present],
            );

            device.end_command_buffer(command_buffer).expect("failed to end command buffer");
        }

        // Submit the command buffer.
        // TODO(fxbug.dev/104692): we wait on the CPU for the fence to finish.  It would be more
        // optimal to pass a signal semaphore here, which corresponds to a `zx::event` which would
        // be passed to Flatland via `PresentArgs.server_wait_fences`.
        let submit_info = [vk::SubmitInfo::builder().command_buffers(&[command_buffer]).build()];
        unsafe {
            device
                .queue_submit(self.queue, &submit_info, self.fence)
                .expect("failed to submit command buffer");

            device
                .wait_for_fences(&[self.fence], false, 10000000000)
                .expect("failed to wait for fence in 10 seconds");

            device.reset_fences(&[self.fence]).expect("failed to reset fence");
        }
    }
}

impl VulkanRenderer {
    pub async fn new(width: u32, height: u32, image_count: usize) -> VulkanRenderer {
        let entry = ash::Entry::linked();
        let instance = VulkanRenderer::create_instance(&entry);

        let suitable_physical_device = VulkanRenderer::find_suitable_physical_device(&instance);

        let (device, queue) = VulkanRenderer::create_device(&instance, &suitable_physical_device);

        let (command_pool, command_buffers) = VulkanRenderer::create_command_buffers(
            &device,
            suitable_physical_device.graphics_queue_family_index,
            image_count as u32,
        );

        let (framebuffers, import_token) = VulkanRenderer::allocate_framebuffers(
            &instance,
            &suitable_physical_device.device,
            &device,
            width,
            height,
            image_count,
        )
        .await
        .expect("failed to allocated sysmem-backed VkImages");

        let fence_create_info = vk::FenceCreateInfo { ..Default::default() };
        let fence = unsafe {
            device.create_fence(&fence_create_info, None).expect("couldn't create fence")
        };
        VulkanRenderer {
            _entry: entry,
            instance,
            device,
            queue,
            queue_family_index: suitable_physical_device.graphics_queue_family_index,
            command_pool,
            command_buffers,
            framebuffers,
            fence,
            import_token,
        }
    }

    fn create_instance(entry: &ash::Entry) -> ash::Instance {
        let app_name = CString::new("flatland-view-provider example").unwrap();
        let engine_name = CString::new("(no engine)").unwrap();
        let app_info = vk::ApplicationInfo::builder()
            .application_name(&app_name)
            .application_version(APPLICATION_VERSION)
            .engine_name(&engine_name)
            .engine_version(ENGINE_VERSION)
            .api_version(API_VERSION)
            .build();

        let pp_enabled_extension_names: Vec<*const c_char> = vec![
            ext::DebugUtils::name().as_ptr(),
            vk::KhrExternalMemoryCapabilitiesFn::name().as_ptr(),
            vk::KhrExternalSemaphoreCapabilitiesFn::name().as_ptr(),
        ];

        let enabled_layer_names: Vec<CString> = vec!["VK_LAYER_KHRONOS_validation"]
            .iter()
            .map(|layer_name| CString::new(*layer_name).unwrap())
            .collect();
        let pp_enabled_layer_names: Vec<*const c_char> =
            enabled_layer_names.iter().map(|layer_name| layer_name.as_ptr()).collect();

        // Enable synchronization validation in the `VkInstance` we're about to create.
        let mut validation_features = vk::ValidationFeaturesEXT::builder()
            .enabled_validation_features(&[
                vk::ValidationFeatureEnableEXT::SYNCHRONIZATION_VALIDATION,
            ])
            .build();

        let create_info = vk::InstanceCreateInfo::builder()
            .push_next(&mut validation_features)
            .application_info(&app_info)
            .enabled_layer_names(&pp_enabled_layer_names)
            .enabled_extension_names(&pp_enabled_extension_names);

        let instance: ash::Instance = unsafe {
            entry.create_instance(&create_info, None).expect("Failed to create instance!")
        };

        instance
    }

    fn find_suitable_physical_device(instance: &ash::Instance) -> SuitablePhysicalDevice {
        let physical_devices = unsafe {
            instance
                .enumerate_physical_devices()
                .expect("Failed to enumerate Vulkan physical devices.")
        };

        physical_devices
            .iter()
            .find_map(|physical_device| {
                SuitablePhysicalDevice::consider(instance, *physical_device)
            })
            .expect("Failed to find a suitable Vulkan physical device.")
    }

    fn create_device(
        instance: &ash::Instance,
        physical_device: &SuitablePhysicalDevice,
    ) -> (ash::Device, vk::Queue) {
        let queue_priorities = [1.0_f32];
        let queue_create_info = vk::DeviceQueueCreateInfo::builder()
            .queue_family_index(physical_device.graphics_queue_family_index)
            .queue_priorities(&queue_priorities)
            .build();

        let enabled_layer_names: Vec<CString> = vec!["VK_LAYER_KHRONOS_validation"]
            .iter()
            .map(|layer_name| CString::new(*layer_name).unwrap())
            .collect();
        let pp_enabled_layer_names: Vec<*const c_char> =
            enabled_layer_names.iter().map(|layer_name| layer_name.as_ptr()).collect();

        let pp_enabled_extension_names: Vec<*const c_char> = vec![
            vk::ExtQueueFamilyForeignFn::name().as_ptr(),
            vk::FuchsiaBufferCollectionFn::name().as_ptr(),
            vk::FuchsiaExternalMemoryFn::name().as_ptr(),
            vk::KhrExternalMemoryFn::name().as_ptr(),
            // TODO(fxbug.dev/104692): see the comment in `render_rgba()` about using semaphores
            // instead of a VkFence.  To this, we need to add the external semaphore extensions.
            // vk::KhrExternalSemaphoreFn::name().as_ptr(),
            // vk::FuchsiaExternalSemaphoreFn::name().as_ptr(),
        ];

        let device_create_info = vk::DeviceCreateInfo::builder()
            .queue_create_infos(&[queue_create_info])
            .enabled_layer_names(&pp_enabled_layer_names)
            .enabled_extension_names(&pp_enabled_extension_names)
            .build();

        let device: ash::Device = unsafe {
            instance
                .create_device(physical_device.device, &device_create_info, None)
                .expect("Failed to create logical Device!")
        };

        let graphics_queue =
            unsafe { device.get_device_queue(physical_device.graphics_queue_family_index, 0) };

        (device, graphics_queue)
    }

    fn create_command_buffers(
        device: &ash::Device,
        queue_family_index: u32,
        command_buffer_count: u32,
    ) -> (vk::CommandPool, Vec<vk::CommandBuffer>) {
        let command_pool_create_info = vk::CommandPoolCreateInfo {
            queue_family_index,
            flags: vk::CommandPoolCreateFlags::TRANSIENT
                | vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER,
            ..Default::default()
        };

        let command_pool = unsafe {
            device
                .create_command_pool(&command_pool_create_info, None)
                .expect("failed to create VkCommandPool")
        };

        let command_buffer_allocate_info = vk::CommandBufferAllocateInfo {
            command_buffer_count,
            command_pool,
            level: vk::CommandBufferLevel::PRIMARY,
            ..Default::default()
        };

        let command_buffers = unsafe {
            device
                .allocate_command_buffers(&command_buffer_allocate_info)
                .expect("Failed to allocate Command Buffers!")
        };

        (command_pool, command_buffers)
    }

    async fn allocate_framebuffers(
        instance: &ash::Instance,
        _physical_device: &vk::PhysicalDevice,
        device: &ash::Device,
        width: u32,
        height: u32,
        image_count: usize,
    ) -> Result<(Vec<ImageMemory>, fland::BufferCollectionImportToken), Error> {
        let sysmem_allocator = connect_to_protocol::<fsysmem::AllocatorMarker>()?;
        sysmem_allocator.set_debug_client_info(
            "flatland-view-provider example",
            fuchsia_runtime::process_self().get_koid()?.raw_koid(),
        )?;

        let (buffer_collection_token, buffer_collection_token_server_end) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;
        sysmem_allocator.allocate_shared_collection(buffer_collection_token_server_end)?;

        // Temporarily transform this from a `ClientEnd` to a `Proxy` in order to make a duplicate.
        let mut buffer_collection_token = buffer_collection_token.into_proxy()?;
        let buffer_collection_token_for_vulkan =
            duplicate_buffer_collection_token(&mut buffer_collection_token).await?;

        let buffer_collection_token = ClientEnd::<fsysmem::BufferCollectionTokenMarker>::new(
            buffer_collection_token.into_channel().unwrap().into_zx_channel(),
        );

        let import_export_tokens = BufferCollectionTokenPair::new();
        {
            let scenic_allocator = connect_to_protocol::<fland::AllocatorMarker>()?;

            let args = fland::RegisterBufferCollectionArgs {
                export_token: Some(import_export_tokens.export_token),
                buffer_collection_token: Some(buffer_collection_token),
                ..fland::RegisterBufferCollectionArgs::EMPTY
            };
            // Two possible errors here:
            //   - FIDL transport error (outer error)
            //   - Sysmem error (inner error)
            scenic_allocator
                .register_buffer_collection(args)
                .await?
                .expect("failed to register BufferCollection");
        }

        // This is used for two purposes:
        //   - to specify constraints to set on the `VkBufferCollectionFUCHSIA`
        //   - to create `VkImages` once the buffer collection has been allocated; the only change
        //     is chaining a `VkBufferCollectionImageCreateInfoFUCHSIA` via the `p_next` slot.
        let vk_image_create_info = vk::ImageCreateInfo::builder()
            .image_type(vk::ImageType::TYPE_2D)
            .format(FRAMEBUFFER_FORMAT)
            .extent(vk::Extent3D { width, height, depth: 1 })
            .mip_levels(1)
            .array_layers(1)
            .samples(vk::SampleCountFlags::TYPE_1)
            .tiling(vk::ImageTiling::OPTIMAL)
            // - TRANSFER_DST required to clear framebuffers via `vkCmdClearColorImage()`
            // TODO(fxbug.dev/104692): if we start using shader pipelines to render into the image,
            // we'll need to add COLOR_ATTACHMENT to the usage flags.
            .usage(vk::ImageUsageFlags::TRANSFER_DST)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .initial_layout(vk::ImageLayout::UNDEFINED)
            .build();

        // Also used to set buffer collection constraints.
        let p_color_spaces = [vk::SysmemColorSpaceFUCHSIA::builder()
            .color_space(FRAMEBUFFER_COLOR_SPACE as u32)
            .build()];

        // Also used to set buffer collection constraints.
        let vk_image_format_constraints_info = [vk::ImageFormatConstraintsInfoFUCHSIA::builder()
            .image_create_info(vk_image_create_info)
            // TODO(fxbug.dev/104692): if we start using shader pipelines to render into the image,
            // we'll need to add COLOR_ATTACHMENT to the usage flags.
            .required_format_features(
                vk::FormatFeatureFlags::TRANSFER_SRC | vk::FormatFeatureFlags::TRANSFER_DST,
            )
            .flags(Default::default())
            // Although the docs for VkImageFormatConstraintsInfoFUCHSIA don't mention it, 'vk.xml'
            // declares this as optional, so 0 is an acceptable value.  This is interpreted as us
            // not having an opinion about the pixel format type.
            .sysmem_pixel_format(0)
            .color_spaces(&p_color_spaces)
            .build()];

        // Also used to set buffer collection constraints.
        let vk_image_constraints_info = vk::ImageConstraintsInfoFUCHSIA::builder()
            .format_constraints(&vk_image_format_constraints_info)
            .buffer_collection_constraints(
                vk::BufferCollectionConstraintsInfoFUCHSIA::builder()
                    .min_buffer_count(image_count as u32)
                    .max_buffer_count(image_count as u32)
                    .build(),
            )
            // No `ImageConstraintsInfoFlagsFUCHSIA` required, because we have no need for protected
            // memory; if we wanted to display DRM-protected video this would be different.
            .build();

        let vk_buffer_collection_create_info = vk::BufferCollectionCreateInfoFUCHSIA {
            collection_token: buffer_collection_token_for_vulkan.into_channel().into_raw(),
            ..Default::default()
        };

        // Instantiate Ash wrapper to call Fuchsia/Vulkan buffer collection APIs.
        let ext_buffer_collection = fuchsia::BufferCollection::new(instance, device);
        let vk_buffer_collection = unsafe {
            ext_buffer_collection.create_buffer_collection(&vk_buffer_collection_create_info, None)
        }?;
        unsafe {
            ext_buffer_collection.set_buffer_collection_image_constraints(
                vk_buffer_collection,
                &vk_image_constraints_info,
            )
        }?;

        // This provides the `memory_type_index` that we will use to allocate memory for the image.
        let vk_buffer_collection_properties = unsafe {
            ext_buffer_collection.get_buffer_collection_properties(vk_buffer_collection)
        }?;

        // Since we obtained the properties anyway, might as well verify that we allocated the
        // expected number of images.
        assert_eq!(image_count, vk_buffer_collection_properties.buffer_count as usize);

        // Iterate over the buffer collection indices to:
        //   - allocate new images
        //   - allocate memory for each new image
        //   - bind the image to the allocated memory
        let mut framebuffers = Vec::new();
        for index in 0..image_count as u32 {
            let vk_buffer_collection_image_create_info =
                vk::BufferCollectionImageCreateInfoFUCHSIA {
                    collection: vk_buffer_collection,
                    index,
                    ..Default::default()
                };

            // Repurpose the struct from above.  Pretty much all of the properties in it are good,
            // we just need to extend it with appropriate `PNext` structs.
            let vk_image_create_info = vk::ImageCreateInfo {
                p_next: &vk_buffer_collection_image_create_info as *const _ as *const c_void,
                // Copy the rest of the fields from the previously-created VkImageCreteInfo.
                ..vk_image_create_info
            };

            let image = unsafe { device.create_image(&vk_image_create_info, None) }?;

            // Import memory from the buffer collection to bind to the image we just created.
            let allocation_size = unsafe { device.get_image_memory_requirements(image).size };

            let mut import_memory_extension = vk::ImportMemoryBufferCollectionFUCHSIA::builder()
                .collection(vk_buffer_collection)
                .index(index)
                .build();
            let memory_allocate_info = vk::MemoryAllocateInfo::builder()
                .push_next(&mut import_memory_extension)
                .allocation_size(allocation_size)
                .memory_type_index(
                    first_bit_index(vk_buffer_collection_properties.memory_type_bits)
                        .expect("no valid memory types available"),
                )
                .build();
            let device_memory = unsafe {
                device
                    .allocate_memory(&memory_allocate_info, None)
                    .expect("failed to allocate memory")
            };

            unsafe {
                // Offset is zero because any offset within the buffer collection VMO is taken into
                // account when allocating the device memory above.
                let offset = 0;
                device
                    .bind_image_memory(image, device_memory, offset)
                    .expect("failed to bind image memory");
            }

            framebuffers.push(ImageMemory { image, device_memory });
        }

        // According to the spec, images created using the buffer collection are allowed to outlive
        // it.  We have no further use for the buffer collection; now is a good time to destroy it.
        unsafe {
            ext_buffer_collection.destroy_buffer_collection(vk_buffer_collection, None);
        }

        Ok((framebuffers, import_export_tokens.import_token))
    }

    fn standard_image_subresource_range() -> vk::ImageSubresourceRange {
        vk::ImageSubresourceRange::builder()
            .aspect_mask(vk::ImageAspectFlags::COLOR)
            .base_mip_level(0)
            .level_count(1)
            .base_array_layer(0)
            .layer_count(1)
            .build()
    }
}

// Return the 0-index of the lowest-order bit in `num`.  For example, if `num == 6` then the two
// lowest-order bit is at index 1.
fn first_bit_index(num: u32) -> Option<u32> {
    for shift in 0..32 {
        if 1 == (num >> shift) & 1 {
            return Some(shift);
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use crate::render_vk::first_bit_index;

    #[fuchsia::test]
    fn no_bits() {
        assert_eq!(None, first_bit_index(0));
    }

    #[fuchsia::test]
    fn correct_bit() {
        assert_eq!(Some(0), first_bit_index(1));
        assert_eq!(Some(1), first_bit_index(2));
        assert_eq!(Some(2), first_bit_index(4));
        assert_eq!(Some(3), first_bit_index(8));
        assert_eq!(Some(4), first_bit_index(16));
        assert_eq!(Some(5), first_bit_index(32));
        assert_eq!(Some(6), first_bit_index(64));
        assert_eq!(Some(7), first_bit_index(128));
        assert_eq!(Some(8), first_bit_index(256));
        assert_eq!(Some(9), first_bit_index(512));
        assert_eq!(Some(10), first_bit_index(1024));
        assert_eq!(Some(11), first_bit_index(2048));
        assert_eq!(Some(12), first_bit_index(4096));
        assert_eq!(Some(13), first_bit_index(8192));
        assert_eq!(Some(14), first_bit_index(16384));
        assert_eq!(Some(15), first_bit_index(32768));
        assert_eq!(Some(16), first_bit_index(65536));
    }

    #[fuchsia::test]
    fn first_bit() {
        assert_eq!(Some(0), first_bit_index(0b11));
        assert_eq!(Some(0), first_bit_index(0b101));
        assert_eq!(Some(0), first_bit_index(0b1101));
        assert_eq!(Some(0), first_bit_index(0b1111));
        assert_eq!(Some(0), first_bit_index(0b11101));

        assert_eq!(Some(1), first_bit_index(0b110));
        assert_eq!(Some(1), first_bit_index(0b1010));
        assert_eq!(Some(1), first_bit_index(0b11010));
        assert_eq!(Some(1), first_bit_index(0b11110));
        assert_eq!(Some(1), first_bit_index(0b111010));

        assert_eq!(Some(2), first_bit_index(0b1100));
        assert_eq!(Some(2), first_bit_index(0b10100));
        assert_eq!(Some(2), first_bit_index(0b110100));
        assert_eq!(Some(2), first_bit_index(0b111100));
        assert_eq!(Some(2), first_bit_index(0b1110100));

        assert_eq!(Some(3), first_bit_index(0b11000));
        assert_eq!(Some(3), first_bit_index(0b101000));
        assert_eq!(Some(3), first_bit_index(0b1101000));
        assert_eq!(Some(3), first_bit_index(0b1111000));
        assert_eq!(Some(3), first_bit_index(0b11101000));
    }
}
