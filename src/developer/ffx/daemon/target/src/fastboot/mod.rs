// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fastboot::{
        client::{FastbootImpl, InterfaceFactory},
        network::{
            tcp::{TcpNetworkFactory, TcpNetworkInterface},
            udp::{UdpNetworkFactory, UdpNetworkInterface},
        },
    },
    crate::target::Target,
    anyhow::{anyhow, bail, Context, Result},
    async_trait::async_trait,
    chrono::Duration,
    fastboot::{
        command::{ClientVariable, Command},
        download,
        reply::Reply,
        send, send_with_listener, send_with_timeout, upload, SendError,
    },
    ffx_config::get,
    ffx_daemon_events::FastbootInterface,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_developer_ffx::{
        FastbootRequestStream, UploadProgressListenerMarker, UploadProgressListenerProxy,
        VariableListenerMarker, VariableListenerProxy,
    },
    futures::{
        io::{AsyncRead, AsyncWrite},
        lock::Mutex,
    },
    lazy_static::lazy_static,
    std::fs::read,
    std::rc::Rc,
    std::{collections::HashSet, convert::TryInto},
    usb_bulk::{AsyncInterface as Interface, InterfaceInfo, Open},
};

pub mod client;
pub mod network;

lazy_static! {
    /// This is used to lock serial numbers that are in use from being interrupted by the discovery
    /// loop.  Otherwise, it's possible to read the REPLY for the discovery code in the flashing
    /// workflow.
    static ref SERIALS_IN_USE: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
}

const FLASH_TIMEOUT_RATE: &str = "fastboot.flash.timeout_rate";
const MIN_FLASH_TIMEOUT: &str = "fastboot.flash.min_timeout_secs";

/// Fastboot Service that handles communicating with a target over the Fastboot protocol.
///
/// Since this service can handle establishing communication with a target in any state (Product,
/// Fastboot, or Zedboot) the service contains both an implementation of the USB transport and
/// the UDP transport. It is impossible to know which transport will be needed if the target
/// starts in the Product or Zedboot state so both implementations are present from creation.
pub struct Fastboot {
    target: Rc<Target>,
    usb: FastbootImpl<Interface>,
    udp: FastbootImpl<UdpNetworkInterface>,
    tcp: FastbootImpl<TcpNetworkInterface>,
}

impl Fastboot {
    pub fn new(target: Rc<Target>) -> Self {
        Self {
            target: target.clone(),
            udp: FastbootImpl::new(target.clone(), Box::new(UdpNetworkFactory::new())),
            usb: FastbootImpl::new(target.clone(), Box::new(UsbFactory::default())),
            tcp: FastbootImpl::new(target.clone(), Box::new(TcpNetworkFactory::new())),
        }
    }

    pub async fn handle_fastboot_requests_from_stream(
        &mut self,
        stream: FastbootRequestStream,
    ) -> Result<()> {
        if let Some((_, interface)) = self.target.fastboot_address() {
            match interface {
                FastbootInterface::Tcp => {
                    self.tcp.handle_fastboot_requests_from_stream(stream).await
                }
                FastbootInterface::Udp => {
                    self.udp.handle_fastboot_requests_from_stream(stream).await
                }
                _ => bail!("Unexpected interface type {:?}", self.target.fastboot_interface()),
            }
        } else {
            self.usb.handle_fastboot_requests_from_stream(stream).await
        }
    }
}

#[derive(Default)]
struct UsbFactory {
    serial: Option<String>,
}

#[async_trait(?Send)]
impl InterfaceFactory<Interface> for UsbFactory {
    async fn open(&mut self, target: &Target) -> Result<Interface> {
        let (s, usb) = target.usb();
        match usb {
            Some(iface) => {
                let mut in_use = SERIALS_IN_USE.lock().await;
                let _ = in_use.insert(s.clone());
                self.serial.replace(s.clone());
                tracing::debug!("serial now in use: {}", s);
                Ok(iface)
            }
            None => bail!("Could not open usb interface for target: {:?}", target),
        }
    }

