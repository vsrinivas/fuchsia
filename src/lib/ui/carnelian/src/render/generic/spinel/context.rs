// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    cmp::Ordering,
    collections::HashMap,
    ffi::{c_void, CStr},
    fmt,
    fs::File,
    io::Read,
    mem::{self, MaybeUninit},
    ptr,
    rc::Rc,
};

use anyhow::Error;
use euclid::default::{Rect, Size2D};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;
use fuchsia_framebuffer::PixelFormat;
use fuchsia_trace as trace;
use fuchsia_zircon::prelude::*;
use spinel_rs_sys::*;
use vk_sys as vk;

use crate::{
    render::generic::{
        spinel::{
            image::{image_create_info, update_descriptor_set, VulkanImage},
            *,
        },
        Context, PostCopy, PreClear, PreCopy, RenderExt,
    },
    ViewAssistantContext,
};

// Matches struct in copy.comp. See shader source for field descriptions.
#[repr(C)]
#[derive(Debug, Default)]
struct CopyParams {
    src_offset: [i32; 2], // ivec2 at 0  (8 byte aligned)
    dst_offset: [i32; 2], // ivec2 at 8  (8 byte aligned)
    dxdy: [i32; 2],       // ivec2 at 16 (8 byte aligned)
    extent: u32,          // uint  at 20 (4 byte aligned)
}

// Matches struct in motioncopy.comp. See shader source for field descriptions.
#[repr(C)]
#[derive(Debug, Default)]
struct MotionCopyParams {
    src_offset: [i32; 2], // ivec2 at 0  (8  byte aligned)
    src_dims: [i32; 2],   // ivec2 at 8  (8  byte aligned)
    dst_offset: [i32; 2], // ivec2 at 16 (8  byte aligned)
    dxdy: [i32; 2],       // ivec2 at 24 (8  byte aligned)
    border: [f32; 4],     // vec4  at 32 (16 byte aligned)
    exposure: u32,        // uint  at 48 (4  byte aligned)
    extent: u32,          // uint  at 52 (4  byte aligned)
}

#[derive(Debug)]
struct VulkanShader {
    pipeline: vk::Pipeline,
    pipeline_layout: vk::PipelineLayout,
}

