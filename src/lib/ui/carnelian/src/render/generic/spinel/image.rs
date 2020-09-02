// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, ensure, Error};
use std::{
    cell::Cell, ffi::c_void, io::Read, marker::PhantomPinned, ops::Deref, pin::Pin, ptr, slice,
    time::Duration,
};

use spinel_rs_sys::*;
use vk_sys as vk;

use crate::render::generic::spinel::*;

// Make sure that the Spinel callback actually reads from the right place by pinning the
// vk::DevicePointers in memory. Possible improvement would be to send a Weak pointer through the
// FFI, but Weak::into_raw and Weak::from_raw are not stable yet.
//
// https://github.com/rust-lang/rust/issues/60728
#[derive(Debug)]
struct PinnedVk {
    vk: vk::DevicePointers,
    _pin: PhantomPinned,
}

impl PinnedVk {
    pub fn new(vk: vk::DevicePointers) -> Pin<Box<PinnedVk>> {
        Box::pin(PinnedVk { vk, _pin: PhantomPinned })
    }
}

impl Deref for PinnedVk {
    type Target = vk::DevicePointers;

    fn deref(&self) -> &Self::Target {
        &self.vk
    }
}

extern "C" fn image_dispatch_submitter(
    queue: vk::Queue,
    fence: vk::Fence,
    cb: vk::CommandBuffer,
    data: *const vk::DevicePointers,
) {
    unsafe {
        // Safe as images outlive the spinel context.
        vk!((*data).QueueSubmit(
            queue,
            1,
            // TODO: Add acquire and release semaphores.
            &vk::SubmitInfo {
                sType: vk::STRUCTURE_TYPE_SUBMIT_INFO,
                pNext: ptr::null(),
                waitSemaphoreCount: 0,
                pWaitSemaphores: ptr::null(),
                pWaitDstStageMask: ptr::null(),
                commandBufferCount: 1,
                pCommandBuffers: &cb,
                signalSemaphoreCount: 0,
                pSignalSemaphores: ptr::null(),
            },
            fence,
        ));
    }
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug)]
struct ImportMemoryBufferCollectionFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    collection: BufferCollectionFUCHSIA,
    index: u32,
}

const SAMPLER_CREATE_INFO: vk::SamplerCreateInfo = vk::SamplerCreateInfo {
    sType: vk::STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    pNext: ptr::null(),
    flags: 0,
    magFilter: vk::FILTER_NEAREST,
    minFilter: vk::FILTER_NEAREST,
    mipmapMode: vk::SAMPLER_MIPMAP_MODE_NEAREST,
    addressModeU: vk::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    addressModeV: vk::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    addressModeW: vk::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    mipLodBias: 0.0,
    anisotropyEnable: vk::FALSE,
    maxAnisotropy: 1.0,
    compareEnable: vk::FALSE,
    compareOp: vk::COMPARE_OP_NEVER,
    minLod: 0.0,
    maxLod: 0.0,
    borderColor: vk::BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    unnormalizedCoordinates: vk::TRUE,
};

pub fn image_create_info(
    width: u32,
    height: u32,
    format: vk::Format,
    tiling: vk::ImageTiling,
    p_next: *const c_void,
) -> vk::ImageCreateInfo {
    vk::ImageCreateInfo {
        sType: vk::STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        pNext: p_next,
        flags: 0,
        imageType: vk::IMAGE_TYPE_2D,
        format,
        extent: vk::Extent3D { width, height, depth: 1 },
        mipLevels: 1,
        arrayLayers: 1,
        samples: vk::SAMPLE_COUNT_1_BIT,
        tiling,
        usage: vk::IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | vk::IMAGE_USAGE_STORAGE_BIT
            | vk::IMAGE_USAGE_TRANSFER_SRC_BIT
            | vk::IMAGE_USAGE_TRANSFER_DST_BIT,
        sharingMode: vk::SHARING_MODE_EXCLUSIVE,
        queueFamilyIndexCount: 0,
        pQueueFamilyIndices: ptr::null(),
        initialLayout: vk::IMAGE_LAYOUT_UNDEFINED,
    }
}