    async fn close(&self) {
        let mut in_use = SERIALS_IN_USE.lock().await;
        if let Some(s) = &self.serial {
            tracing::debug!("dropping in use serial: {}", s);
            let _ = in_use.remove(s);
        }
    }
}

impl Drop for UsbFactory {
    fn drop(&mut self) {
        futures::executor::block_on(async move {
            self.close().await;
        });
    }
}

//TODO(fxbug.dev/52733) - this info will probably get rolled into the target struct
#[derive(Debug)]
pub struct FastbootDevice {
    pub product: String,
    pub serial: String,
}

pub struct VariableListener(VariableListenerProxy);

impl VariableListener {
    fn new(listener: ClientEnd<VariableListenerMarker>) -> Result<Self> {
        Ok(Self(listener.into_proxy().map_err(|e| anyhow!(e))?))
    }
}

impl fastboot::InfoListener for VariableListener {
    fn on_info(&self, info: String) -> Result<()> {
        if let Some((name, val)) = info.split_once(':') {
            self.0.on_variable(name.trim(), val.trim()).map_err(|e| anyhow!(e))?;
        }
        Ok(())
    }
}

pub struct UploadProgressListener(UploadProgressListenerProxy);

impl UploadProgressListener {
    fn new(listener: ClientEnd<UploadProgressListenerMarker>) -> Result<Self> {
        Ok(Self(listener.into_proxy().map_err(|e| anyhow!(e))?))
    }
}

impl fastboot::UploadProgressListener for UploadProgressListener {
    fn on_started(&self, size: usize) -> Result<()> {
        self.0.on_started(size.try_into()?).map_err(|e| anyhow!(e))
    }

    fn on_progress(&self, bytes_written: u64) -> Result<()> {
        self.0.on_progress(bytes_written).map_err(|e| anyhow!(e))
    }

    fn on_error(&self, error: &str) -> Result<()> {
        self.0.on_error(error).map_err(|e| anyhow!(e))
    }

    fn on_finished(&self) -> Result<()> {
        self.0.on_finished().map_err(|e| anyhow!(e))
    }
}

fn is_fastboot_match(info: &InterfaceInfo) -> bool {
    (info.dev_vendor == 0x18d1) && ((info.dev_product == 0x4ee0) || (info.dev_product == 0x0d02))
}

fn extract_serial_number(info: &InterfaceInfo) -> String {
    let null_pos = match info.serial_number.iter().position(|&c| c == 0) {
        Some(p) => p,
        None => {
            return "".to_string();
        }
    };
    (*String::from_utf8_lossy(&info.serial_number[..null_pos])).to_string()
}

fn open_interface<F>(mut cb: F) -> Result<Interface>
where
    F: FnMut(&InterfaceInfo) -> bool,
{
    tracing::trace!("opening usb interface via open_interface");

    let mut open_cb = |info: &InterfaceInfo| -> bool {
        if is_fastboot_match(info) {
            cb(info)
        } else {
            // Do not open.
            false
        }
    };
    Interface::open(&mut open_cb)
}

fn enumerate_interfaces<F>(mut cb: F)
where
    F: FnMut(&InterfaceInfo),
{
    tracing::trace!("usb calling enumerate_interface");
    let mut cb = |info: &InterfaceInfo| -> bool {
        if is_fastboot_match(info) {
            cb(info)
        }
        // Do not open anything.
        false
    };
    let _result = Interface::open(&mut cb);
}

fn find_serial_numbers() -> Vec<String> {
    let mut serials = Vec::new();
    let cb = |info: &InterfaceInfo| serials.push(extract_serial_number(info));
    enumerate_interfaces(cb);
    serials
}

