// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use std::convert::TryFrom;
use {
    anyhow::{format_err, Result},
    ffx_core::ffx_plugin,
    ffx_driver_lsusb_args::DriverLsusbCommand,
    fidl_fuchsia_device_manager::DeviceWatcherProxy,
    fuchsia_zircon_status as zx,
};

#[ffx_plugin(
    "driver_enabled",
    DeviceWatcherProxy = "bootstrap/driver_manager:expose:fuchsia.hardware.usb.DeviceWatcher"
)]
pub async fn lsusb(device_watcher: DeviceWatcherProxy, cmd: DriverLsusbCommand) -> Result<()> {
    let device_result = device_watcher
        .next_device()
        .await
        .map_err(|err| format_err!("FIDL call to get next device failed: {}", err))?
        .map_err(|err| format_err!("FIDL call to get next device returned an error: {}", err))?;

    println!("ID    VID:PID   SPEED  MANUFACTURER PRODUCT");
    let channel = fidl::AsyncChannel::from_channel(device_result)?;

    let device = fidl_fuchsia_hardware_usb_device::DeviceProxy::new(channel);

    return do_list_device(device, "000", cmd).await;
}

#[cfg(test)]
mod test {}

#[allow(non_snake_case)]
#[repr(C)]
pub struct DeviceDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bcdUSB: u16,
    pub bDeviceClass: u8,
    pub bDeviceSubClass: u8,
    pub bDeviceProtocol: u8,
    pub bMaxPacketSize0: u8,
    pub idVendor: u16,
    pub idProduct: u16,
    pub bcdDevice: u16,
    pub iManufacturer: u8,
    pub iProduct: u8,
    pub iSerialNumber: u8,
    pub bNumConfigurations: u8,
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct ConfigurationDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub wTotalLength: u16,
    pub bNumInterfaces: u8,
    pub bConfigurationValue: u8,
    pub iConfiguration: u8,
    pub bmAttributes: u8,
    pub bMaxPower: u8,
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct InterfaceInfoDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bInterfaceNumber: u8,
    pub bAlternateSetting: u8,
    pub bNumEndpoints: u8,
    pub bInterfaceClass: u8,
    pub bInterfaceSubClass: u8,
    pub bInterfaceProtocol: u8,
    pub iInterface: u8,
}

struct UsbSpeed(u32);

impl std::fmt::Display for UsbSpeed {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            1 => write!(f, "FULL"),
            2 => write!(f, "LOW"),
            3 => write!(f, "HIGH"),
            4 => write!(f, "SUPER"),
            _ => write!(f, "<unknown>"),
        }
    }
}

const EN_US: u16 = 0x0409;

