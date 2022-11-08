// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem as fsysmem;
use fidl_fuchsia_ui_composition as fuicomp;
use fuchsia_image_format::*;
use fuchsia_vulkan::*;
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;
use std::mem;
use vk_sys as vk;

/// `BufferCollectionTokens` contains all the buffer collection tokens required to initialize a
/// `Loader`.
///
/// The `buffer_token_proxy` is expected to be the source (via `duplicate`) of both the
/// `scenic_token` as well as the `vulkan_token`.
pub struct BufferCollectionTokens {
    /// The buffer collection token that is the source of the scenic and vulkan tokens.
    pub buffer_token_proxy: fsysmem::BufferCollectionTokenSynchronousProxy,

    /// The buffer collection token that is handed off to scenic.
    pub scenic_token: Option<ClientEnd<fsysmem::BufferCollectionTokenMarker>>,

    /// The buffer collection token that is handed off to vulkan.
    pub vulkan_token: ClientEnd<fsysmem::BufferCollectionTokenMarker>,
}

/// `Loader` stores all the interfaces/entry points for the created devices and instances.
pub struct Loader {
    /// The vulkan instance pointers associated with this loader. These are fetched at loader
    /// creation time.
    instance_pointers: vk::InstancePointers,

    /// The instance that is created for this loader. Not currently used after the loader has been
    /// created.
    _instance: vk::Instance,

    /// The physical device associated with this loader. Not currently used after the loader has
    /// been created.
    _physical_device: vk::PhysicalDevice,

    /// The device that is created by this Loader.
    device: vk::Device,
}

impl Loader {
    /// Creates a new `Loader` by creating a Vulkan instance and device, with the provided physical
    /// device index.
    pub fn new(physical_device_index: u32) -> Result<Loader, vk::Result> {
        let entry_points = entry_points();
        let application_info = app_info();
        let instance_info = instance_info(&application_info);

        let mut instance: usize = 0;
        let result = unsafe {
            entry_points.CreateInstance(
                &instance_info,
                std::ptr::null(),
                &mut instance as *mut vk::Instance,
            )
        };
        if result != vk::SUCCESS {
            return Err(result);
        }

        let instance_pointers = instance_pointers(instance);
        let mut device_count: u32 = 0;
        if unsafe {
            instance_pointers.EnumeratePhysicalDevices(
                instance,
                &mut device_count as *mut u32,
                std::ptr::null_mut(),
            )
        } != vk::SUCCESS
        {
            return Err(vk::ERROR_INITIALIZATION_FAILED);
        }

        let mut physical_devices: Vec<vk::PhysicalDevice> = vec![0; device_count as usize];
        if unsafe {
            instance_pointers.EnumeratePhysicalDevices(
                instance,
                &mut device_count as *mut u32,
                physical_devices.as_mut_ptr(),
            )
        } != vk::SUCCESS
        {
            return Err(vk::ERROR_INITIALIZATION_FAILED);
        }

        let physical_device = physical_devices[physical_device_index as usize];

        let mut queue_family_count: u32 = 0;
        unsafe {
            instance_pointers.GetPhysicalDeviceQueueFamilyProperties(
                physical_device,
                &mut queue_family_count as *mut u32,
                std::ptr::null_mut(),
            );
        };

        let mut queue_family_properties =
            Vec::<vk::QueueFamilyProperties>::with_capacity(queue_family_count as usize);
        queue_family_properties.resize_with(queue_family_count as usize, || {
            vk::QueueFamilyProperties {
                queueFlags: 0,
                queueCount: 0,
                timestampValidBits: 0,
                minImageTransferGranularity: vk::Extent3D { width: 0, height: 0, depth: 0 },
            }
        });
        unsafe {
            instance_pointers.GetPhysicalDeviceQueueFamilyProperties(
                physical_device,
                &mut queue_family_count as *mut u32,
                queue_family_properties.as_mut_ptr(),
            );
        };

        let mut queue_family_index = None;
        for (index, queue_family) in queue_family_properties.iter().enumerate() {
            if queue_family.queueFlags & vk::QUEUE_GRAPHICS_BIT != 0 {
                queue_family_index = Some(index);
            }
        }
        if queue_family_index.is_none() {
            return Err(vk::ERROR_INITIALIZATION_FAILED);
        }

        let queue_family_index = queue_family_index.unwrap();
        let queue_create_info = vk::DeviceQueueCreateInfo {
            sType: vk::STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            pNext: std::ptr::null(),
            flags: 0,
            queueFamilyIndex: queue_family_index as u32,
            queueCount: 1,
            pQueuePriorities: &0.0,
        };

        let extension_name: Vec<u8> = b"VK_FUCHSIA_buffer_collection".to_vec();
        let device_extensions = vec![extension_name.as_ptr() as *const i8];
        let device_create_info = vk::DeviceCreateInfo {
            sType: vk::STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            pNext: std::ptr::null(),
            flags: 0,
            queueCreateInfoCount: 1,
            pQueueCreateInfos: &queue_create_info as *const vk::DeviceQueueCreateInfo,
            enabledLayerCount: 0,
            ppEnabledLayerNames: std::ptr::null(),
            enabledExtensionCount: device_extensions.len() as u32,
            ppEnabledExtensionNames: device_extensions.as_ptr(),
            pEnabledFeatures: std::ptr::null(),
        };

        let mut device: usize = 0;
        let result = unsafe {
            instance_pointers.CreateDevice(
                physical_device,
                &device_create_info as *const vk::DeviceCreateInfo,
                std::ptr::null(),
                &mut device as *mut usize,
            )
        };
        if result != vk::SUCCESS {
            return Err(result);
        }

        Ok(Loader {
            instance_pointers,
            _instance: instance,
            _physical_device: physical_device,
            device,
        })
    }

