// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;
mod descriptors;

use {
    crate::args::{Args, UsbDevice},
    crate::descriptors::*,
    anyhow::{format_err, Context, Result},
    fidl_fuchsia_device_manager::DeviceWatcherProxy,
    fuchsia_async::TimeoutExt,
    fuchsia_zircon_status as zx,
    futures::future::{BoxFuture, FutureExt},
    std::sync::Mutex,
};

// This isn't actually unused, but rustc can't seem to tell otherwise.
#[allow(unused_imports)]
use zerocopy::{AsBytes, LayoutVerified};

pub async fn lsusb(device_watcher: DeviceWatcherProxy, args: Args) -> Result<()> {
    if args.tree {
        list_tree(&device_watcher, &args).await
    } else {
        list_devices(&device_watcher, &args).await
    }
}

async fn list_devices(device_watcher: &DeviceWatcherProxy, args: &Args) -> Result<()> {
    println!("ID    VID:PID   SPEED  MANUFACTURER PRODUCT");

    let mut idx = 0;
    while let Ok(device) = device_watcher
        .next_device()
        // This will wait forever, so if there are no more devices, lets stop waiting.
        .on_timeout(std::time::Duration::from_millis(200), || Ok(Err(-1)))
        .await
        .context("FIDL call to get next device returned an error")?
    {
        let channel = fidl::AsyncChannel::from_channel(device)?;
        let device = fidl_fuchsia_hardware_usb_device::DeviceProxy::new(channel);

        match list_device(&device, idx, 0, 0, &args).await {
            Ok(()) => {}
            Err(e) => eprintln!("Error: {:?}", e),
        }

        idx += 1
    }
    Ok(())
}
async fn list_device(
    device: &fidl_fuchsia_hardware_usb_device::DeviceProxy,
    devnum: u32,
    depth: usize,
    max_depth: usize,
    args: &Args,
) -> Result<()> {
    let devname = &format!("/dev/class/usb-device/{:03}", devnum);

    let device_desc_buf = device
        .get_device_descriptor()
        .await
        .context(format!("DeviceGetDeviceDescriptor failed for {}", devname))?;

    let device_desc = LayoutVerified::<_, DeviceDescriptor>::new(device_desc_buf.as_ref()).unwrap();

    if let Some(UsbDevice { vendor_id, product_id }) = args.device {
        // Return early if this isn't the device that was asked about.
        if { device_desc.id_vendor } != vendor_id {
            return Ok(());
        }
        if product_id.is_some() && { device_desc.id_product } != product_id.unwrap() {
            return Ok(());
        }
    }

    let speed = device
        .get_device_speed()
        .await
        .context(format!("DeviceGetDeviceSpeed failed for {}", devname))?;

    let string_manu_desc = get_string_descriptor(device, device_desc.i_manufacturer)
        .await
        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

    let string_prod_desc = get_string_descriptor(device, device_desc.i_product)
        .await
        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

    let left_pad = depth * 4;
    let right_pad = (max_depth - depth) * 4;

    println!(
        "{0:left_pad$}{1:03}  {0:right_pad$}{2:04X}:{3:04X}  {4:<5}  {5} {6}",
        "",
        devnum,
        { device_desc.id_vendor },
        { device_desc.id_product },
        UsbSpeed(speed),
        string_manu_desc,
        string_prod_desc,
        left_pad = left_pad,
        right_pad = right_pad,
    );

    if args.verbose {
        println!("Device Descriptor:");
        println!("  {:<33}{}", "bLength", device_desc.b_length);
        println!("  {:<33}{}", "bDescriptorType", device_desc.b_descriptor_type);
        println!("  {:<33}{}.{}", "bcdUSB", device_desc.bcd_usb >> 8, device_desc.bcd_usb & 0xFF);
        println!("  {:<33}{}", "bDeviceClass", device_desc.b_device_class);
        println!("  {:<33}{}", "bDeviceSubClass", device_desc.b_device_sub_class);
        println!("  {:<33}{}", "bDeviceProtocol", device_desc.b_device_protocol);
        println!("  {:<33}{}", "bMaxPacketSize0", device_desc.b_max_packet_size0);
        println!("  {:<33}{:#06X}", "idVendor", { device_desc.id_vendor });
        println!("  {:<33}{:#06X}", "idProduct", { device_desc.id_product });
        println!(
            "  {:<33}{}.{}",
            "bcdDevice",
            device_desc.bcd_device >> 8,
            device_desc.bcd_device & 0xFF
        );
        println!("  {:<33}{} {}", "iManufacturer", device_desc.i_manufacturer, string_manu_desc);
        println!("  {:<33}{} {}", "iProduct", device_desc.i_product, string_prod_desc);

        let serial_number = get_string_descriptor(device, device_desc.i_serial_number)
            .await
            .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;

        println!("  {:<33}{} {}", "iSerialNumber", device_desc.i_serial_number, serial_number);
        println!("  {:<33}{}", "bNumConfigurations", device_desc.b_num_configurations);

        let mut config = args.configuration;
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
            .map_err(|e| return anyhow::anyhow!("Failed to get configuration descriptor: {}", e))?;

        for descriptor in DescriptorIterator::new(&config_desc_data) {
            match descriptor {
                Descriptor::Config(config_desc) => {
                    println!("{:>2}Configuration Descriptor:", "");
                    println!("{:>4}{:<31}{}", "", "bLength", config_desc.b_length);
                    println!("{:>4}{:<31}{}", "", "bDescriptorType", config_desc.b_descriptor_type);
                    println!("{:>4}{:<31}{}", "", "wTotalLength", { config_desc.w_total_length });
                    println!("{:>4}{:<31}{}", "", "bNumInterfaces", config_desc.b_num_interfaces);
                    println!(
                        "{:>4}{:<31}{}",
                        "", "bConfigurationValue", config_desc.b_configuration_value
                    );
                    let config_str = get_string_descriptor(device, config_desc.i_configuration)
                        .await
                        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;
                    println!(
                        "{:>4}{:<31}{} {}",
                        "", "iConfiguration", config_desc.i_configuration, config_str
                    );
                    println!("{:>4}{:<31}{:#04X}", "", "bmAttributes", config_desc.bm_attributes);
                    println!("{:>4}{:<31}{}", "", "bMaxPower", config_desc.b_max_power);
                }
                Descriptor::Interface(info) => {
                    println!("{:>4}Interface Descriptor:", "");
                    println!("{:>6}{:<29}{}", "", "bLength", info.b_length);
                    println!("{:>6}{:<29}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!("{:>6}{:<29}{}", "", "bInterfaceNumber", info.b_interface_number);
                    println!("{:>6}{:<29}{}", "", "bAlternateSetting", info.b_alternate_setting);
                    println!("{:>6}{:<29}{}", "", "bNumEndpoints", info.b_num_endpoints);
                    println!("{:>6}{:<29}{}", "", "bInterfaceClass", info.b_interface_class);
                    println!("{:>6}{:<29}{}", "", "bInterfaceSubClass", info.b_interface_sub_class);
                    println!("{:>6}{:<29}{}", "", "bInterfaceProtocol", info.b_interface_protocol);

                    let interface_str = get_string_descriptor(device, info.i_interface)
                        .await
                        .context(format!("DeviceGetStringDescriptor failed for {}", devname))?;
                    println!("{:>6}{:<29}{}{}", "", "iInterface", info.i_interface, interface_str);
                }
                Descriptor::Endpoint(info) => {
                    println!("{:>6}Endpoint Descriptor:", "");
                    println!("{:>8}{:<27}{}", "", "bLength", info.b_length);
                    println!("{:>8}{:<27}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!("{:>8}{:<27}{:#04X}", "", "bEndpointAddress", info.b_endpoint_address);
                    println!("{:>8}{:<27}{:#04X}", "", "bmAttributes", info.bm_attributes);
                    println!("{:>8}{:<27}{}", "", "wMaxPacketSize", { info.w_max_packet_size });
                    println!("{:>8}{:<27}{}", "", "bInterval", info.b_interval);
                }
                Descriptor::Hid(descriptor) => {
                    let info = descriptor.get();
                    println!("{:>6}HID Descriptor:", "");
                    println!("{:>8}{:<27}{}", "", "bLength", info.b_length);
                    println!("{:>8}{:<27}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!(
                        "{:>8}{:<27}{}{}",
                        "",
                        "bcdHID",
                        info.bcd_hid >> 8,
                        info.bcd_hid & 0xFF
                    );
                    println!("{:>8}{:<27}{}", "", "bCountryCode", info.b_country_code);
                    println!("{:>8}{:<27}{}", "", "bNumDescriptors", info.b_num_descriptors);
                    for entry in descriptor {
                        println!("{:>10}{:<25}{}", "", "bDescriptorType", entry.b_descriptor_type);
                        println!("{:>10}{:<25}{}", "", "wDescriptorLength", {
                            entry.w_descriptor_length
                        });
                    }
                }
                Descriptor::SsEpCompanion(info) => {
                    println!("{:>8}SuperSpeed Endpoint Companion Descriptor:", "");
                    println!("{:>10}{:<25}{}", "", "bLength", info.b_length);
                    println!("{:>10}{:<25}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!("{:>10}{:<25}{:#04X}", "", "bMaxBurst", info.b_max_burst);
                    println!("{:>10}{:<25}{:#04X}", "", "bmAttributes", info.bm_attributes);
                    println!("{:>10}{:<25}{}", "", "wBytesPerInterval", info.w_bytes_per_interval);
                }
                Descriptor::SsIsochEpCompanion(info) => {
                    println!("{:>10}SuperSpeed Isochronous Endpoint Companion Descriptor:", "");
                    println!("{:>12}{:<23}{}", "", "bLength", info.b_length);
                    println!("{:>12}{:<23}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!("{:>12}{:<23}{}", "", "wReserved", { info.w_reserved });
                    println!("{:>12}{:<23}{}", "", "dwBytesPerInterval", {
                        info.dw_bytes_per_interval
                    });
                }
                Descriptor::InterfaceAssociation(info) => {
                    println!("{:>12}Interface Association Descriptor:", "");
                    println!("{:>14}{:<21}{}", "", "bLength", info.b_length);
                    println!("{:>14}{:<21}{}", "", "bDescriptorType", info.b_descriptor_type);
                    println!("{:>14}{:<21}{}", "", "bFirstInterface", info.b_first_interface);
                    println!("{:>14}{:<21}{}", "", "bInterfaceCount", info.b_interface_count);
                    println!("{:>14}{:<21}{}", "", "bFunctionClass", info.b_function_class);
                    println!("{:>14}{:<21}{}", "", "bFunctionSubClass", info.b_function_sub_class);
                    println!("{:>14}{:<21}{}", "", "bFunctionProtocol", info.b_function_protocol);
                    println!("{:>14}{:<21}{}", "", "iFunction", info.i_function);
                }
                Descriptor::Unknown(buffer) => {
                    println!("Unknown Descriptor:");
                    println!("  {:<33}{}", "bLength", buffer[0]);
                    println!("  {:<33}{}", "bDescriptorType", buffer[1]);
                    println!("  {:X?}", buffer);
                }
            }
        }
    }
    return Ok(());
}

struct DeviceNode {
    pub device: fidl_fuchsia_hardware_usb_device::DeviceProxy,
    pub devnum: u32,
    pub device_id: u32,
    pub hub_id: u32,
    // Depth in tree, None if not computed yet.
    // Mutex is used for interior mutability.
    pub depth: Mutex<Option<usize>>,
}

impl DeviceNode {
    fn get_depth(&self, devices: &[DeviceNode]) -> Result<usize> {
        if let Some(depth) = self.depth.lock().unwrap().clone() {
            return Ok(depth);
        }
        if self.hub_id == 0 {
            return Ok(0);
        }
        for device in devices.iter() {
            if self.hub_id == device.device_id {
                return device.get_depth(devices).map(|depth| depth + 1);
            }
        }
        Err(format_err!("Hub not found for device"))
    }
}

async fn list_tree(device_watcher: &DeviceWatcherProxy, args: &Args) -> Result<()> {
    let mut devices = Vec::new();
    while let Ok(device) = device_watcher
        .next_device()
        // This will wait forever, so if there are no more devices, lets stop waiting.
        .on_timeout(std::time::Duration::from_millis(200), || Ok(Err(-1)))
        .await
        .context("FIDL call to get next device returned an error")?
    {
        let channel = fidl::AsyncChannel::from_channel(device)?;
        let device = fidl_fuchsia_hardware_usb_device::DeviceProxy::new(channel);

        devices.push(get_device_info(device, devices.len() as u32).await?);
    }

    for device in devices.iter() {
        let depth = device.get_depth(&devices)?;
        *device.depth.lock().unwrap() = Some(depth);
    }

    let max_depth = devices
        .iter()
        .filter_map(|device| device.depth.lock().unwrap().clone())
        .fold(0, std::cmp::max::<usize>);

    print!("ID   ");
    for _ in 0..max_depth {
        print!("    ");
    }
    println!(" VID:PID   SPEED  MANUFACTURER PRODUCT");

    do_list_tree(&devices, 0, max_depth, args).await
}

fn do_list_tree<'a>(
    devices: &'a [DeviceNode],
    hub_id: u32,
    max_depth: usize,
    args: &'a Args,
) -> BoxFuture<'a, Result<()>> {
    async move {
        for device in devices.iter() {
            if device.hub_id == hub_id {
                let depth = device.depth.lock().unwrap().unwrap().clone();
                match list_device(&device.device, device.devnum, depth, max_depth, args).await {
                    Ok(()) => {}
                    Err(e) => eprintln!("Error: {:?}", e),
                }
                do_list_tree(devices, device.device_id, max_depth, args).await?;
            }
        }
        Ok(())
    }
    .boxed()
}

async fn get_device_info(
    device: fidl_fuchsia_hardware_usb_device::DeviceProxy,
    devnum: u32,
) -> Result<DeviceNode> {
    let devname = &format!("/dev/class/usb-device/{:03}", devnum);

    let device_id =
        device.get_device_id().await.context(format!("GetDeviceId failed for {}", devname))?;

    let hub_id =
        device.get_hub_device_id().await.context(format!("GeHubId failed for {}", devname))?;

    Ok(DeviceNode { device, devnum, device_id, hub_id, depth: Mutex::new(None) })
}

async fn get_string_descriptor(
    device: &fidl_fuchsia_hardware_usb_device::DeviceProxy,
    desc_id: u8,
) -> Result<String, anyhow::Error> {
    match desc_id {
        0 => return Ok(String::from("UNKNOWN")),
        _ => {
            return device.get_string_descriptor(desc_id, EN_US).await.map(
                |(status, value, _)| {
                    if zx::Status::ok(status).is_ok() {
                        Ok(value)
                    } else {
                        Ok(String::from("UNKNOWN"))
                    }
                },
            )?
        }
    };
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
                        b_length: std::mem::size_of::<DeviceDescriptor>() as u8,
                        b_descriptor_type: 1,
                        bcd_usb: 2,
                        b_device_class: 3,
                        b_device_sub_class: 4,
                        b_device_protocol: 5,
                        b_max_packet_size0: 6,
                        id_vendor: 7,
                        id_product: 8,
                        bcd_device: 9,
                        i_manufacturer: 10,
                        i_product: 11,
                        i_serial_number: 12,
                        b_num_configurations: 2,
                    };
                    let mut array = [0; 18];
                    array.copy_from_slice(descriptor.as_bytes());
                    responder.send(&mut array)?;
                }
                fidl_fuchsia_hardware_usb_device::DeviceRequest::GetConfigurationDescriptor {
                        config: _, responder} => {
                    let total_length =
                        std::mem::size_of::<ConfigurationDescriptor>() +
                        std::mem::size_of::<InterfaceInfoDescriptor>() * 2;

                    let config_descriptor = ConfigurationDescriptor {
                        b_length: std::mem::size_of::<ConfigurationDescriptor>() as u8,
                        b_descriptor_type: 0,
                        w_total_length: total_length as u16,
                        b_num_interfaces: 2,
                        b_configuration_value: 0,
                        i_configuration: 0,
                        bm_attributes: 0,
                        b_max_power: 0,
                    };

                    let interface_one = InterfaceInfoDescriptor {
                        b_length: std::mem::size_of::<InterfaceInfoDescriptor>() as u8,
                        b_descriptor_type: 4,
                        b_interface_number: 1,
                        b_alternate_setting: 0,
                        b_num_endpoints: 0,
                        b_interface_class: 1,
                        b_interface_sub_class: 2,
                        b_interface_protocol: 3,
                        i_interface: 1,
                    };

                    let interface_two = InterfaceInfoDescriptor {
                        b_length: std::mem::size_of::<InterfaceInfoDescriptor>() as u8,
                        b_descriptor_type: 4,
                        b_interface_number: 2,
                        b_alternate_setting: 0,
                        b_num_endpoints: 0,
                        b_interface_class: 3,
                        b_interface_sub_class: 4,
                        b_interface_protocol: 5,
                        i_interface: 2,
                    };

                    let mut vec = std::vec::Vec::new();
                    vec.extend_from_slice(config_descriptor.as_bytes());
                    vec.extend_from_slice(interface_one.as_bytes());
                    vec.extend_from_slice(interface_two.as_bytes());

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
            let args = Args { tree: false, verbose: true, configuration: None, device: None };
            println!("ID    VID:PID   SPEED  MANUFACTURER PRODUCT");
            list_device(&device, 0, 0, 0, &args).await.unwrap();
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