fn image_view_create_info(image: vk::Image, format: vk::Format) -> vk::ImageViewCreateInfo {
    vk::ImageViewCreateInfo {
        sType: vk::STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        pNext: ptr::null(),
        flags: 0,
        image,
        viewType: vk::IMAGE_VIEW_TYPE_2D,
        format,
        components: vk::ComponentMapping {
            r: vk::COMPONENT_SWIZZLE_R,
            g: vk::COMPONENT_SWIZZLE_G,
            b: vk::COMPONENT_SWIZZLE_B,
            a: vk::COMPONENT_SWIZZLE_A,
        },
        subresourceRange: vk::ImageSubresourceRange {
            aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
            baseMipLevel: 0,
            levelCount: 1,
            baseArrayLayer: 0,
            layerCount: 1,
        },
    }
}

pub(crate) fn update_descriptor_set(
    vk: &vk::DevicePointers,
    device: vk::Device,
    src_image: &VulkanImage,
    dst_image: &VulkanImage,
    descriptor_set: vk::DescriptorSet,
) {
    let descriptor_set = [
        vk::WriteDescriptorSet {
            sType: vk::STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            pNext: ptr::null(),
            dstSet: descriptor_set,
            dstBinding: 0,
            dstArrayElement: 0,
            descriptorCount: 1,
            descriptorType: vk::DESCRIPTOR_TYPE_STORAGE_IMAGE,
            pImageInfo: &vk::DescriptorImageInfo {
                sampler: src_image.sampler,
                imageView: src_image.view,
                imageLayout: vk::IMAGE_LAYOUT_GENERAL,
            },
            pBufferInfo: ptr::null(),
            pTexelBufferView: ptr::null(),
        },
        vk::WriteDescriptorSet {
            sType: vk::STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            pNext: ptr::null(),
            dstSet: descriptor_set,
            dstBinding: 1,
            dstArrayElement: 0,
            descriptorCount: 1,
            descriptorType: vk::DESCRIPTOR_TYPE_STORAGE_IMAGE,
            pImageInfo: &vk::DescriptorImageInfo {
                sampler: dst_image.sampler,
                imageView: dst_image.view,
                imageLayout: vk::IMAGE_LAYOUT_GENERAL,
            },
            pBufferInfo: ptr::null(),
            pTexelBufferView: ptr::null(),
        },
    ];
    unsafe {
        vk.UpdateDescriptorSets(
            device,
            descriptor_set.len() as u32,
            descriptor_set.as_ptr(),
            0,
            ptr::null(),
        );
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct SpinelImage(pub(crate) usize);

#[derive(Debug)]
pub(crate) struct VulkanImage {
    device: vk::Device,
    image: vk::Image,
    sampler: vk::Sampler,
    view: vk::ImageView,
    memory: vk::DeviceMemory,
    vk: Pin<Box<PinnedVk>>,
    width: u32,
    height: u32,
    layout: Cell<vk::ImageLayout>,
    stage: Cell<vk::PipelineStageFlagBits>,
    access_mask: Cell<vk::AccessFlags>,
    pool: Cell<Option<vk::CommandPool>>,
    cb: Cell<Option<vk::CommandBuffer>>,
    queue: Cell<Option<vk::Queue>>,
    fence: Cell<Option<vk::Fence>>,
    id: SpinelImage,
}

impl VulkanImage {
    pub fn new(
        device: vk::Device,
        vk_i: &vk::InstancePointers,
        width: u32,
        height: u32,
        format: vk::Format,
        id: SpinelImage,
    ) -> Self {
        let vk = device_pointers(vk_i, device);
        let image = unsafe {
            let info =
                image_create_info(width, height, format, vk::IMAGE_TILING_OPTIMAL, ptr::null());
            init(|ptr| vk!(vk.CreateImage(device, &info, ptr::null(), ptr)))
        };

        let mem_reqs = unsafe { init(|ptr| vk.GetImageMemoryRequirements(device, image, ptr)) };

        let mem_type_bits = mem_reqs.memoryTypeBits;
        assert_ne!(mem_type_bits, 0);
        let mem_type_index = mem_type_bits.trailing_zeros();

        let memory = unsafe {
            init(|ptr| {
                vk!(vk.AllocateMemory(
                    device,
                    &vk::MemoryAllocateInfo {
                        sType: vk::STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        pNext: ptr::null(),
                        allocationSize: mem_reqs.size as u64,
                        memoryTypeIndex: mem_type_index,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        unsafe {
            vk!(vk.BindImageMemory(device, image, memory, 0));
        }

        let sampler = unsafe {
            init(|ptr| vk!(vk.CreateSampler(device, &SAMPLER_CREATE_INFO, ptr::null(), ptr)))
        };

        let view = unsafe {
            init(|ptr| {
                vk!(vk.CreateImageView(
                    device,
                    &image_view_create_info(image, format),
                    ptr::null(),
                    ptr
                ))
            })
        };

        Self {
            device,
            image,
            sampler,
            view,
            memory,
            vk: PinnedVk::new(vk),
            width,
            height,
            layout: Cell::new(vk::IMAGE_LAYOUT_UNDEFINED),
            stage: Cell::new(vk::PIPELINE_STAGE_TOP_OF_PIPE_BIT),
            access_mask: Cell::new(0),
            pool: Cell::default(),
            cb: Cell::default(),
            queue: Cell::default(),
            fence: Cell::default(),
            id,
        }
    }

    pub fn from_png<R: Read>(
        device: vk::Device,
        vk_i: &vk::InstancePointers,
        format: vk::Format,
        reader: &mut png::Reader<R>,
        id: SpinelImage,
    ) -> Result<Self, Error> {
        let info = reader.info();
        let color_type = info.color_type;
        ensure!(
            color_type == png::ColorType::RGBA || color_type == png::ColorType::RGB,
            "unsupported color type {:#?}",
            color_type
        );
        let (width, height) = info.size();
        let vk = device_pointers(vk_i, device);
        let image = unsafe {
            let info =
                image_create_info(width, height, format, vk::IMAGE_TILING_LINEAR, ptr::null());
            init(|ptr| vk!(vk.CreateImage(device, &info, ptr::null(), ptr)))
        };

        let mem_reqs = unsafe { init(|ptr| vk.GetImageMemoryRequirements(device, image, ptr)) };

        let mem_type_bits = mem_reqs.memoryTypeBits;
        ensure!(mem_type_bits != 0, "unsupported memory type bits: {}", mem_type_bits);
        let mem_type_index = mem_type_bits.trailing_zeros();

        let memory = unsafe {
            init(|ptr| {
                vk!(vk.AllocateMemory(
                    device,
                    &vk::MemoryAllocateInfo {
                        sType: vk::STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        pNext: ptr::null(),
                        allocationSize: mem_reqs.size as u64,
                        memoryTypeIndex: mem_type_index,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        unsafe {
            vk!(vk.BindImageMemory(device, image, memory, 0));
        }

        let subresource = vk::ImageSubresource {
            aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
            mipLevel: 0,
            arrayLayer: 0,
        };
        let layout =
            unsafe { init(|ptr| vk.GetImageSubresourceLayout(device, image, &subresource, ptr)) };
        let size = layout.rowPitch as usize * height as usize;
        let data = unsafe { init(|ptr| vk!(vk.MapMemory(device, memory, 0, size as u64, 0, ptr))) };
        let slice = unsafe { slice::from_raw_parts_mut(data as *mut u8, size) };
        for dst_row in slice.chunks_mut(layout.rowPitch as usize) {
            let src_row = reader.next_row()?.unwrap();
            match format {
                vk::FORMAT_R8G8B8A8_UNORM | vk::FORMAT_R8G8B8A8_SRGB => {
                    if color_type == png::ColorType::RGB {
                        // Transfer row and convert to BGRA.
                        for (src, dst) in src_row.chunks(3).zip(dst_row.chunks_mut(4)) {
                            dst.copy_from_slice(&[src[0], src[1], src[2], 0xff]);
                        }
                    } else {
                        dst_row.copy_from_slice(src_row);
                    }
                }
                vk::FORMAT_B8G8R8A8_UNORM | vk::FORMAT_B8G8R8A8_SRGB => {
                    if color_type == png::ColorType::RGB {
                        // Transfer row and convert to BGRA.
                        for (src, dst) in src_row.chunks(3).zip(dst_row.chunks_mut(4)) {
                            dst.copy_from_slice(&[src[2], src[1], src[0], 0xff]);
                        }
                    } else {
                        for (src, dst) in src_row.chunks(4).zip(dst_row.chunks_mut(4)) {
                            dst.copy_from_slice(&[src[2], src[1], src[0], src[3]]);
                        }
                    }
                }
                _ => bail!("Unsupported image format {}", format),
            }
        }

        unsafe {
            vk.UnmapMemory(device, memory);
        }

        let sampler = unsafe {
            init(|ptr| vk!(vk.CreateSampler(device, &SAMPLER_CREATE_INFO, ptr::null(), ptr)))
        };

        let view = unsafe {
            init(|ptr| {
                vk!(vk.CreateImageView(
                    device,
                    &image_view_create_info(image, format),
                    ptr::null(),
                    ptr
                ))
            })
        };

        Ok(Self {
            device,
            image,
            sampler,
            view,
            memory,
            vk: PinnedVk::new(vk),
            width,
            height,
            layout: Cell::new(vk::IMAGE_LAYOUT_UNDEFINED),
            stage: Cell::new(vk::PIPELINE_STAGE_TOP_OF_PIPE_BIT),
            access_mask: Cell::new(0),
            pool: Cell::default(),
            cb: Cell::default(),
            queue: Cell::default(),
            fence: Cell::default(),
            id,
        })
    }

    pub fn from_buffer_collection(
        device: vk::Device,
        vk_i: &vk::InstancePointers,
        vk_ext: &FuchsiaExtensionPointers,
        width: u32,
        height: u32,
        format: vk::Format,
        collection: BufferCollectionFUCHSIA,
        index: u32,
        id: SpinelImage,
    ) -> Self {
        let vk = device_pointers(vk_i, device);
        let image = unsafe {
            let p_next = BufferCollectionImageCreateInfoFUCHSIA {
                sType: STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
                pNext: ptr::null(),
                collection,
                index,
            };

            init(|ptr| {
                vk!(vk.CreateImage(
                    device,
                    &image_create_info(
                        width,
                        height,
                        format,
                        vk::IMAGE_TILING_OPTIMAL,
                        &p_next as *const _ as *const c_void,
                    ),
                    ptr::null(),
                    ptr
                ))
            })
        };

        let mem_reqs = unsafe { init(|ptr| vk.GetImageMemoryRequirements(device, image, ptr)) };

        let mut mem_props = BufferCollectionPropertiesFUCHSIA {
            sType: STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA,
            pNext: ptr::null(),
            memoryTypeBits: 0,
            count: 0,
        };
        unsafe {
            vk!(vk_ext.GetBufferCollectionPropertiesFUCHSIA(device, collection, &mut mem_props));
        }

        let mem_type_bits = mem_reqs.memoryTypeBits & mem_props.memoryTypeBits;
        assert_ne!(mem_type_bits, 0);
        let mem_type_index = mem_type_bits.trailing_zeros();

        let memory = unsafe {
            init(|ptr| {
                let p_next = ImportMemoryBufferCollectionFUCHSIA {
                    sType: STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
                    pNext: ptr::null(),
                    collection,
                    index,
                };

                vk!(vk.AllocateMemory(
                    device,
                    &vk::MemoryAllocateInfo {
                        sType: vk::STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                        pNext: &p_next as *const _ as *const c_void,
                        allocationSize: mem_reqs.size as u64,
                        memoryTypeIndex: mem_type_index,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        unsafe {
            vk!(vk.BindImageMemory(device, image, memory, 0));
        }

        let sampler = unsafe {
            init(|ptr| vk!(vk.CreateSampler(device, &SAMPLER_CREATE_INFO, ptr::null(), ptr)))
        };

        let view = unsafe {
            init(|ptr| {
                vk!(vk.CreateImageView(
                    device,
                    &image_view_create_info(image, format),
                    ptr::null(),
                    ptr
                ))
            })
        };

        Self {
            device,
            image,
            sampler,
            view,
            memory,
            vk: PinnedVk::new(vk),
            width,
            height,
            layout: Cell::new(vk::IMAGE_LAYOUT_UNDEFINED),
            stage: Cell::new(vk::PIPELINE_STAGE_TOP_OF_PIPE_BIT),
            access_mask: Cell::new(0),
            pool: Cell::default(),
            cb: Cell::default(),
            queue: Cell::default(),
            fence: Cell::default(),
            id,
        }
    }

    pub fn image(&self) -> vk::Image {
        self.image
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    pub fn layout(&self) -> vk::ImageLayout {
        self.layout.get()
    }

    pub fn set_layout(&self, layout: vk::ImageLayout) {
        self.layout.set(layout);
    }

    pub fn rs_image(
        &self,
        rs_image_ext: *mut c_void,
    ) -> SpnVkRenderSubmitExtImageRender<vk::DevicePointers> {
        SpnVkRenderSubmitExtImageRender {
            ext: rs_image_ext,
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImageRender,
            image: self.image,
            image_info: vk::DescriptorImageInfo {
                sampler: self.sampler,
                imageView: self.view,
                imageLayout: vk::IMAGE_LAYOUT_GENERAL,
            },
            submitter_pfn: image_dispatch_submitter,
            // Safe as image outlive context.
            submitter_data: &**self.vk,
        }
    }

    pub fn layout_transition(
        &self,
        new_layout: vk::ImageLayout,
        stage: vk::PipelineStageFlagBits,
        access_mask: vk::AccessFlagBits,
    ) {
        let pool = self.pool.take().unwrap_or_else(|| unsafe {
            init(|ptr| {
                vk!(self.vk.CreateCommandPool(
                    self.device,
                    &vk::CommandPoolCreateInfo {
                        sType: vk::STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: vk::COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                        queueFamilyIndex: 0,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        });

        let cb = self.cb.take().unwrap_or_else(|| unsafe {
            let info = vk::CommandBufferAllocateInfo {
                sType: vk::STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                pNext: ptr::null(),
                commandPool: pool,
                level: vk::COMMAND_BUFFER_LEVEL_PRIMARY,
                commandBufferCount: 1,
            };

            init(|ptr| vk!(self.vk.AllocateCommandBuffers(self.device, &info, ptr)))
        });

        let queue = self.queue.take().unwrap_or_else(|| unsafe {
            init(|ptr| self.vk.GetDeviceQueue(self.device, 0, 0, ptr))
        });

        let fence = self.fence.take().unwrap_or_else(|| unsafe {
            init(|ptr| {
                vk!(self.vk.CreateFence(
                    self.device,
                    &vk::FenceCreateInfo {
                        sType: vk::STRUCTURE_TYPE_FENCE_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: vk::FENCE_CREATE_SIGNALED_BIT,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        });

        unsafe {
            vk!(self.vk.WaitForFences(
                self.device,
                1,
                &fence,
                vk::TRUE,
                Duration::from_secs(10).as_nanos() as u64
            ));
            vk!(self.vk.ResetFences(self.device, 1, &fence));

            vk!(self.vk.BeginCommandBuffer(
                cb,
                &vk::CommandBufferBeginInfo {
                    sType: vk::STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    pNext: ptr::null(),
                    flags: vk::COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                    pInheritanceInfo: ptr::null(),
                },
            ));

            self.vk.CmdPipelineBarrier(
                cb,
                self.stage.get(),
                stage,
                0,
                0,
                ptr::null(),
                0,
                ptr::null(),
                1,
                &vk::ImageMemoryBarrier {
                    sType: vk::STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    pNext: ptr::null(),
                    srcAccessMask: self.access_mask.get(),
                    dstAccessMask: access_mask,
                    oldLayout: self.layout.get(),
                    newLayout: new_layout,
                    srcQueueFamilyIndex: 0,
                    dstQueueFamilyIndex: 0,
                    image: self.image,
                    subresourceRange: vk::ImageSubresourceRange {
                        aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
                        baseMipLevel: 0,
                        levelCount: 1,
                        baseArrayLayer: 0,
                        layerCount: 1,
                    },
                },
            );

            vk!(self.vk.EndCommandBuffer(cb));

            vk!(self.vk.QueueSubmit(
                queue,
                1,
                &vk::SubmitInfo {
                    sType: vk::STRUCTURE_TYPE_SUBMIT_INFO,
                    pNext: ptr::null(),
                    waitSemaphoreCount: 0,
                    pWaitSemaphores: ptr::null(),
                    pWaitDstStageMask: ptr::null(),
                    commandBufferCount: 1,
                    pCommandBuffers: &cb,
                    signalSemaphoreCount: 0,
                    pSignalSemaphores: ptr::null(),
                },
                fence,
            ));
        }

        self.layout.set(new_layout);
        self.stage.set(stage);
        self.access_mask.set(access_mask);

        // Cache pool, cb, queue and fence.
        self.pool.set(Some(pool));
        self.cb.set(Some(cb));
        self.queue.set(Some(queue));
        self.fence.set(Some(fence));
    }
}

impl Drop for VulkanImage {
    fn drop(&mut self) {
        unsafe {
            self.vk.DestroyImageView(self.device, self.view, ptr::null());
            self.vk.DestroySampler(self.device, self.sampler, ptr::null());
            self.vk.DestroyImage(self.device, self.image, ptr::null());
            self.vk.FreeMemory(self.device, self.memory, ptr::null());
        }

        if let Some(pool) = self.pool.take() {
            unsafe {
                self.vk.DestroyCommandPool(self.device, pool, ptr::null());
            }
        }

        if let Some(fence) = self.fence.take() {
            unsafe {
                self.vk.DestroyFence(self.device, fence, ptr::null());
            }
        }
    }
}