async fn do_list_device(
    device: fidl_fuchsia_hardware_usb_device::DeviceProxy,
    devname: &str,
    cmd: DriverLsusbCommand,
) -> Result<(), anyhow::Error> {
    let device_desc = device.get_device_descriptor().await.context(format!(
        "DeviceGetDeviceDescriptor failed for /dev/class/usb-device/{}",
        devname
    ))?;

    let speed = device
        .get_device_speed()
        .await
        .context(format!("DeviceGetDeviceSpeed failed for /dev/class/usb-device/{}", devname))?;

    let device_desc = unsafe { std::mem::transmute::<[u8; 18], DeviceDescriptor>(device_desc) };

    let (status, string_manu_desc, _) =
        device.get_string_descriptor(device_desc.iManufacturer, EN_US).await.context(format!(
            "DeviceGetStringDescriptor failed for /dev/class/usb-device/{}",
            devname
        ))?;

    zx::Status::ok(status)
        .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

    let (status, string_prod_desc, _) =
        device.get_string_descriptor(device_desc.iProduct, EN_US).await.context(format!(
            "DeviceGetStringDescriptor failed for /dev/class/usb-device/{}",
            devname
        ))?;

    zx::Status::ok(status)
        .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

    println!(
        "{:03}  {:04X}:{:04X}  {:<5}  {} {}",
        devname,
        device_desc.idVendor,
        device_desc.idProduct,
        UsbSpeed(speed),
        string_manu_desc,
        string_prod_desc,
    );

    if cmd.verbose {
        println!("Device Descriptor:");
        println!("  {:<33}{}", "bLength", device_desc.bLength);
        println!("  {:<33}{}", "bDescriptorType", device_desc.bDescriptorType);
        println!("  {:<33}{}.{}", "bcdUSB", device_desc.bcdUSB >> 8, device_desc.bcdUSB & 0xFF);
        println!("  {:<33}{}", "bDeviceClass", device_desc.bDeviceClass);
        println!("  {:<33}{}", "bDeviceSubClass", device_desc.bDeviceSubClass);
        println!("  {:<33}{}", "bDeviceProtocol", device_desc.bDeviceProtocol);
        println!("  {:<33}{}", "bMaxPacketSize0", device_desc.bMaxPacketSize0);
        println!("  {:<33}{:#06X}", "idVendor", device_desc.idVendor);
        println!("  {:<33}{:#06X}", "idProduct", device_desc.idProduct);
        println!(
            "  {:<33}{}.{}",
            "bcdDevice",
            device_desc.bcdDevice >> 8,
            device_desc.bcdDevice & 0xFF
        );
        println!("  {:<33}{} {}", "iManufacturer", device_desc.iManufacturer, string_manu_desc);
        println!("  {:<33}{} {}", "iProduct", device_desc.iProduct, string_prod_desc);

        let string_buf =
            device.get_string_descriptor(device_desc.iSerialNumber, EN_US).await.context(
                format!("DeviceGetStringDescriptor failed for /dev/class/usb-device/{}", devname),
            )?;

        println!("  {:<33}{} {}", "iSerialNumber", device_desc.iSerialNumber, string_buf.1);
        println!("  {:<33}{}", "bNumConfigurations", device_desc.bNumConfigurations);

        let mut config = cmd.configuration;
        if config.is_none() {
            config = Some(device.get_configuration().await.context(format!(
                "DeviceGetConfiguration failed for /dev/class/usb-device/{}",
                devname
            ))?);
        }

        let (status, mut config_desc) =
            device.get_configuration_descriptor(config.unwrap()).await.context(format!(
                "DeviceGetConfigurationDescriptor failed for /dev/class/usb-device/{}",
                devname
            ))?;

        zx::Status::ok(status)
            .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

        config_desc.resize(std::mem::size_of::<ConfigurationDescriptor>(), 0);
        let config_desc =
            <[u8; std::mem::size_of::<ConfigurationDescriptor>()]>::try_from(config_desc).unwrap();

        let config_desc =
            unsafe { std::mem::transmute::<[u8; 10], ConfigurationDescriptor>(config_desc) };

        println!("{}", "  Configuration Descriptor:");
        println!("    {:<33}{}", "bLength", config_desc.bLength);
        println!("    {:<33}{}", "bDescriptorType", config_desc.bDescriptorType);
        println!("    {:<33}{}", "wTotalLength", config_desc.wTotalLength);
        println!("    {:<33}{}", "bNumInterfaces", config_desc.bNumInterfaces);
        println!("    {:<33}{}", "bConfigurationValue", config_desc.bConfigurationValue);
        let string_buf =
            device.get_string_descriptor(config_desc.iConfiguration, EN_US).await.context(
                format!("DeviceGetStringDescriptor failed for /dev/class/usb-device/{}", devname),
            )?;
        println!("    {:<33}{} {}", "iConfiguration", config_desc.iConfiguration, string_buf.1);
        println!("    {:<33}{:#04X}", "bmAttributes", config_desc.bmAttributes);
        println!("    {:<33}{}", "bMaxPower", config_desc.bMaxPower);

        // TODO: print interface & endpoint descriptors (add pointers together)
    }
    return Ok(());
}
