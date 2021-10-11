// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod descriptors;

use {
    crate::descriptors::*,
    anyhow::{Context, Result},
    ffx_core::ffx_plugin,
    ffx_driver_lsusb_args::DriverLsusbCommand,
    fidl_fuchsia_device_manager::DeviceWatcherProxy,
    fuchsia_async::{Duration, TimeoutExt},
    fuchsia_zircon_status as zx,
    num_traits::FromPrimitive,
};

#[ffx_plugin(
    "driver_enabled",
    DeviceWatcherProxy = "bootstrap/driver_manager:expose:fuchsia.hardware.usb.DeviceWatcher"
)]
pub async fn lsusb(device_watcher: DeviceWatcherProxy, cmd: DriverLsusbCommand) -> Result<()> {
    println!("ID    VID:PID   SPEED  MANUFACTURER PRODUCT");

    let mut idx = 0;
    while let Ok(device) = device_watcher
        .next_device()
        // This will wait forever, so if there are no more devices, lets stop waiting.
        // TODO(http://fxbug.dev/86386): Avoid this timeout.
        .on_timeout(Duration::from_millis(200), || Ok(Err(-1)))
        .await
        .context("FIDL call to get next device returned an error")?
    {
        let channel = fidl::AsyncChannel::from_channel(device)?;
        let device = fidl_fuchsia_hardware_usb_device::DeviceProxy::new(channel);

        do_list_device(device, idx, &cmd).await?;

        idx += 1
    }
    Ok(())
}

