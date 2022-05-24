// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_vulkan as fvk;
use std::ffi::CStr;
use std::mem::MaybeUninit;
use std::ptr;
use anyhow::anyhow;
use vk_sys as vk;

macro_rules! cstr {
    ( $bytes:expr ) => {
        CStr::from_bytes_with_nul($bytes).expect("CStr must end with '\\0'")
    };
}

macro_rules! vulkan_version {
    ( $major:expr, $minor:expr, $patch:expr ) => {
        ($major as u32) << 22 | ($minor as u32) << 12 | ($patch as u32)
    };
}

unsafe fn create_from<T>(f: impl FnOnce(*mut T)) -> T {
    let mut value = MaybeUninit::uninit();
    f(value.as_mut_ptr());
    value.assume_init()
}

fn create_vk_instance() -> Result<vk::Instance, Error> {
    let entry_points = fvk::entry_points();
    let mut instance: vk::Instance = 0;
    let create_instance_result = unsafe {
        entry_points.CreateInstance(
            &vk::InstanceCreateInfo {
                sType: vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                pNext: ptr::null(),
                flags: 0,
                pApplicationInfo: &vk::ApplicationInfo {
                    sType: vk::STRUCTURE_TYPE_APPLICATION_INFO,
                    pNext: ptr::null(),
                    pApplicationName: cstr!(b"flatland-view-provider\0").as_ptr(),
                    applicationVersion: 0,
                    pEngineName: ptr::null(),
                    engineVersion: 0,
                    apiVersion: vulkan_version!(1, 1, 0),
                },
                enabledLayerCount: 0,
                ppEnabledLayerNames: ptr::null(),
                enabledExtensionCount: 0,
                ppEnabledExtensionNames: ptr::null(),
            },
            ptr::null(),
            &mut instance as *mut vk::Instance,
        )
    };
    if create_instance_result != vk::SUCCESS {
        return Err(anyhow!("Failed to create vk::Instance"));
    }
    Ok(instance)
}

fn get_vk_physical_device(instance: vk::Instance) -> Option<vk::PhysicalDevice> {
    let instance_pointers = fvk::instance_pointers(instance);
    let physical_devices = {
        let mut len = unsafe {
            create_from(|ptr| {
                instance_pointers.EnumeratePhysicalDevices(instance, ptr, ptr::null_mut());
            })
        };
        let mut physical_devices: Vec<vk::PhysicalDevice> = Vec::with_capacity(len as usize);
        unsafe {
            let enumerate_result = instance_pointers.EnumeratePhysicalDevices(
                instance,
                &mut len,
                physical_devices.as_mut_ptr(),
            );
            assert!(enumerate_result == vk::SUCCESS);
            physical_devices.set_len(len as usize);
        }
        physical_devices
    };
    physical_devices.get(0).map(|val| *val)
}

fn create_vk_device(
    instance: vk::Instance,
    extension_names: Vec<*const c_char>,
) -> Result<vk::Device, Error> {
    let instance_pointers = fvk::instance_pointers(instance);
    let physical_device = match get_vk_physical_device(instance)?;

    let mut queue_family_count = unsafe {
        create_from(|ptr| {
            vk_i.GetPhysicalDeviceQueueFamilyProperties(physical_device, ptr, ptr::null_mut())
        })
    };
    let mut queue_family_properties = Vec::with_capacity(queue_family_count as usize);
    unsafe {
        vk_i.GetPhysicalDeviceQueueFamilyProperties(
            physical_device,
            &mut queue_family_count,
            queue_family_properties.as_mut_ptr(),
        );
        queue_family_properties.set_len(queue_family_count as usize);
    }
    if queue_family_count == 0 {
        println!("no queue family available!");
        return None;
    }

    let queue_priority: f32 = 1.0;
    let queue_create_info = vk::DeviceQueueCreateInfo {
        sType: vk::STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        pNext: ptr::null(),
        flags: 0,
        queueFamilyIndex: 0,
        queueCount: 1,
        pQueuePriorities: &queue_priority as *const f32,
    };

    let device = unsafe {
        init(|ptr| {
            let create_device_result = vk_i.CreateDevice(
                physical_device,
                &vk::DeviceCreateInfo {
                    sType: vk::STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    pNext: ptr::null(),
                    flags: 0,
                    queueCreateInfoCount: 1,
                    pQueueCreateInfos: &queue_create_info as *const _,
                    enabledLayerCount: 0,
                    ppEnabledLayerNames: ptr::null(),
                    enabledExtensionCount: extension_names.len() as u32,
                    ppEnabledExtensionNames: extension_names.as_ptr(),
                    pEnabledFeatures: ptr::null(),
                },
                ptr::null(),
                ptr,
            );
            assert!(create_device_result == vk::SUCCESS);
        })
    };
    Some(device)
}

fn destroy_vk_device(instance: vk::Instance, device: vk::Device) {
    let vk_i = super::instance_pointers(instance);
    let vk_d = super::device_pointers(&vk_i, device);
    unsafe {
        vk_d.DestroyDevice(device, ptr::null());
    }
}



pub fn init() {
    let instance = create_vk_instance();
    let physical_device = get_vk_physical_device(instance);

    assert!(physical_device.is_some());
    // destroy_vk_device(instance, physical_device.unwrap());
}