pub async fn find_devices() -> Vec<FastbootDevice> {
    let mut products = Vec::new();
    let is_disabled: bool = get("fastboot.usb.disabled").await.unwrap_or(false);
    if is_disabled {
        return products;
    }

    tracing::trace!("discovering fastboot devices via usb");

    let serials = find_serial_numbers();
    let in_use = SERIALS_IN_USE.lock().await;
    // Don't probe in-use clients
    let serials_to_probe = serials.into_iter().filter(|s| !in_use.contains(s));
    for serial in serials_to_probe {
        match open_interface_with_serial(&serial) {
            Ok(mut usb_interface) => {
                if let Ok(Reply::Okay(version)) =
                    send(Command::GetVar(ClientVariable::Version), &mut usb_interface).await
                {
                    // Only support 0.4 right now.
                    if version == "0.4".to_string() {
                        if let Ok(Reply::Okay(product)) =
                            send(Command::GetVar(ClientVariable::Product), &mut usb_interface).await
                        {
                            products.push(FastbootDevice { product, serial: serial.to_string() })
                        }
                    }
                }
            }
            Err(e) => tracing::error!("error opening usb interface: {}", e),
        }
    }
    products
}

pub fn open_interface_with_serial(serial: &String) -> Result<Interface> {
    tracing::trace!("opening interface with serial number: {}", serial);
    open_interface(|info: &InterfaceInfo| -> bool { extract_serial_number(info) == *serial })
}

pub async fn get_all_vars<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    listener: &VariableListener,
) -> Result<()> {
    send_with_listener(Command::GetVar(ClientVariable::All), interface, listener).await.and_then(
        |r| match r {
            Reply::Okay(_) => Ok(()),
            Reply::Fail(s) => bail!("Failed to get all variables {}", s),
            _ => bail!("Unexpected reply from fastboot deviced for get_var"),
        },
    )
}

pub async fn get_var<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    name: &String,
) -> Result<String> {
    send(Command::GetVar(ClientVariable::Oem(name.to_string())), interface).await.and_then(|r| {
        match r {
            Reply::Okay(v) => Ok(v),
            Reply::Fail(s) => bail!("Failed to get {}: {}", name, s),
            _ => bail!("Unexpected reply from fastboot deviced for get_var"),
        }
    })
}

pub async fn stage<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    file: &String,
    listener: &UploadProgressListener,
) -> Result<()> {
    let bytes = read(file)?;
    tracing::debug!("uploading file size: {}", bytes.len());
    match upload(&bytes[..], interface, listener).await.context(format!("uploading {}", file))? {
        Reply::Okay(s) => {
            tracing::debug!("Received response from download command: {}", s);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to upload {}: {}", file, s),
        _ => bail!("Unexpected reply from fastboot device for download"),
    }
}

pub async fn flash<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    file: &String,
    name: &String,
    listener: &UploadProgressListener,
) -> Result<()> {
    let bytes = read(file)?;
    let upload_reply =
        upload(&bytes[..], interface, listener).await.context(format!("uploading {}", file))?;
    match upload_reply {
        Reply::Okay(s) => tracing::debug!("Received response from download command: {}", s),
        Reply::Fail(s) => bail!("Failed to upload {}: {}", file, s),
        _ => bail!("Unexpected reply from fastboot device for download: {:?}", upload_reply),
    };
    //timeout rate is in mb per seconds
    let min_timeout: i64 = get(MIN_FLASH_TIMEOUT).await?;
    let timeout_rate: i64 = get(FLASH_TIMEOUT_RATE).await?;
    let megabytes = (bytes.len() / 1000000) as i64;
    let mut timeout = megabytes / timeout_rate;
    timeout = std::cmp::max(timeout, min_timeout);
    tracing::debug!("Estimated timeout: {}s for {}MB", timeout, megabytes);
    let send_reply =
        send_with_timeout(Command::Flash(name.to_string()), interface, Duration::seconds(timeout))
            .await
            .context("sending flash");
    match send_reply {
        Ok(Reply::Okay(_)) => Ok(()),
        Ok(Reply::Fail(s)) => bail!("Failed to flash \"{}\": {}", name, s),
        Ok(_) => bail!("Unexpected reply from fastboot device for flash command"),
        Err(ref e) => {
            if let Some(ffx_err) = e.downcast_ref::<SendError>() {
                match ffx_err {
                    SendError::Timeout => {
                        if timeout_rate == 1 {
                            bail!("Could not read response from device.  Reply timed out.");
                        }
                        let lowered_rate = timeout_rate - 1;
                        let timeout_err = format!(
                            "Time out while waiting on a response from the device. \n\
                            The current timeout rate is {} mb/s.  Try lowering the timeout rate: \n\
                            ffx config set \"{}\" {}",
                            timeout_rate, FLASH_TIMEOUT_RATE, lowered_rate
                        );
                        bail!("{}", timeout_err);
                    }
                }
            }
            bail!("Unexpected reply from fastboot device for flash command: {:?}", send_reply)
        }
    }
}