    pub fn is_intel_device(&self) -> bool {
        unsafe {
            let mut props: vk::PhysicalDeviceProperties = mem::zeroed();
            self.instance_pointers.GetPhysicalDeviceProperties(self._physical_device, &mut props);
            props.vendorID == 0x8086
        }
    }

    pub fn get_format_features(
        &self,
        format: vk::Format,
        linear_tiling: bool,
    ) -> vk::FormatFeatureFlags {
        let mut format_properties = vk::FormatProperties {
            linearTilingFeatures: 0,
            optimalTilingFeatures: 0,
            bufferFeatures: 0,
        };
        unsafe {
            self.instance_pointers.GetPhysicalDeviceFormatProperties(
                self._physical_device,
                format,
                &mut format_properties,
            );
        }
        if linear_tiling {
            format_properties.linearTilingFeatures
        } else {
            format_properties.optimalTilingFeatures
        }
    }

    /// Creates a new `BufferCollection` and returns the image import endpoint for the collection,
    /// as well as an active proxy to the collection.
    ///
    /// Returns an error if creating the collection fails.
    pub fn create_collection(
        &self,
        extent: vk::Extent2D,
        image_constraints_info: &ImageConstraintsInfoFUCHSIA,
        pixel_format: fsysmem::PixelFormatType,
        modifiers: &[u64],
        tokens: BufferCollectionTokens,
        scenic_allocator: &Option<fuicomp::AllocatorSynchronousProxy>,
        sysmem_allocator: &fsysmem::AllocatorSynchronousProxy,
    ) -> Result<
        (Option<fuicomp::BufferCollectionImportToken>, fsysmem::BufferCollectionSynchronousProxy),
        vk::Result,
    > {
        let scenic_import_token = if let Some(allocator) = &scenic_allocator {
            Some(register_buffer_collection_with_scenic(
                tokens.scenic_token.ok_or(vk::ERROR_INITIALIZATION_FAILED).unwrap(),
                allocator,
            )?)
        } else {
            None
        };

        let vk_ext = FuchsiaExtensionPointers::load(|name| unsafe {
            self.instance_pointers.GetDeviceProcAddr(self.device, name.as_ptr()) as *const _
        });

        let mut collection: BufferCollectionFUCHSIA = 0;
        let result = unsafe {
            vk_ext.CreateBufferCollectionFUCHSIA(
                self.device,
                &BufferCollectionCreateInfoFUCHSIA {
                    sType: STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
                    pNext: std::ptr::null(),
                    collectionToken: tokens.vulkan_token.as_handle_ref().raw_handle(),
                },
                std::ptr::null(),
                &mut collection as *mut BufferCollectionFUCHSIA,
            )
        };
        if result != vk::SUCCESS {
            return Err(result);
        }
        assert!(collection != 0);

        let result = unsafe {
            vk_ext.SetBufferCollectionImageConstraintsFUCHSIA(
                self.device,
                collection,
                image_constraints_info as *const ImageConstraintsInfoFUCHSIA,
            )
        };
        if result != vk::SUCCESS {
            return Err(result);
        }

        let (client, remote) =
            fidl::endpoints::create_endpoints::<fsysmem::BufferCollectionMarker>()
                .expect("Failed to create buffer collection endpoints.");

        sysmem_allocator
            .bind_shared_collection(tokens.buffer_token_proxy.into_channel().into(), remote)
            .map_err(|_| vk::ERROR_INITIALIZATION_FAILED)?;
        let buffer_collection =
            fsysmem::BufferCollectionSynchronousProxy::new(client.into_channel());

        let mut constraints = buffer_collection_constraints();
        constraints.image_format_constraints_count = modifiers.len() as u32;

        for (index, modifier) in modifiers.iter().enumerate() {
            let mut image_constraints = fsysmem::ImageFormatConstraints {
                min_coded_width: extent.width,
                min_coded_height: extent.height,
                max_coded_width: extent.width,
                max_coded_height: extent.height,
                min_bytes_per_row: 0,
                color_spaces_count: 1,
                ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
            };

            image_constraints.pixel_format.type_ = pixel_format;
            image_constraints.pixel_format.has_format_modifier = true;
            image_constraints.pixel_format.format_modifier.value = *modifier;
            image_constraints.color_space[0].type_ = fsysmem::ColorSpaceType::Srgb;

            constraints.image_format_constraints[index] = image_constraints;
        }

        buffer_collection
            .set_constraints(true, &mut constraints)
            .map_err(|_| vk::ERROR_INITIALIZATION_FAILED)?;

        Ok((scenic_import_token, buffer_collection))
    }
}