impl VulkanShader {
    pub fn new(
        vk: &vk::DevicePointers,
        device: vk::Device,
        descriptor_set_layout: vk::DescriptorSetLayout,
        name: &str,
        constant_range_size: usize,
    ) -> Self {
        let pipeline_layout = unsafe {
            init(|ptr| {
                vk!(vk.CreatePipelineLayout(
                    device,
                    &vk::PipelineLayoutCreateInfo {
                        sType: vk::STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        setLayoutCount: 1,
                        pSetLayouts: &descriptor_set_layout,
                        pushConstantRangeCount: 1,
                        pPushConstantRanges: &vk::PushConstantRange {
                            stageFlags: vk::SHADER_STAGE_COMPUTE_BIT,
                            offset: 0,
                            size: constant_range_size as u32,
                        },
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        // glslangValidator -V shaders/name.comp -o shaders/name-noopt.comp.spv
        // spirv-opt -O shaders/name-noopt.comp.spv -o shaders/name.comp.spv
        let mut file = File::open(format!("/pkg/data/shaders/{}.comp.spv", name))
            .expect(&format!("failed to open file /pkg/data/shaders/{}.comp.spv", name));
        let mut shader = vec![];
        file.read_to_end(&mut shader)
            .expect(&format!("failed to read from file /pkg/data/shaders/{}.comp.spv", name));

        let chunks = shader.chunks_exact(mem::size_of::<u32>());
        if !chunks.remainder().is_empty() {
            panic!("wrong number of bytes in shader {}", name);
        }
        let code: Vec<u32> = chunks
            .map(|chunk| {
                let mut bytes = [0; 4];
                bytes.copy_from_slice(chunk);
                u32::from_le_bytes(bytes)
            })
            .collect();

        let shader_module = unsafe {
            init(|ptr| {
                vk!(vk.CreateShaderModule(
                    device,
                    &vk::ShaderModuleCreateInfo {
                        sType: vk::STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        codeSize: shader.len(),
                        pCode: code.as_ptr(),
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        let pipeline = unsafe {
            init(|ptr| {
                vk!(vk.CreateComputePipelines(
                    device,
                    0,
                    1,
                    &vk::ComputePipelineCreateInfo {
                        sType: vk::STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        stage: vk::PipelineShaderStageCreateInfo {
                            sType: vk::STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            pNext: ptr::null(),
                            flags: 0,
                            stage: vk::SHADER_STAGE_COMPUTE_BIT,
                            module: shader_module,
                            pName: cstr!(b"main\0").as_ptr(),
                            pSpecializationInfo: ptr::null(),
                        },
                        layout: pipeline_layout,
                        basePipelineHandle: 0,
                        basePipelineIndex: 0,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        unsafe {
            vk.DestroyShaderModule(device, shader_module, ptr::null());
        }

        VulkanShader { pipeline, pipeline_layout }
    }
}

const DESCRIPTOR_SET_PRE_PROCESS: usize = 0;
const DESCRIPTOR_SET_POST_PROCESS: usize = 1;
const DESCRIPTOR_SET_COUNT: usize = 2;

const STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: u32 = 1_000_059_000;

#[derive(Debug)]
struct VulkanContext {
    device: vk::Device,
    vk_i: vk::InstancePointers,
    vk: vk::DevicePointers,
    vk_ext: FuchsiaExtensionPointers,
    buffer_collection: BufferCollectionFUCHSIA,
    width: u32,
    height: u32,
    format: vk::Format,
    descriptor_pool: vk::DescriptorPool,
    descriptor_set_layout: vk::DescriptorSetLayout,
    descriptor_sets: Vec<vk::DescriptorSet>,
    copy_shader: Option<VulkanShader>,
    motioncopy_shader: Option<VulkanShader>,
}

#[derive(Debug)]
pub(crate) struct InnerContext {
    context: SpnContext,
    is_dropped: bool,
}

impl InnerContext {
    pub fn get(&self) -> SpnContext {
        self.context
    }

    pub fn get_checked(&self) -> Option<SpnContext> {
        if self.is_dropped {
            None
        } else {
            Some(self.context)
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SpinelConfig {
    pub block_pool_size: u64,
    pub handle_count: u32,
}

impl Default for SpinelConfig {
    fn default() -> Self {
        Self { block_pool_size: 1 << 26, handle_count: 1 << 14 }
    }
}

pub struct SpinelContext {
    inner: Rc<RefCell<InnerContext>>,
    path_builder: Rc<SpnPathBuilder>,
    raster_builder: Rc<SpnRasterBuilder>,
    compositions: HashMap<SpinelImage, SpnComposition>,
    vulkan: VulkanContext,
    images: Vec<VulkanImage>,
    index_map: HashMap<u32, usize>,
    image_post_copy_regions: Vec<vk::ImageCopy>,
}

impl SpinelContext {
    pub(crate) fn new(token: ClientEnd<BufferCollectionTokenMarker>, size: Size2D<u32>) -> Self {
        Self::with_config(token, size, SpinelConfig::default())
    }

    pub fn with_config(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
        config: SpinelConfig,
    ) -> Self {
        let entry_points = entry_points();

        macro_rules! vulkan_version {
            ( $major:expr, $minor:expr, $patch:expr ) => {
                ($major as u32) << 22 | ($minor as u32) << 12 | ($patch as u32)
            };
        }

        let layers = if cfg!(debug_assertions) {
            vec![cstr!(b"VK_LAYER_KHRONOS_validation\0").as_ptr()]
        } else {
            vec![]
        };

        let instance = unsafe {
            init(|ptr| {
                vk!(entry_points.CreateInstance(
                    &vk::InstanceCreateInfo {
                        sType: vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        pApplicationInfo: &vk::ApplicationInfo {
                            sType: vk::STRUCTURE_TYPE_APPLICATION_INFO,
                            pNext: ptr::null(),
                            pApplicationName: cstr!(b"Carnelian application\0").as_ptr(),
                            applicationVersion: 0,
                            pEngineName: ptr::null(),
                            engineVersion: 0,
                            apiVersion: vulkan_version!(1, 1, 0),
                        },
                        enabledLayerCount: layers.len() as u32,
                        ppEnabledLayerNames: layers.as_ptr(),
                        enabledExtensionCount: 0,
                        ppEnabledExtensionNames: ptr::null(),
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        let vk_i = instance_pointers(instance);

        let physical_devices = {
            let mut len = unsafe {
                init(|ptr| vk!(vk_i.EnumeratePhysicalDevices(instance, ptr, ptr::null_mut())))
            };

            let mut physical_devices: Vec<vk::PhysicalDevice> = Vec::with_capacity(len as usize);
            unsafe {
                vk!(vk_i.EnumeratePhysicalDevices(
                    instance,
                    &mut len,
                    physical_devices.as_mut_ptr()
                ));
                physical_devices.set_len(len as usize);
            }

            physical_devices
        };

        // TODO: Ability to select physical device.
        let physical_device = *physical_devices.get(0).expect("no physical device found");

        let physical_device_properties =
            unsafe { init(|ptr| vk_i.GetPhysicalDeviceProperties(physical_device, ptr)) };

        let physical_device_memory_properties =
            unsafe { init(|ptr| vk_i.GetPhysicalDeviceMemoryProperties(physical_device, ptr)) };

        println!(
            "{:X} : {:X} : {:?}",
            physical_device_properties.vendorID,
            physical_device_properties.deviceID,
            unsafe { CStr::from_ptr(physical_device_properties.deviceName.as_ptr()) }
                .to_str()
                .unwrap()
        );

        let format = {
            macro_rules! with_name {
                ($var:tt) => {
                    (vk::$var, stringify!($var))
                };
            }

            static FORMATS: &'static [(vk::Format, &str)] = &[
                with_name!(FORMAT_B8G8R8A8_SRGB),
                with_name!(FORMAT_R8G8B8A8_SRGB),
                with_name!(FORMAT_B8G8R8A8_UNORM),
                with_name!(FORMAT_R8G8B8A8_UNORM),
            ];

            let mut preferred_format = vk::FORMAT_UNDEFINED;
            let mut preferred_format_features = 0;

            for format in FORMATS {
                let properties = unsafe {
                    init(|ptr| {
                        vk_i.GetPhysicalDeviceFormatProperties(physical_device, format.0, ptr)
                    })
                };

                // Storage is required.
                if properties.optimalTilingFeatures & vk::FORMAT_FEATURE_STORAGE_IMAGE_BIT == 0 {
                    continue;
                }

                if preferred_format == vk::FORMAT_UNDEFINED {
                    preferred_format = format.0;
                    preferred_format_features = properties.optimalTilingFeatures;
                }

                // Prefer formats that support sampling.
                if (properties.optimalTilingFeatures & vk::FORMAT_FEATURE_SAMPLED_IMAGE_BIT != 0)
                    && (preferred_format_features & vk::FORMAT_FEATURE_SAMPLED_IMAGE_BIT == 0)
                {
                    preferred_format = format.0;
                    preferred_format_features = properties.optimalTilingFeatures;
                }

                println!("{}{}", format.1, if preferred_format == format.0 { " *" } else { "" });
            }

            assert_ne!(preferred_format, vk::FORMAT_UNDEFINED);

            preferred_format
        };

        let (spn_target, hs_target) = unsafe {
            let mut spn_target = MaybeUninit::uninit();
            let mut hs_target = MaybeUninit::uninit();
            let result = spn_vk_find_target(
                physical_device_properties.vendorID,
                physical_device_properties.deviceID,
                spn_target.as_mut_ptr(),
                hs_target.as_mut_ptr(),
                ptr::null_mut(),
                0,
            );
            assert_eq!(result, true);
            (spn_target.assume_init(), hs_target.assume_init())
        };

        let mut spn_target_requirements = unsafe {
            let mut requirements = MaybeUninit::zeroed();
            let result = spn_vk_target_get_requirements(spn_target, requirements.as_mut_ptr());
            assert_eq!(result, SpnResult::SpnErrorPartialTargetRequirements);
            requirements.assume_init()
        };
        let mut hs_target_requirements = unsafe {
            let mut requirements = MaybeUninit::zeroed();
            let result = hotsort_vk_target_get_requirements(hs_target, requirements.as_mut_ptr());
            assert_eq!(result, false);
            requirements.assume_init()
        };

        let mut extension_names = vec![
            cstr!(b"VK_FUCHSIA_external_memory\0").as_ptr(),
            cstr!(b"VK_FUCHSIA_buffer_collection\0").as_ptr(),
        ];
        extension_names.reserve(
            spn_target_requirements.ext_name_count as usize
                + hs_target_requirements.ext_name_count as usize,
        );

        let mut features_len = 0;
        unsafe {
            spn_vk_target_get_feature_structures(spn_target, &mut features_len, ptr::null_mut());
        }

        #[repr(C)]
        #[allow(non_snake_case)]
        struct VkBaseOutStructure {
            _sType: u32,
            _pNext: *const c_void,
        }

        let mut features: Vec<VkBaseOutStructure> =
            // + 1 in case features_len % mem::size_of::<VkBaseOutStructure>() != 0.
            Vec::with_capacity(features_len / mem::size_of::<VkBaseOutStructure>() + 1);

        unsafe {
            spn_vk_target_get_feature_structures(
                spn_target,
                &mut features_len,
                features.as_mut_ptr() as *mut c_void,
            );
        }

        let mut physical_device_features2 = PhysicalDeviceFeatures2 {
            sType: STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            pNext: features.as_mut_ptr() as *mut c_void,
            features: unsafe { MaybeUninit::zeroed().assume_init() },
        };

        let mut queue_create_infos = Vec::with_capacity(spn_target_requirements.qci_count as usize);

        spn_target_requirements.qcis = queue_create_infos.as_mut_ptr();
        spn_target_requirements.ext_names = if spn_target_requirements.ext_name_count > 0 {
            unsafe { extension_names.as_mut_ptr().add(2) }
        } else {
            ptr::null_mut()
        };
        spn_target_requirements.pdf2 = &mut physical_device_features2;

        unsafe {
            spn!(spn_vk_target_get_requirements(spn_target, &mut spn_target_requirements));
        }

        hs_target_requirements.ext_names = if hs_target_requirements.ext_name_count > 0 {
            unsafe {
                extension_names
                    .as_mut_ptr()
                    .add(2 + spn_target_requirements.ext_name_count as usize)
            }
        } else {
            ptr::null_mut()
        };
        hs_target_requirements.pdf = &mut physical_device_features2.features;

        unsafe {
            let result = hotsort_vk_target_get_requirements(hs_target, &mut hs_target_requirements);
            assert_eq!(result, true);
        };

        unsafe {
            extension_names.set_len(
                2 + spn_target_requirements.ext_name_count as usize
                    + hs_target_requirements.ext_name_count as usize,
            );
            queue_create_infos.set_len(spn_target_requirements.qci_count as usize);
        }

        let device = unsafe {
            init(|ptr| {
                vk!(vk_i.CreateDevice(
                    physical_device,
                    &vk::DeviceCreateInfo {
                        sType: vk::STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                        pNext: physical_device_features2.pNext,
                        flags: 0,
                        queueCreateInfoCount: queue_create_infos.len() as u32,
                        pQueueCreateInfos: queue_create_infos.as_ptr(),
                        enabledLayerCount: 0,
                        ppEnabledLayerNames: ptr::null(),
                        enabledExtensionCount: extension_names.len() as u32,
                        ppEnabledExtensionNames: extension_names.as_ptr(),
                        pEnabledFeatures: &physical_device_features2.features,
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        let vk = device_pointers(&vk_i, device);
        let vk_ext = FuchsiaExtensionPointers::load(|name| unsafe {
            vk_i.GetDeviceProcAddr(device, name.as_ptr()) as *const _
        });

        let buffer_collection = unsafe {
            init(|ptr| {
                vk!(vk_ext.CreateBufferCollectionFUCHSIA(
                    device,
                    &BufferCollectionCreateInfoFUCHSIA {
                        sType: STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
                        pNext: ptr::null(),
                        collectionToken: token.into_channel().into_raw(),
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        unsafe {
            vk!(vk_ext.SetBufferCollectionConstraintsFUCHSIA(
                device,
                buffer_collection,
                &image_create_info(
                    size.width,
                    size.height,
                    format,
                    vk::IMAGE_TILING_OPTIMAL,
                    ptr::null(),
                )
            ));
        }

        let descriptor_pool = unsafe {
            init(|ptr| {
                vk!(vk.CreateDescriptorPool(
                    device,
                    &vk::DescriptorPoolCreateInfo {
                        sType: vk::STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: vk::DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                        maxSets: DESCRIPTOR_SET_COUNT as u32,
                        poolSizeCount: 1,
                        pPoolSizes: &vk::DescriptorPoolSize {
                            ty: vk::DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            descriptorCount: 2 * DESCRIPTOR_SET_COUNT as u32,
                        },
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        let descriptor_set_layout = unsafe {
            let bindings = [
                vk::DescriptorSetLayoutBinding {
                    binding: 0,
                    descriptorType: vk::DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    descriptorCount: 1,
                    stageFlags: vk::SHADER_STAGE_COMPUTE_BIT,
                    pImmutableSamplers: ptr::null(),
                },
                vk::DescriptorSetLayoutBinding {
                    binding: 1,
                    descriptorType: vk::DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    descriptorCount: 1,
                    stageFlags: vk::SHADER_STAGE_COMPUTE_BIT,
                    pImmutableSamplers: ptr::null(),
                },
            ];

            init(|ptr| {
                vk!(vk.CreateDescriptorSetLayout(
                    device,
                    &vk::DescriptorSetLayoutCreateInfo {
                        sType: vk::STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                        pNext: ptr::null(),
                        flags: 0,
                        bindingCount: bindings.len() as u32,
                        pBindings: bindings.as_ptr(),
                    },
                    ptr::null(),
                    ptr,
                ))
            })
        };

        let descriptor_sets = unsafe {
            let layouts = [descriptor_set_layout, descriptor_set_layout];

            let mut descriptor_sets = Vec::with_capacity(DESCRIPTOR_SET_COUNT);
            vk!(vk.AllocateDescriptorSets(
                device,
                &vk::DescriptorSetAllocateInfo {
                    sType: vk::STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    pNext: ptr::null(),
                    descriptorPool: descriptor_pool,
                    descriptorSetCount: DESCRIPTOR_SET_COUNT as u32,
                    pSetLayouts: layouts.as_ptr(),
                },
                descriptor_sets.as_mut_ptr(),
            ));
            descriptor_sets.set_len(DESCRIPTOR_SET_COUNT as usize);
            descriptor_sets
        };

        let mut environment = Box::new(SpnVkEnvironment {
            d: device,
            ac: ptr::null(),
            pc: 0,
            pd: physical_device,
            qfi: 0,
            pdmp: physical_device_memory_properties,
        });

        let create_info = SpnVkContextCreateInfo {
            block_pool_size: config.block_pool_size,
            handle_count: config.handle_count,
            spinel: spn_target,
            hotsort: hs_target,
        };

        let context = unsafe {
            init(|ptr| spn!(spn_vk_context_create(&mut *environment, &create_info, ptr)))
        };

        let inner = Rc::new(RefCell::new(InnerContext { context, is_dropped: false }));
        let path_builder = Rc::new(unsafe {
            init(|ptr| spn!(spn_path_builder_create(inner.borrow().get(), ptr)))
        });
        let raster_builder = Rc::new(unsafe {
            init(|ptr| spn!(spn_raster_builder_create(inner.borrow().get(), ptr)))
        });

        Self {
            inner,
            path_builder,
            raster_builder,
            compositions: HashMap::new(),
            vulkan: VulkanContext {
                device,
                vk_i,
                vk,
                vk_ext,
                buffer_collection,
                width: size.width,
                height: size.height,
                format,
                descriptor_pool,
                descriptor_set_layout,
                descriptor_sets,
                copy_shader: None,
                motioncopy_shader: None,
            },
            images: vec![],
            index_map: HashMap::new(),
            image_post_copy_regions: vec![],
        }
    }
}

impl fmt::Debug for SpinelContext {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.debug_struct("SpinelContex")
            .field("inner", &self.inner)
            .field("path_builder", &self.path_builder)
            .field("raster_builder", &self.raster_builder)
            .field("vulkan", &self.vulkan)
            .field("images", &self.images)
            .field(
                "image_post_copy_regions",
                &self.image_post_copy_regions.iter().map(|_| "ImageCopy").collect::<Vec<_>>(),
            )
            .finish()
    }
}

impl Context<Spinel> for SpinelContext {
    fn pixel_format(&self) -> PixelFormat {
        match self.vulkan.format {
            vk::FORMAT_R8G8B8A8_UNORM => fuchsia_framebuffer::PixelFormat::BgrX888,
            _ => fuchsia_framebuffer::PixelFormat::RgbX888,
        }
    }

    fn path_builder(&self) -> Option<SpinelPathBuilder> {
        if Rc::strong_count(&self.path_builder) == 1 {
            unsafe {
                spn!(spn_path_builder_begin(*self.path_builder));
            }

            Some(SpinelPathBuilder {
                context: Rc::clone(&self.inner),
                path_builder: Rc::clone(&self.path_builder),
            })
        } else {
            None
        }
    }

    fn raster_builder(&self) -> Option<SpinelRasterBuilder> {
        Some(SpinelRasterBuilder { paths: Vec::new() })
    }

    fn new_image(&mut self, size: Size2D<u32>) -> SpinelImage {
        let vulkan = &self.vulkan;
        let image = SpinelImage(self.images.len());
        self.images.push(VulkanImage::new(
            vulkan.device,
            &vulkan.vk_i,
            size.width,
            size.height,
            vulkan.format,
            image,
        ));

        image
    }

    fn new_image_from_png<R: Read>(
        &mut self,
        reader: &mut png::Reader<R>,
    ) -> Result<SpinelImage, Error> {
        let vulkan = &self.vulkan;
        let image = SpinelImage(self.images.len());
        self.images.push(VulkanImage::from_png(
            vulkan.device,
            &vulkan.vk_i,
            vulkan.format,
            reader,
            image,
        )?);

        Ok(image)
    }

    fn get_image(&mut self, image_index: u32) -> SpinelImage {
        let vulkan = &self.vulkan;
        let images = &mut self.images;

        let index = self.index_map.entry(image_index).or_insert_with(|| {
            let index = images.len();
            images.push(VulkanImage::from_buffer_collection(
                vulkan.device,
                &vulkan.vk_i,
                &vulkan.vk_ext,
                vulkan.width,
                vulkan.height,
                vulkan.format,
                vulkan.buffer_collection,
                image_index,
                SpinelImage(index),
            ));

            index
        });

        SpinelImage(*index)
    }

    fn get_current_image(&mut self, context: &ViewAssistantContext) -> SpinelImage {
        self.get_image(context.image_index)
    }

    fn render_with_clip(
        &mut self,
        composition: &SpinelComposition,
        clip: Rect<u32>,
        image: SpinelImage,
        ext: &RenderExt<Spinel>,
    ) {
        let image_id = image;
        let image =
            self.images.get(image.0 as usize).expect(&format!("invalid image {:?}", image_id));

        let mut rs_image_pre_barrier = SpnVkRenderSubmitExtImagePreBarrier {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePreBarrier,
            old_layout: vk::IMAGE_LAYOUT_UNDEFINED,
            src_qfi: vk::QUEUE_FAMILY_IGNORED,
        };
        let mut rs_clear_color = vk::ClearColorValue { float32: [0.0; 4] };
        let mut rs_image_clear = SpnVkRenderSubmitExtImagePreClear {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePreClear,
            color: &rs_clear_color,
        };
        let mut rs_image_pre_process_params = CopyParams::default();
        let mut rs_image_pre_process = SpnVkRenderSubmitExtImageProcess {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePreProcess,
            access_mask: vk::ACCESS_SHADER_READ_BIT | vk::ACCESS_SHADER_WRITE_BIT,
            pipeline: 0,
            pipeline_layout: 0,
            descriptor_set_count: 0,
            descriptor_sets: ptr::null(),
            push_offset: 0,
            push_size: mem::size_of::<CopyParams>() as u32,
            push_values: &rs_image_pre_process_params as *const _ as *const c_void,
            group_count_x: 1,
            group_count_y: 1,
            group_count_z: 1,
        };
        let mut rs_image_post_process_params = MotionCopyParams::default();
        let mut rs_image_post_process = SpnVkRenderSubmitExtImageProcess {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePostProcess,
            access_mask: vk::ACCESS_SHADER_READ_BIT,
            pipeline: 0,
            pipeline_layout: 0,
            descriptor_set_count: 0,
            descriptor_sets: ptr::null(),
            push_offset: 0,
            push_size: mem::size_of::<MotionCopyParams>() as u32,
            push_values: &rs_image_post_process_params as *const _ as *const c_void,
            group_count_x: 1,
            group_count_y: 1,
            group_count_z: 1,
        };
        let mut rs_image_post_copy = SpnVkRenderSubmitExtImagePostCopyToImage {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePostCopyToImage,
            dst: 0,
            dst_layout: vk::IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            region_count: 0,
            regions: ptr::null(),
        };
        let mut rs_image_post_barrier = SpnVkRenderSubmitExtImagePostBarrier {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePostBarrier,
            new_layout: vk::IMAGE_LAYOUT_UNDEFINED,
            dst_qfi: vk::QUEUE_FAMILY_IGNORED,
        };
        let mut rs_image_ext = ptr::null_mut();
        let mut post_image_layout = vk::IMAGE_LAYOUT_PRESENT_SRC_KHR;

        if image.layout() != vk::IMAGE_LAYOUT_GENERAL {
            rs_image_pre_barrier.old_layout = image.layout();
            rs_image_pre_barrier.ext = rs_image_ext;
            rs_image_ext = &mut rs_image_pre_barrier as *mut _ as *mut c_void;
        }

        if let Some(PreClear { color }) = ext.pre_clear {
            rs_clear_color.float32 = color.to_linear_premult_rgba();
            rs_image_clear.ext = rs_image_ext;
            rs_image_ext = &mut rs_image_clear as *mut _ as *mut c_void;
        }

        let vk = &self.vulkan.vk;
        let device = self.vulkan.device;
        let descriptor_set_layout = self.vulkan.descriptor_set_layout;

        if let Some(PreCopy { image: src_image_id, copy_region }) = ext.pre_copy {
            let copy_shader = self.vulkan.copy_shader.get_or_insert_with(|| {
                VulkanShader::new(
                    vk,
                    device,
                    descriptor_set_layout,
                    "copy",
                    mem::size_of::<CopyParams>(),
                )
            });
            rs_image_pre_process.pipeline = copy_shader.pipeline;
            rs_image_pre_process.pipeline_layout = copy_shader.pipeline_layout;

            let src_image = self
                .images
                .get(src_image_id.0 as usize)
                .expect(&format!("invalid PreCopy image {:?}", src_image_id));

            update_descriptor_set(
                &self.vulkan.vk,
                device,
                src_image,
                image,
                self.vulkan.descriptor_sets[DESCRIPTOR_SET_PRE_PROCESS],
            );
            rs_image_pre_process.descriptor_set_count = 1;
            rs_image_pre_process.descriptor_sets =
                &self.vulkan.descriptor_sets[DESCRIPTOR_SET_PRE_PROCESS];

            // TODO: Combined X+Y offset support.
            if copy_region.dst_offset.x != copy_region.src_offset.x
                && copy_region.dst_offset.y != copy_region.src_offset.y
            {
                panic!(
                    "dst_offset/src_offset x and dst_offset/src_offset y offset cannot both change"
                );
            }

            let mut extent = None;
            let mut groups = 0;
            for i in 0..2 {
                let src_offset = copy_region.src_offset.to_array();
                let dst_offset = copy_region.dst_offset.to_array();
                let region_extent = copy_region.extent.to_array();

                match dst_offset[i].cmp(&src_offset[i]) {
                    Ordering::Less => {
                        // Copy forward.
                        rs_image_pre_process_params.src_offset[i] = src_offset[i] as i32;
                        rs_image_pre_process_params.dst_offset[i] = dst_offset[i] as i32;
                        rs_image_pre_process_params.dxdy[i] = 1;
                        extent = Some(region_extent[i]);
                    }
                    Ordering::Equal => {
                        // Copy direction is not important.
                        rs_image_pre_process_params.src_offset[i] = src_offset[i] as i32;
                        rs_image_pre_process_params.dst_offset[i] = dst_offset[i] as i32;
                        rs_image_pre_process_params.dxdy[i] = 0;
                        groups = region_extent[i];
                    }
                    Ordering::Greater => {
                        // Copy backwards.
                        rs_image_pre_process_params.src_offset[i] =
                            (src_offset[i] + region_extent[i]) as i32 - 1;
                        rs_image_pre_process_params.dst_offset[i] =
                            (dst_offset[i] + region_extent[i]) as i32 - 1;
                        rs_image_pre_process_params.dxdy[i] = -1;
                        extent = Some(region_extent[i]);
                    }
                }
            }

            let extent = extent.unwrap_or_else(|| {
                // Copy rows forward if direction is not important for either dimension.
                rs_image_pre_process_params.dxdy[0] = 1;
                groups = copy_region.extent.height;
                copy_region.extent.width
            });

            rs_image_pre_process_params.extent = extent;
            const LOCAL_SIZE_X: u32 = 48;
            // TODO: Clip output to extent instead of rounding up.
            rs_image_pre_process.group_count_x = (groups + LOCAL_SIZE_X - 1) / LOCAL_SIZE_X;
            rs_image_pre_process.ext = rs_image_ext;
            rs_image_ext = &mut rs_image_pre_process as *mut _ as *mut c_void;
        }

        if let Some(PostCopy { image: dst_image_id, color, exposure_distance, copy_region }) =
            ext.post_copy
        {
            if dst_image_id == image_id {
                panic!("PostCopy.image must be different from the render image");
            }

            if exposure_distance.x.abs() > 1 || exposure_distance.y.abs() > 1 {
                let motioncopy_shader_name = match self.vulkan.format {
                    vk::FORMAT_B8G8R8A8_SRGB | vk::FORMAT_R8G8B8A8_SRGB => "motioncopy-srgb",
                    vk::FORMAT_B8G8R8A8_UNORM | vk::FORMAT_R8G8B8A8_UNORM => "motioncopy-unorm",
                    _ => panic!("Unsupported image format {}", self.vulkan.format),
                };
                let motioncopy_shader = self.vulkan.motioncopy_shader.get_or_insert_with(|| {
                    VulkanShader::new(
                        vk,
                        device,
                        descriptor_set_layout,
                        motioncopy_shader_name,
                        mem::size_of::<MotionCopyParams>(),
                    )
                });
                rs_image_post_process.pipeline = motioncopy_shader.pipeline;
                rs_image_post_process.pipeline_layout = motioncopy_shader.pipeline_layout;

                let dst_image = self
                    .images
                    .get(dst_image_id.0 as usize)
                    .expect(&format!("invalid PostCopy image {:?}", dst_image_id));
                if dst_image.layout() != vk::IMAGE_LAYOUT_GENERAL {
                    dst_image.layout_transition(
                        vk::IMAGE_LAYOUT_GENERAL,
                        vk::PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        vk::ACCESS_SHADER_WRITE_BIT,
                    );
                }

                update_descriptor_set(
                    &self.vulkan.vk,
                    device,
                    image,
                    dst_image,
                    self.vulkan.descriptor_sets[DESCRIPTOR_SET_POST_PROCESS],
                );
                rs_image_post_process.descriptor_set_count = 1;
                rs_image_post_process.descriptor_sets =
                    &self.vulkan.descriptor_sets[DESCRIPTOR_SET_POST_PROCESS];

                // TODO: Combined X+Y exposure support.
                if exposure_distance.x != 0 && exposure_distance.y != 0 {
                    panic!("x and y exposure_distance cannot be used at the same time");
                }

                let mut extent = None;
                let mut groups = 0;
                for i in 0..2 {
                    let src_offset = copy_region.src_offset.to_array();
                    let dst_offset = copy_region.dst_offset.to_array();
                    let region_extent = copy_region.extent.to_array();
                    let exposure_distance = exposure_distance.to_array();

                    match exposure_distance[i].cmp(&0) {
                        Ordering::Less => {
                            // Copy forward.
                            rs_image_post_process_params.src_offset[i] = src_offset[i] as i32;
                            rs_image_post_process_params.dst_offset[i] = dst_offset[i] as i32;
                            rs_image_post_process_params.dxdy[i] = 1;
                            extent = Some(region_extent[i]);
                        }
                        Ordering::Equal => {
                            // Copy direction is not important.
                            rs_image_post_process_params.src_offset[i] = src_offset[i] as i32;
                            rs_image_post_process_params.dst_offset[i] = dst_offset[i] as i32;
                            rs_image_post_process_params.dxdy[i] = 0;
                            groups = region_extent[i]
                        }
                        Ordering::Greater => {
                            // Copy backwards.
                            rs_image_post_process_params.src_offset[i] =
                                (src_offset[i] + region_extent[i]) as i32 - 1;
                            rs_image_post_process_params.dst_offset[i] =
                                (dst_offset[i] + region_extent[i]) as i32 - 1;
                            rs_image_post_process_params.dxdy[i] = -1;
                            extent = Some(region_extent[i]);
                        }
                    }
                }

                let extent = extent.unwrap_or_else(|| {
                    // Copy rows forward if direction is not important for either dimension.
                    rs_image_post_process_params.dxdy[0] = 1;
                    groups = copy_region.extent.height;
                    copy_region.extent.width
                });
                let exposure_amount =
                    extent.min((exposure_distance.x.abs() + exposure_distance.y.abs()) as u32);

                rs_image_post_process_params.exposure = exposure_amount;
                rs_image_post_process_params.extent = extent - exposure_amount;
                rs_image_post_process_params.src_dims =
                    [image.width() as i32, image.height() as i32];
                rs_image_post_process_params.border = color.to_linear_premult_rgba();
                const LOCAL_SIZE_X: u32 = 48;
                // TODO: Clip output to extent instead of rounding up.
                rs_image_post_process.group_count_x = (groups + LOCAL_SIZE_X - 1) / LOCAL_SIZE_X;
                rs_image_post_process.ext = rs_image_ext;
                rs_image_ext = &mut rs_image_post_process as *mut _ as *mut c_void;
            } else {
                let mut src_y = copy_region.src_offset.y;
                let mut dst_y = copy_region.dst_offset.y;
                let mut height = copy_region.extent.height;

                self.image_post_copy_regions.clear();

                while height > 0 {
                    let rows = height.min(image.height() - src_y);
                    let mut width = copy_region.extent.width;
                    let mut src_x = copy_region.src_offset.x;
                    let mut dst_x = copy_region.dst_offset.x;

                    while width > 0 {
                        let columns = width.min(image.width() - src_x);

                        self.image_post_copy_regions.push(vk::ImageCopy {
                            srcSubresource: vk::ImageSubresourceLayers {
                                aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
                                mipLevel: 0,
                                baseArrayLayer: 0,
                                layerCount: 1,
                            },
                            srcOffset: vk::Offset3D { x: src_x as i32, y: src_y as i32, z: 0 },
                            dstSubresource: vk::ImageSubresourceLayers {
                                aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
                                mipLevel: 0,
                                baseArrayLayer: 0,
                                layerCount: 1,
                            },
                            dstOffset: vk::Offset3D { x: dst_x as i32, y: dst_y as i32, z: 0 },
                            extent: vk::Extent3D { width: columns, height: rows, depth: 1 },
                        });

                        width -= columns;
                        dst_x += columns;
                        src_x = 0;
                    }

                    height -= rows;
                    dst_y += rows;
                    src_y = 0;
                }

                let dst_image = self
                    .images
                    .get(dst_image_id.0 as usize)
                    .expect(&format!("invalid PostCopy image {:?}", dst_image_id));
                if dst_image.layout() != vk::IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL {
                    dst_image.layout_transition(
                        vk::IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        vk::PIPELINE_STAGE_TRANSFER_BIT,
                        vk::ACCESS_TRANSFER_WRITE_BIT,
                    );
                }

                rs_image_post_copy.region_count = self.image_post_copy_regions.len() as u32;
                rs_image_post_copy.regions = self.image_post_copy_regions.as_ptr();
                rs_image_post_copy.dst = dst_image.image();
                rs_image_post_copy.ext = rs_image_ext;
                rs_image_ext = &mut rs_image_post_copy as *mut _ as *mut c_void;
            }

            post_image_layout = vk::IMAGE_LAYOUT_GENERAL;
        }

        if post_image_layout != vk::IMAGE_LAYOUT_GENERAL {
            rs_image_post_barrier.new_layout = post_image_layout;
            rs_image_post_barrier.ext = rs_image_ext;
            rs_image_ext = &mut rs_image_post_barrier as *mut _ as *mut c_void;
        }
        image.set_layout(post_image_layout);

        let mut rs_image = image.rs_image(rs_image_ext);

        let spn_context = self.inner.borrow().get();
        let spn_composition = *self.compositions.entry(image_id).or_insert_with(|| unsafe {
            init(|ptr| spn!(spn_composition_create(spn_context, ptr)))
        });
        composition.set_up_spn_composition(
            &*self.inner.borrow(),
            *self.raster_builder,
            spn_composition,
            clip,
        );
        let spn_styling = composition
            .spn_styling(&*self.inner.borrow())
            .expect("SpinelComposition::spn_styling called from outside SpinelContext::render");

        let rs = SpnRenderSubmit {
            ext: &mut rs_image as *mut _ as *mut c_void,
            styling: spn_styling,
            composition: spn_composition,
            clip: [clip.min_x(), clip.min_y(), clip.max_x(), clip.max_y()],
        };

        unsafe {
            spn!(spn_composition_seal(spn_composition));
            spn!(spn_render(spn_context, &rs));
            // Unsealing the composition will block until rendering has completed.
            // TODO(reveman): Use fence instead of blocking.
            spn!(spn_composition_unseal(spn_composition));
            spn!(spn_styling_release(spn_styling));
        }

        if trace::category_enabled(cstr!(b"spinel:block-pool\0")) {
            let mut block_pool = SpnVkStatusExtBlockPool {
                ext: ptr::null_mut(),
                type_: SpnVkStatusExtType::SpnVkStatusExtTypeBlockPool,
                avail: 0,
                inuse: 0,
            };
            let mut s = SpnStatus { ext: &mut block_pool as *mut _ as *mut c_void };
            unsafe {
                spn!(spn_context_status(spn_context, &mut s));
            }

            trace::counter!(
                "spinel:block-pool",
                "Spinel BlockPool",
                0,
                "avail" => (block_pool.avail / 1024) as u32,
                "inuse" => (block_pool.inuse / 1024) as u32
            );
        }
    }
}

impl Drop for SpinelContext {
    fn drop(&mut self) {
        self.inner.borrow_mut().is_dropped = true;

        // Drop all images before Vulkan teardown.
        self.images.clear();

        unsafe {
            for composition in self.compositions.values().copied() {
                spn!(spn_composition_release(composition));
            }

            assert_eq!(
                Rc::strong_count(&self.raster_builder),
                1,
                "SpinelContext dropped while SpinelRasterBuilder was still alive",
            );
            spn!(spn_raster_builder_release(*self.raster_builder));
            assert_eq!(
                Rc::strong_count(&self.path_builder),
                1,
                "SpinelContext dropped while SpinelPathBuilder was still alive",
            );
            spn!(spn_path_builder_release(*self.path_builder));
            spn!(spn_context_release(self.inner.borrow().get()));

            self.vulkan.vk_ext.DestroyBufferCollectionFUCHSIA(
                self.vulkan.device,
                self.vulkan.buffer_collection,
                ptr::null(),
            );
            self.vulkan.vk.FreeDescriptorSets(
                self.vulkan.device,
                self.vulkan.descriptor_pool,
                2,
                self.vulkan.descriptor_sets.as_ptr(),
            );
            self.vulkan.vk.DestroyDescriptorSetLayout(
                self.vulkan.device,
                self.vulkan.descriptor_set_layout,
                ptr::null(),
            );
            if let Some(shader) = self.vulkan.copy_shader.take() {
                self.vulkan.vk.DestroyPipeline(self.vulkan.device, shader.pipeline, ptr::null());
                self.vulkan.vk.DestroyPipelineLayout(
                    self.vulkan.device,
                    shader.pipeline_layout,
                    ptr::null(),
                );
            }
            if let Some(shader) = self.vulkan.motioncopy_shader.take() {
                self.vulkan.vk.DestroyPipeline(self.vulkan.device, shader.pipeline, ptr::null());
                self.vulkan.vk.DestroyPipelineLayout(
                    self.vulkan.device,
                    shader.pipeline_layout,
                    ptr::null(),
                );
            }
            self.vulkan.vk.DestroyDescriptorPool(
                self.vulkan.device,
                self.vulkan.descriptor_pool,
                ptr::null(),
            );
            self.vulkan.vk.DestroyDevice(self.vulkan.device, ptr::null());
        }
    }
}