pub async fn erase<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    name: &String,
) -> Result<()> {
    let reply = send(Command::Erase(name.to_string()), interface).await.context("sending erase")?;
    match reply {
        Reply::Okay(_) => {
            tracing::debug!("Successfully erased parition: {}", name);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to erase \"{}\": {}", name, s),
        _ => bail!("Unexpected reply from fastboot device for erase command: {:?}", reply),
    }
}

pub async fn boot<T: AsyncRead + AsyncWrite + Unpin>(interface: &mut T) -> Result<()> {
    let reply = send(Command::Boot, interface).await.context("sending boot")?;
    match reply {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent boot");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to boot: {}", s),
        _ => bail!("Unexpected reply from fastboot device for boot command: {:?}", reply),
    }
}

pub async fn reboot<T: AsyncRead + AsyncWrite + Unpin>(interface: &mut T) -> Result<()> {
    let reply = send(Command::Reboot, interface).await.context("sending reboot")?;
    match reply {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent reboot");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to reboot: {}", s),
        _ => bail!("Unexpected reply from fastboot device for reboot command: {:?}", reply),
    }
}

pub async fn reboot_bootloader<T: AsyncRead + AsyncWrite + Unpin>(interface: &mut T) -> Result<()> {
    match send(Command::RebootBootLoader, interface).await.context("sending reboot bootloader")? {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent reboot bootloader");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to reboot to bootloader: {}", s),
        _ => bail!("Unexpected reply from fastboot device for reboot bootloader command"),
    }
}

pub async fn continue_boot<T: AsyncRead + AsyncWrite + Unpin>(interface: &mut T) -> Result<()> {
    match send(Command::Continue, interface).await.context("sending continue")? {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent continue");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to continue: {}", s),
        _ => bail!("Unexpected reply from fastboot device for continue command"),
    }
}

pub async fn set_active<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    slot: &String,
) -> Result<()> {
    match send(Command::SetActive(slot.to_string()), interface)
        .await
        .context("sending set_active")?
    {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent set_active");
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to set_active: {}", s),
        _ => bail!("Unexpected reply from fastboot device for set_active command"),
    }
}

pub async fn oem<T: AsyncRead + AsyncWrite + Unpin>(interface: &mut T, cmd: &String) -> Result<()> {
    match send(Command::Oem(cmd.to_string()), interface).await.context("sending oem")? {
        Reply::Okay(_) => {
            tracing::debug!("Successfully sent oem command \"{}\"", cmd);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to oem \"{}\": {}", cmd, s),
        _ => bail!("Unexpected reply from fastboot device for oem command \"{}\"", cmd),
    }
}

pub async fn get_staged<T: AsyncRead + AsyncWrite + Unpin>(
    interface: &mut T,
    file: &String,
) -> Result<()> {
    match download(file, interface).await.context(format!("downloading to {}", file))? {
        Reply::Okay(_) => {
            tracing::debug!("Successfully downloaded to \"{}\"", file);
            Ok(())
        }
        Reply::Fail(s) => bail!("Failed to download: {}", s),
        _ => bail!("Unexpected reply from fastboot device for download command"),
    }
}