async fn do_list_device(
    device: fidl_fuchsia_hardware_usb_device::DeviceProxy,
    devnum: u32,
    cmd: &DriverLsusbCommand,
) -> Result<(), anyhow::Error> {
    let devname = &format!("/dev/class/usb-device/{:03}", devnum);

    let device_desc = device
        .get_device_descriptor()
        .await
        .context(format!("DeviceGetDeviceDescriptor failed for {}", devname))?;

    let speed = device
        .get_device_speed()
        .await
        .context(format!("DeviceGetDeviceSpeed failed for {}", devname))?;

    let device_desc = DeviceDescriptor::from_array(device_desc);

    let (status, string_manu_desc, _) = device
        .get_string_descriptor(device_desc.iManufacturer, EN_US)
        .await
        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

    zx::Status::ok(status)
        .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

    let (status, string_prod_desc, _) = device
        .get_string_descriptor(device_desc.iProduct, EN_US)
        .await
        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

    zx::Status::ok(status)
        .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

    println!(
        "{:03}  {:04X}:{:04X}  {:<5}  {} {}",
        devnum,
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

        let string_buf = device
            .get_string_descriptor(device_desc.iSerialNumber, EN_US)
            .await
            .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

        println!("  {:<33}{} {}", "iSerialNumber", device_desc.iSerialNumber, string_buf.1);
        println!("  {:<33}{}", "bNumConfigurations", device_desc.bNumConfigurations);

        let mut config = cmd.configuration;
        if config.is_none() {
            config = Some(
                device
                    .get_configuration()
                    .await
                    .context(format!("DeviceGetConfiguration failed for {}", devname))?,
            );
        }

        let (status, config_desc_data) = device
            .get_configuration_descriptor(config.unwrap())
            .await
            .context(format!("DeviceGetConfigurationDescriptor failed for {}", devname))?;

        zx::Status::ok(status)
            .map_err(|e| return anyhow::anyhow!("Failed to get string descriptor: {}", e))?;

        if config_desc_data.len() < std::mem::size_of::<ConfigurationDescriptor>() {
            return Err(anyhow::anyhow!(
                "ConfigurationDescriptor data is too small, size: {}",
                config_desc_data.len()
            ));
        }

        let mut config_desc = [0; std::mem::size_of::<ConfigurationDescriptor>()];
        config_desc
            .copy_from_slice(&config_desc_data[0..std::mem::size_of::<ConfigurationDescriptor>()]);
        let config_desc = ConfigurationDescriptor::from_array(config_desc);

        println!("{}", "  Configuration Descriptor:");
        println!("    {:<33}{}", "bLength", config_desc.bLength);
        println!("    {:<33}{}", "bDescriptorType", config_desc.bDescriptorType);
        println!("    {:<33}{}", "wTotalLength", config_desc.wTotalLength);
        println!("    {:<33}{}", "bNumInterfaces", config_desc.bNumInterfaces);
        println!("    {:<33}{}", "bConfigurationValue", config_desc.bConfigurationValue);
        let string_buf = device
            .get_string_descriptor(config_desc.iConfiguration, EN_US)
            .await
            .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;
        println!("    {:<33}{} {}", "iConfiguration", config_desc.iConfiguration, string_buf.1);
        println!("    {:<33}{:#04X}", "bmAttributes", config_desc.bmAttributes);
        println!("    {:<33}{}", "bMaxPower", config_desc.bMaxPower);

        let mut offset = std::mem::size_of::<ConfigurationDescriptor>();

        while offset < config_desc_data.len() {
            let length = config_desc_data[offset];
            let desc_type = config_desc_data[offset + 1];

            if length == 0 {
                println!("zero length header, bailing");
            }

            match DescriptorType::from_u8(desc_type) {
                Some(DescriptorType::Interface) => {
                    let mut info = [0; std::mem::size_of::<InterfaceInfoDescriptor>()];
                    info.copy_from_slice(
                        &config_desc_data
                            [offset..(std::mem::size_of::<InterfaceInfoDescriptor>() + offset)],
                    );
                    let info = InterfaceInfoDescriptor::from_array(info);

                    println!("    Interface Descriptor:");
                    println!("      {:<33}{}", "bLength", info.bLength);
                    println!("      {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!("      {:<33}{}", "bInterfaceNumber", info.bInterfaceNumber);
                    println!("      {:<33}{}", "bAlternateSetting", info.bAlternateSetting);
                    println!("      {:<33}{}", "bNumEndpoints", info.bNumEndpoints);
                    println!("      {:<33}{}", "bInterfaceClass", info.bInterfaceClass);
                    println!("      {:<33}{}", "bInterfaceSubClass", info.bInterfaceSubClass);
                    println!("      {:<33}{}", "bInterfaceProtocol", info.bInterfaceProtocol);

                    let string_buf = device
                        .get_string_descriptor(device_desc.iSerialNumber, EN_US)
                        .await
                        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;
                    println!("      {:<33}{}{}", "iInterface", info.iInterface, string_buf.1);
                }
                Some(DescriptorType::Endpoint) => {
                    let mut info = [0; std::mem::size_of::<EndpointInfoDescriptor>()];
                    info.copy_from_slice(
                        &config_desc_data
                            [offset..(std::mem::size_of::<EndpointInfoDescriptor>() + offset)],
                    );
                    let info = EndpointInfoDescriptor::from_array(info);

                    println!("      Endpoint Descriptor:");
                    println!("        {:<33}{}", "bLength", info.bLength);
                    println!("        {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!("        {:<33}{}", "bEndpointAddress", info.bEndpointAddress);
                    println!("        {:<33}{}", "bmAttributes", info.bmAttributes);
                    println!("        {:<33}{}", "wMaxPacketSize", info.wMaxPacketSize);
                    println!("        {:<33}{}", "bInterval", info.bInterval);
                }
                Some(DescriptorType::Hid) => {
                    let mut info = [0; std::mem::size_of::<HidDescriptor>()];
                    info.copy_from_slice(
                        &config_desc_data[offset..(std::mem::size_of::<HidDescriptor>() + offset)],
                    );
                    let info = HidDescriptor::from_array(info);

                    println!("        HID Descriptor:\n");
                    println!("          {:<33}{}", "bLength", info.bLength);
                    println!("          {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!(
                        "          {:<33}{}{}",
                        "bcdHID",
                        info.bcdHID >> 8,
                        info.bcdHID & 0xFF
                    );
                    println!("          {:<33}{}", "bCountryCode", info.bCountryCode);
                    println!("          {:<33}{}", "bNumDescriptors", info.bNumDescriptors);
                    for i in 0..info.bNumDescriptors {
                        let mut entry = [0; std::mem::size_of::<HidDescriptorEntry>()];
                        entry.copy_from_slice(
                            &config_desc_data[offset
                                ..(std::mem::size_of::<HidDescriptor>()
                                    + offset
                                    + (i as usize * std::mem::size_of::<HidDescriptorEntry>()))],
                        );
                        let entry = HidDescriptorEntry::from_array(entry);
                        println!("          {:<33}{}", "bDescriptorType", entry.bDescriptorType);
                        println!(
                            "          {:<33}{}",
                            "wDescriptorLength", entry.wDescriptorLength
                        );
                    }
                }
                Some(DescriptorType::SsEpCompanion) => {
                    let mut info = [0; std::mem::size_of::<SsEpCompDescriptorInfo>()];
                    info.copy_from_slice(
                        &config_desc_data
                            [offset..(std::mem::size_of::<SsEpCompDescriptorInfo>() + offset)],
                    );
                    let info = SsEpCompDescriptorInfo::from_array(info);

                    println!("          SuperSpeed Endpoint Companion Descriptor:");
                    println!("            {:<33}{}", "bLength", info.bLength);
                    println!("            {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!("            {:<33}{}", "bMaxBurst", info.bMaxBurst);
                    println!("            {:<33}{}", "bmAttributes", info.bmAttributes);
                    println!("            {:<33}{}", "wBytesPerInterval", info.wBytesPerInterval);
                }
                Some(DescriptorType::SsIsochEpCompanion) => {
                    let mut info = [0; std::mem::size_of::<SsIsochEpCompDescriptor>()];
                    info.copy_from_slice(
                        &config_desc_data
                            [offset..(std::mem::size_of::<SsIsochEpCompDescriptor>() + offset)],
                    );
                    let info = SsIsochEpCompDescriptor::from_array(info);

                    println!("            SuperSpeed Isochronous Endpoint Companion Descriptor:");
                    println!("              {:<33}{}", "bLength", info.bLength);
                    println!("              {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!("              {:<33}{}", "wReserved", info.wReserved);
                    println!(
                        "              {:<33}{}",
                        "dwBytesPerInterval", info.dwBytesPerInterval
                    );
                }
                Some(DescriptorType::InterfaceAssociation) => {
                    let mut info = [0; std::mem::size_of::<InterfaceAssocDescriptor>()];
                    info.copy_from_slice(
                        &config_desc_data
                            [offset..(std::mem::size_of::<InterfaceAssocDescriptor>() + offset)],
                    );
                    let info = InterfaceAssocDescriptor::from_array(info);

                    println!("              Interface Association Descriptor:");
                    println!("                {:<33}{}", "bLength", info.bLength);
                    println!("                {:<33}{}", "bDescriptorType", info.bDescriptorType);
                    println!("                {:<33}{}", "bFirstInterface", info.bFirstInterface);
                    println!("                {:<33}{}", "bInterfaceCount", info.bInterfaceCount);
                    println!("                {:<33}{}", "bFunctionClass", info.bFunctionClass);
                    println!(
                        "                {:<33}{}",
                        "bFunctionSubClass", info.bFunctionSubClass
                    );
                    println!(
                        "                {:<33}{}",
                        "bFunctionProtocol", info.bFunctionProtocol
                    );
                    println!("                {:<33}{}", "iFunction", info.iFunction);
                }
                _ => {
                    println!("                Unknown Descriptor:");
                    println!("                  {:<33}{}", "bLength", length);
                    println!("                  {:<33}{}", "bDescriptorType", desc_type);
                    println!("{:X?}", &config_desc_data[offset..offset + length as usize]);
                }
            }
            offset += length as usize;
        }
    }
    return Ok(());
}

#[cfg(test)]
mod test {

    use super::*;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use futures::FutureExt;

    async fn run_usb_server(
        stream: fidl_fuchsia_hardware_usb_device::DeviceRequestStream,
    ) -> Result<(), anyhow::Error> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async {
                match request {
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetDeviceSpeed {responder } => {
                    responder.send(1)?;
                }
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetDeviceDescriptor {
                        responder} => {
                    let descriptor = DeviceDescriptor {
                        bLength: std::mem::size_of::<DeviceDescriptor>() as u8,
                        bDescriptorType: 1,
                        bcdUSB: 2,
                        bDeviceClass: 3,
                        bDeviceSubClass: 4,
                        bDeviceProtocol: 5,
                        bMaxPacketSize0: 6,
                        idVendor: 7,
                        idProduct: 8,
                        bcdDevice: 9,
                        iManufacturer: 10,
                        iProduct: 11,
                        iSerialNumber: 12,
                        bNumConfigurations: 2,
                    };
                    let mut array = descriptor.to_array();
                    responder.send(&mut array)?;
                }
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetConfigurationDescriptor {
                        config: _, responder} => {
                    let total_length =
                        std::mem::size_of::<ConfigurationDescriptor>() +
                        std::mem::size_of::<InterfaceInfoDescriptor>() * 2;

                    let config_descriptor = ConfigurationDescriptor {
                        bLength: std::mem::size_of::<ConfigurationDescriptor>() as u8,
                        bDescriptorType: 0,
                        wTotalLength: total_length as u16,
                        bNumInterfaces: 2,
                        bConfigurationValue: 0,
                        iConfiguration: 0,
                        bmAttributes: 0,
                        bMaxPower: 0,
                    };

                    let interface_one = InterfaceInfoDescriptor {
                        bLength: std::mem::size_of::<InterfaceInfoDescriptor>() as u8,
                        bDescriptorType: 4,
                        bInterfaceNumber: 1,
                        bAlternateSetting: 0,
                        bNumEndpoints: 0,
                        bInterfaceClass: 1,
                        bInterfaceSubClass: 2,
                        bInterfaceProtocol: 3,
                        iInterface: 1,
                    };

                    let interface_two = InterfaceInfoDescriptor {
                        bLength: std::mem::size_of::<InterfaceInfoDescriptor>() as u8,
                        bDescriptorType: 4,
                        bInterfaceNumber: 2,
                        bAlternateSetting: 0,
                        bNumEndpoints: 0,
                        bInterfaceClass: 3,
                        bInterfaceSubClass: 4,
                        bInterfaceProtocol: 5,
                        iInterface: 2,
                    };

                    let mut vec = std::vec::Vec::new();
                    vec.extend_from_slice(&config_descriptor.to_array());
                    vec.extend_from_slice(&interface_one.to_array());
                    vec.extend_from_slice(&interface_two.to_array());

                    responder.send(0, &mut vec)?;
                }
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetStringDescriptor {
                        desc_id: _, lang_id: _, responder} => {
                    responder.send(0, "<unknown>", 0)?;
                }
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetConfiguration {responder} => {
                    responder.send(0)?;
                }
                _ => {
                    return Err(anyhow::anyhow!("Unsupported function"));
                }
            }
                Ok(())
            })
            .await?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn smoke_test() {
        let (device, stream) = fidl::endpoints::create_proxy_and_stream::<
            fidl_fuchsia_hardware_usb_device::DeviceMarker,
        >()
        .unwrap();

        let server_task = run_usb_server(stream).fuse();
        let test_task = async move {
            let cmd = DriverLsusbCommand {
                tree: false,
                verbose: true,
                configuration: None,
                debug: false,
            };
            println!("ID    VID:PID   SPEED  MANUFACTURER PRODUCT");
            do_list_device(device, 0, &cmd).await.unwrap();
        }
        .fuse();
        futures::pin_mut!(server_task, test_task);
        futures::select! {
            result = server_task => {
                panic!("Server task finished: {:?}", result);
            },
            () = test_task => {},
        }
    }
}
