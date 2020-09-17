// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::Error as FidlError;
use fidl_fuchsia_mem::Buffer;
#[cfg(not(test))]
use fidl_fuchsia_paver::PaverMarker;
use fidl_fuchsia_paver::{SysconfigMarker, SysconfigProxy};
#[cfg(not(test))]
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::Status;
use thiserror::Error;

pub mod channel;

#[derive(Debug, Error)]
pub enum SysconfigError {
    #[error("Out of range: {}", _0)]
    OutOfRange(usize),

    #[error("Sysconfig Fidl Error: {:?}", _0)]
    SysconfigFidl(FidlError),

    #[error("Zircon Error: {:?}", _0)]
    Zircon(Status),

    #[error("Service Connection")]
    ServiceConnection,

    #[error("Sysconfig Proxy Creation")]
    SysconfigProxy,
}

impl From<FidlError> for SysconfigError {
    fn from(e: FidlError) -> Self {
        SysconfigError::SysconfigFidl(e)
    }
}

impl From<Status> for SysconfigError {
    fn from(e: Status) -> Self {
        SysconfigError::Zircon(e)
    }
}

#[cfg(not(test))]
pub fn get_sysconfig_proxy() -> Result<SysconfigProxy, SysconfigError> {
    let paver =
        connect_to_service::<PaverMarker>().map_err(|_| SysconfigError::ServiceConnection)?;
    let (sysconfig, server_end) = fidl::endpoints::create_proxy::<SysconfigMarker>()
        .map_err(|_| SysconfigError::SysconfigProxy)?;
    let () = paver.find_sysconfig(server_end)?;

    Ok(sysconfig)
}

#[cfg(test)]
use sysconfig_mock::get_sysconfig_proxy;

pub async fn read_sysconfig_partition() -> Result<Vec<u8>, SysconfigError> {
    let sysconfig = get_sysconfig_proxy()?;
    let read_result = sysconfig.read().await?;
    let buffer = read_result.map_err(|status| Status::from_raw(status))?;
    let get_size_result = sysconfig.get_partition_size().await?;
    let ptn_size = get_size_result.map_err(|status| Status::from_raw(status))?;
    let mut ret = vec![0u8; ptn_size as usize];
    buffer.vmo.read(ret.as_mut_slice(), 0)?;
    Ok(ret)
}

pub async fn write_sysconfig_partition(data: &[u8]) -> Result<(), SysconfigError> {
    let sysconfig = get_sysconfig_proxy()?;
    let get_size_result = sysconfig.get_partition_size().await?;
    let ptn_size = get_size_result.map_err(|status| Status::from_raw(status))?;

    if data.len() > ptn_size as usize {
        return Err(SysconfigError::OutOfRange(data.len()));
    }

    let mut buf =
        Buffer { vmo: fuchsia_zircon::Vmo::create(ptn_size as u64)?, size: ptn_size as u64 };
    buf.vmo.write(data, 0)?;
    let write_result = sysconfig.write(&mut buf).await?;
    Status::ok(write_result)?;
    let flush_result = sysconfig.flush().await?;
    Status::ok(flush_result)?;
    Ok(())
}

#[cfg(test)]
mod sysconfig_mock {
    use super::*;
    use fasync::futures::TryStreamExt;
    use fidl_fuchsia_paver::SysconfigRequest;
    use fuchsia_async as fasync;
    use std::cell::RefCell;
    pub const SYSCONFIG_PARTITION_SIZE: usize = 4096;

    thread_local!(pub static DATA: RefCell<Vec<u8>> = RefCell::new(vec![0; SYSCONFIG_PARTITION_SIZE]));

    pub fn get_data() -> Vec<u8> {
        DATA.with(|data| data.borrow().clone())
    }

    pub fn set_data(new_data: Vec<u8>) {
        DATA.with(|data| *data.borrow_mut() = new_data);
    }

    pub fn spawn_sysconfig_service() -> SysconfigProxy {
        let (sysconfig_proxy, mut sysconfig_stream) =
            fidl::endpoints::create_proxy_and_stream::<SysconfigMarker>().unwrap();
        fasync::Task::spawn(async move {
            while let Some(req) = sysconfig_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    SysconfigRequest::Read { responder } => {
                        let buf = Buffer {
                            vmo: fuchsia_zircon::Vmo::create(SYSCONFIG_PARTITION_SIZE as u64)
                                .unwrap(),
                            size: SYSCONFIG_PARTITION_SIZE as u64,
                        };
                        buf.vmo.write(get_data().as_mut_slice(), 0).unwrap();
                        responder.send(&mut Ok(buf)).expect("sysconfig response to send");
                    }
                    SysconfigRequest::Write { payload, responder } => {
                        assert_eq!(payload.size as usize, SYSCONFIG_PARTITION_SIZE);
                        let mut temp: Vec<u8> = vec![0; SYSCONFIG_PARTITION_SIZE];
                        payload.vmo.read(temp.as_mut_slice(), 0).unwrap();
                        set_data(temp);
                        responder.send(Status::OK.into_raw()).expect("sysconfig response to send");
                    }
                    SysconfigRequest::GetPartitionSize { responder } => {
                        responder
                            .send(&mut Ok(SYSCONFIG_PARTITION_SIZE as u64))
                            .expect("sysconfig response to send");
                    }
                    SysconfigRequest::Flush { responder } => {
                        responder.send(Status::OK.into_raw()).expect("sysconfig response to send");
                    }
                    _ => {}
                }
            }
        })
        .detach();
        sysconfig_proxy
    }

    pub fn get_sysconfig_proxy() -> Result<SysconfigProxy, SysconfigError> {
        Ok(spawn_sysconfig_service())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use matches::assert_matches;
    use sysconfig_mock::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sysconfig_partition() {
        let expected: Vec<u8> = vec![0x4a; SYSCONFIG_PARTITION_SIZE];
        set_data(expected.clone());
        assert_eq!(read_sysconfig_partition().await.unwrap(), expected)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_sysconfig_partition_out_of_range() {
        const LEN: usize = SYSCONFIG_PARTITION_SIZE * 2;
        let data: [u8; LEN] = [0; LEN];
        assert_matches!(
            write_sysconfig_partition(&data).await,
            Err(SysconfigError::OutOfRange(LEN))
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_sysconfig_partition() {
        let mut expected: Vec<u8> = vec![0x4a; SYSCONFIG_PARTITION_SIZE];
        write_sysconfig_partition(&expected.as_mut_slice()).await.unwrap();
        assert_eq!(expected, get_data().to_vec())
    }
}
