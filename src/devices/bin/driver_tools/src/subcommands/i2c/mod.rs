// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

mod subcommands;

use {
    anyhow::{Context, Result},
    args::{I2cCommand, I2cSubCommand},
    fidl_fuchsia_io as fio,
    std::io::Write,
};

pub async fn i2c(
    cmd: &I2cCommand,
    writer: &mut impl Write,
    dev: &fio::DirectoryProxy,
) -> Result<()> {
    match cmd.subcommand {
        I2cSubCommand::Ping(ref subcmd) => {
            subcommands::ping::ping(subcmd, writer, dev).await.context("Ping subcommand failed")?;
        }
        I2cSubCommand::Read(ref subcmd) => {
            subcommands::read::read(subcmd, writer, dev).await.context("Read subcommand failed")?;
        }
        I2cSubCommand::Transact(ref subcmd) => {
            subcommands::transact::transact(subcmd, writer, dev)
                .await
                .context("Transact subcommand failed")?;
        }
        I2cSubCommand::Write(ref subcmd) => {
            subcommands::write::write(subcmd, writer, dev)
                .await
                .context("Writer subcommand failed")?;
        }
    };
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{Context, Result},
        argh::FromArgs,
        fidl_fuchsia_hardware_i2c as fi2c, fuchsia_async as fasync,
        fuchsia_component::server::{FidlService, ServiceFs},
        futures::{future, lock::Mutex, Future, FutureExt, StreamExt, TryStreamExt},
        std::sync::Arc,
    };

    enum I2cDeviceRequestStream {
        I2cDeviceA(fi2c::DeviceRequestStream),
        I2cDeviceB(fi2c::DeviceRequestStream),
    }

    /// Creates two mock I2C devices at `/dev/class/i2c/A` and
    /// `/dev/class/i2c/B`, invokes `i2c` function with `cmd`, and invokes
    /// `on_i2c_device_a_request` and `on_i2c_device_b_request` whenever the
    /// mock I2c device `A` and `B` receive an I2C device request respectively.
    /// The output of `i2c` that is normally written to its `writer`
    /// parameter is returned.
    async fn test_i2c<
        AFut: Future<Output = Result<()>> + Send + 'static,
        BFut: Future<Output = Result<()>> + Send + 'static,
    >(
        cmd: I2cCommand,
        on_i2c_device_a_request: impl Fn(fi2c::DeviceRequest) -> AFut,
        on_i2c_device_b_request: impl Fn(fi2c::DeviceRequest) -> BFut,
    ) -> Result<String> {
        // Create a virtual file system that can serve I2C devices.
        let mut service_fs = ServiceFs::new_local();
        let mut dir = service_fs.dir("class");
        let mut dir = dir.dir("i2c");
        dir.add_service_at("A", FidlService::from(I2cDeviceRequestStream::I2cDeviceA));
        dir.add_service_at("B", FidlService::from(I2cDeviceRequestStream::I2cDeviceB));

        // Create a directory proxy to access the I2C devices.
        let (dev, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .context("Failed to create FIDL proxy")?;
        service_fs
            .serve_connection(server_end.into_channel())
            .context("Failed to serve connection")?;

        // Run the command and mock I2C device servers.
        let mut writer = Vec::new();
        futures::select! {
            _ = service_fs.for_each_concurrent(None, |request: I2cDeviceRequestStream| async {
                match request {
                    I2cDeviceRequestStream::I2cDeviceA(stream) => if let Err(e) = stream
                            .map(|result| result.context("Failed to get I2C device request for I2C device A"))
                            .try_for_each(|request| on_i2c_device_a_request(request))
                            .await
                        {
                            panic!("Failed to handle I2C device requests for I2C device A: {:?}", e);
                        },
                        I2cDeviceRequestStream::I2cDeviceB(stream) => if let Err(e) = stream
                            .map(|result| result.context("Failed to get I2C device request for I2C device B"))
                            .try_for_each(|request| on_i2c_device_b_request(request))
                            .await
                        {
                            panic!("Failed to handle I2C device requests for I2C device B: {:?}", e);
                        }
                }
            }) => {
                anyhow::bail!("Prematurely completed serving I2C device requests");
            },
            res = i2c(&cmd, &mut writer, &dev).fuse() => res.unwrap(),
        }

        String::from_utf8(writer).context("Failed to convert i2c output to a string")
    }

    fn verify_write_transaction(
        transaction: &fi2c::Transaction,
        expected_write_data: &[u8],
    ) -> Result<()> {
        match transaction.data_transfer {
            Some(fi2c::DataTransfer::WriteData(ref data)) => {
                if data != expected_write_data {
                    anyhow::bail!(
                        "Expected write data to be {:?} but was actually {:?}",
                        expected_write_data,
                        data
                    );
                }
            }
            None => anyhow::bail!("Transaction missing data transfer"),
            _ => anyhow::bail!("Transaction is not a write"),
        }
        Ok(())
    }

    fn verify_read_transaction(
        transaction: &fi2c::Transaction,
        expected_read_size: u32,
    ) -> Result<()> {
        match transaction.data_transfer {
            Some(fi2c::DataTransfer::ReadSize(size)) => {
                if size != expected_read_size {
                    anyhow::bail!(
                        "Expected read size to be {} but was actually {}",
                        expected_read_size,
                        size
                    );
                }
            }
            None => anyhow::bail!("Transaction missing data transfer"),
            _ => anyhow::bail!("Transaction is not a read"),
        }
        Ok(())
    }

    fn verify_read_byte_transactions(
        transactions: &[fi2c::Transaction],
        expected_address: &[u8],
    ) -> Result<()> {
        if transactions.len() != 2 {
            anyhow::bail!("Expected 2 transactions: Received {}", transactions.len());
        }
        verify_write_transaction(&transactions[0], expected_address)
            .context("Failed to verify first transaction")?;
        verify_read_transaction(&transactions[1], 1)
            .context("Failed to verify second transaction")?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_ping() -> Result<()> {
        let cmd = I2cCommand::from_args(&["i2c"], &["ping"]).unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let num_pings_received = Arc::new(Mutex::new(0));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let num_pings_received = Arc::clone(&num_pings_received);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            verify_read_byte_transactions(&transactions, &[0])
                                .context("Failed to verify ping")?;
                            *num_pings_received.lock().await += 1;
                            responder
                                .send(&mut Ok(vec![vec![0]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let num_pings_received = Arc::clone(&num_pings_received);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            verify_read_byte_transactions(&transactions, &[0])
                                .context("Failed to verify ping")?;
                            *num_pings_received.lock().await += 1;
                            responder
                                .send(&mut Ok(vec![vec![0]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
        )
        .await?;

        assert_eq!(
            output,
            r#"class/i2c/A: OK
class/i2c/B: OK
"#
        );
        assert_eq!(*num_pings_received.lock().await, 2);
        assert_eq!(*num_transfer_requests_received.lock().await, 2);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read() -> Result<()> {
        let cmd = I2cCommand::from_args(&["i2c"], &["read", "class/i2c/A", "134", "12"]).unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let received_expected_transfer_request = Arc::new(Mutex::new(false));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let received_expected_transfer_request =
                    Arc::clone(&received_expected_transfer_request);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            verify_read_byte_transactions(&transactions, &[134, 12])
                                .context("Failed to verify transaction")?;
                            *received_expected_transfer_request.lock().await = true;
                            responder
                                .send(&mut Ok(vec![vec![245]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |_| future::ready(Ok(())),
        )
        .await?;

        assert_eq!(
            output,
            r#"Read from 0x86 0x0c: 0xf5
"#
        );
        assert!(*received_expected_transfer_request.lock().await);
        assert_eq!(*num_transfer_requests_received.lock().await, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_empty() -> Result<()> {
        let cmd = I2cCommand::from_args(&["i2c"], &["write", "class/i2c/A"]).unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let received_expected_transfer_request = Arc::new(Mutex::new(false));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let received_expected_transfer_request =
                    Arc::clone(&received_expected_transfer_request);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            if transactions.len() != 1 {
                                anyhow::bail!(
                                    "Expected 1 transaction: Received {}",
                                    transactions.len()
                                );
                            }
                            verify_write_transaction(&transactions[0], &[])
                                .context("Failed to verify transaction")?;
                            *received_expected_transfer_request.lock().await = true;
                            responder
                                .send(&mut Ok(vec![vec![]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |_| future::ready(Ok(())),
        )
        .await?;

        assert_eq!(
            output,
            r#"Write:
"#
        );
        assert!(*received_expected_transfer_request.lock().await);
        assert_eq!(*num_transfer_requests_received.lock().await, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write() -> Result<()> {
        let cmd =
            I2cCommand::from_args(&["i2c"], &["write", "class/i2c/A", "2", "255", "0"]).unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let received_expected_transfer_request = Arc::new(Mutex::new(false));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let received_expected_transfer_request =
                    Arc::clone(&received_expected_transfer_request);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            if transactions.len() != 1 {
                                anyhow::bail!(
                                    "Expected 1 transaction: Received {}",
                                    transactions.len()
                                );
                            }
                            verify_write_transaction(&transactions[0], &[2, 255, 0])
                                .context("Failed to verify transaction")?;
                            *received_expected_transfer_request.lock().await = true;
                            responder
                                .send(&mut Ok(vec![vec![]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |_| future::ready(Ok(())),
        )
        .await?;

        assert_eq!(
            output,
            r#"Write: 0x02 0xff 0x00
"#
        );
        assert!(*received_expected_transfer_request.lock().await);
        assert_eq!(*num_transfer_requests_received.lock().await, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_transact_empty() -> Result<()> {
        let cmd = I2cCommand::from_args(&["i2c"], &["transact", "class/i2c/A"]).unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let received_expected_transfer_request = Arc::new(Mutex::new(false));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let received_expected_transfer_request =
                    Arc::clone(&received_expected_transfer_request);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            if transactions.len() != 0 {
                                anyhow::bail!(
                                    "Expected 0 transactions: Received {}",
                                    transactions.len()
                                );
                            }
                            *received_expected_transfer_request.lock().await = true;
                            responder
                                .send(&mut Ok(vec![]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |_| future::ready(Ok(())),
        )
        .await?;

        assert_eq!(output, r#""#);
        assert!(*received_expected_transfer_request.lock().await);
        assert_eq!(*num_transfer_requests_received.lock().await, 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_transact() -> Result<()> {
        let cmd = I2cCommand::from_args(
            &["i2c"],
            &["transact", "class/i2c/A", "r", "77", "w", "w", "45", "106", "r", "0", "w", "0"],
        )
        .unwrap();
        let num_transfer_requests_received = Arc::new(Mutex::new(0));
        let received_expected_transfer_request = Arc::new(Mutex::new(false));
        let output = test_i2c(
            cmd,
            |request: fi2c::DeviceRequest| {
                let num_transfer_requests_received = Arc::clone(&num_transfer_requests_received);
                let received_expected_transfer_request =
                    Arc::clone(&received_expected_transfer_request);
                async move {
                    match request {
                        fi2c::DeviceRequest::Transfer { transactions, responder } => {
                            *num_transfer_requests_received.lock().await += 1;
                            if transactions.len() != 5 {
                                anyhow::bail!(
                                    "Expected 5 transactionss: Received {}",
                                    transactions.len()
                                );
                            }
                            verify_read_transaction(&transactions[0], 77)
                                .context("Failed to verify first transaction")?;
                            verify_write_transaction(&transactions[1], &[])
                                .context("Failed to verify second transaction")?;
                            verify_write_transaction(&transactions[2], &[45, 106])
                                .context("Failed to verify third transaction")?;
                            verify_read_transaction(&transactions[3], 0)
                                .context("Failed to verify fourth transaction")?;
                            verify_write_transaction(&transactions[4], &[0])
                                .context("Failed to verify fifth transaction")?;
                            *received_expected_transfer_request.lock().await = true;
                            responder
                                .send(&mut Ok(vec![vec![26, 45], vec![8]]))
                                .or_else(|err| if err.is_closed() { Ok(()) } else { Err(err) })
                                .context("Failed to respond to Transfer request")?;
                        }
                    }
                    Ok(())
                }
            },
            |_| future::ready(Ok(())),
        )
        .await?;

        assert_eq!(
            output,
            r#"Writes: 0x2d 0x6a 0x00
Reads: 0x1a 0x2d 0x08
"#
        );
        assert!(*received_expected_transfer_request.lock().await);
        assert_eq!(*num_transfer_requests_received.lock().await, 1);
        Ok(())
    }
}