/// Returns a `u32` encoding the provided vulkan version.
macro_rules! vulkan_version {
    ( $major:expr, $minor:expr, $patch:expr ) => {
        ($major as u32) << 22 | ($minor as u32) << 12 | ($patch as u32)
    };
}

/// Creates a `CStr` from the provided `bytes`.
macro_rules! cstr {
    ( $bytes:expr ) => {
        ::std::ffi::CStr::from_bytes_with_nul($bytes).expect("CStr must end with '\\0'")
    };
}

/// Returns the default buffer collection constraints set on the buffer collection.
///
/// The returned buffer collection constraints are modified by the caller to contain the appropriate
/// image format constraints before being set on the collection.
pub fn buffer_collection_constraints() -> fsysmem::BufferCollectionConstraints {
    let usage = fsysmem::BufferUsage {
        cpu: fsysmem::CPU_USAGE_READ_OFTEN | fsysmem::CPU_USAGE_WRITE_OFTEN,
        ..BUFFER_USAGE_DEFAULT
    };

    let buffer_memory_constraints = fsysmem::BufferMemoryConstraints {
        ram_domain_supported: true,
        cpu_domain_supported: true,
        ..BUFFER_MEMORY_CONSTRAINTS_DEFAULT
    };

    fsysmem::BufferCollectionConstraints {
        min_buffer_count: 1,
        usage,
        has_buffer_memory_constraints: true,
        buffer_memory_constraints,
        image_format_constraints_count: 1,
        ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
    }
}

/// Returns the Vulkan application info for starnix.
pub fn app_info() -> vk::ApplicationInfo {
    vk::ApplicationInfo {
        sType: vk::STRUCTURE_TYPE_APPLICATION_INFO,
        pNext: std::ptr::null(),
        pApplicationName: cstr!(b"starnix\0").as_ptr(),
        applicationVersion: 0,
        pEngineName: std::ptr::null(),
        engineVersion: 0,
        apiVersion: vulkan_version!(1, 1, 0),
    }
}

/// Returns the Vukan instance info for starnix.
pub fn instance_info(app_info: &vk::ApplicationInfo) -> vk::InstanceCreateInfo {
    vk::InstanceCreateInfo {
        sType: vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        pNext: std::ptr::null(),
        flags: 0,
        pApplicationInfo: app_info,
        enabledLayerCount: 0,
        ppEnabledLayerNames: std::ptr::null(),
        enabledExtensionCount: 0,
        ppEnabledExtensionNames: std::ptr::null(),
    }
}

/// Registers a buffer collection with scenic and returns the associated import token.
///
/// # Parameters
/// - `buffer_collection_token`: The buffer collection token that is passed to the scenic allocator.
/// - `scenic_allocator`: The allocator proxy that is used to register the buffer collection.
fn register_buffer_collection_with_scenic(
    buffer_collection_token: ClientEnd<fsysmem::BufferCollectionTokenMarker>,
    scenic_allocator: &fuicomp::AllocatorSynchronousProxy,
) -> Result<fuicomp::BufferCollectionImportToken, vk::Result> {
    let (scenic_import_token, export_token) =
        zx::EventPair::create().expect("Failed to create event pair.");

    let export_token = fuicomp::BufferCollectionExportToken { value: export_token };
    let scenic_import_token = fuicomp::BufferCollectionImportToken { value: scenic_import_token };

    let args = fuicomp::RegisterBufferCollectionArgs {
        export_token: Some(export_token),
        buffer_collection_token: Some(buffer_collection_token),
        ..fuicomp::RegisterBufferCollectionArgs::EMPTY
    };

    scenic_allocator
        .register_buffer_collection(args, zx::Time::INFINITE)
        .map_err(|_| vk::ERROR_INITIALIZATION_FAILED)?
        .map_err(|_| vk::ERROR_INITIALIZATION_FAILED)?;

    Ok(scenic_import_token)
}
