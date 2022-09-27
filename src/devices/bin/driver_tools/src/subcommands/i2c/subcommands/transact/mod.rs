// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    anyhow::{Context, Result},
    args::TransactCommand,
    fidl_fuchsia_hardware_i2c as fi2c, fidl_fuchsia_io as fio, fuchsia_zircon_status as zx,
    std::{fmt::Display, io::Write},
};

fn parse_transactions<T: AsRef<str> + Display>(
    src: impl IntoIterator<Item = T>,
) -> Result<Vec<fi2c::Transaction>> {
    let mut transactions = vec![];
    let mut src = src.into_iter().peekable();
    while let Some(ty) = src.next() {
        match ty.as_ref() {
            "r" => {
                let length = if let Some(length) = src.next() {
                    u32::from_str_radix(length.as_ref(), 10).with_context(|| {
                        format!("Failed to parse \"{}\" as length for read transaction", length)
                    })?
                } else {
                    anyhow::bail!("Missing length of read transaction");
                };
                transactions.push(fi2c::Transaction {
                    data_transfer: Some(fi2c::DataTransfer::ReadSize(length)),
                    ..fi2c::Transaction::EMPTY
                });
            }
            "w" => {
                let mut data = vec![];
                while let Some(next) = src.peek() {
                    let next = next.as_ref();
                    if next == "r" || next == "w" {
                        break;
                    }
                    let byte = src.next().unwrap();
                    let byte = u8::from_str_radix(byte.as_ref(), 10).with_context(|| {
                        format!("Failed to parse \"{}\" as data for write transaction", byte)
                    })?;
                    data.push(byte);
                }
                transactions.push(fi2c::Transaction {
                    data_transfer: Some(fi2c::DataTransfer::WriteData(data)),
                    ..fi2c::Transaction::EMPTY
                });
            }
            ty => {
                anyhow::bail!("Failed to parse unknown transaction type \"{}\"", ty);
            }
        }
    }
    Ok(transactions)
}

pub async fn transact(
    cmd: &TransactCommand,
    writer: &mut impl Write,
    dev: &fio::DirectoryProxy,
) -> Result<()> {
    let transactions =
        parse_transactions(&cmd.transactions).context("Failed to parse transactions")?;
    let write_data: Vec<Vec<u8>> = transactions
        .iter()
        .filter_map(|transaction| match transaction.data_transfer {
            Some(fi2c::DataTransfer::WriteData(ref data)) => Some(data.clone()),
            _ => None,
        })
        .collect();
    let device = super::connect_to_i2c_device(&cmd.device_path, dev)
        .context("Failed to connect to I2C device")?;
    let read_data = device
        .transfer(&mut transactions.into_iter())
        .await
        .context("Failed to send request to transfer transactions to I2C device")?
        .map_err(|status| zx::Status::from_raw(status))
        .context("Failed to transfer transactions to I2C device")?;
    if write_data.len() > 0 {
        write!(writer, "Writes:")?;
        for write in write_data.iter() {
            for byte in write.iter() {
                write!(writer, " {:#04x}", byte)?;
            }
        }
        writeln!(writer, "")?;
    }
    if read_data.len() > 0 {
        write!(writer, "Reads:")?;
        for line in read_data.iter() {
            for byte in line.iter() {
                write!(writer, " {:#04x}", byte)?;
            }
        }
        writeln!(writer, "")?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Result, fidl_fuchsia_hardware_i2c as fi2c};

    fn i2c_read(length: u32) -> fi2c::Transaction {
        fi2c::Transaction {
            data_transfer: Some(fi2c::DataTransfer::ReadSize(length)),
            ..fi2c::Transaction::EMPTY
        }
    }

    fn i2c_write(data: &[u8]) -> fi2c::Transaction {
        fi2c::Transaction {
            data_transfer: Some(fi2c::DataTransfer::WriteData(data.into())),
            ..fi2c::Transaction::EMPTY
        }
    }

    #[test]
    fn test_parse_transactions_empty() -> Result<()> {
        let transactions = parse_transactions(&([] as [&str; 0]))?;
        assert_eq!(transactions, []);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_single_read() -> Result<()> {
        let transactions = parse_transactions(&["r", "204"])?;
        assert_eq!(transactions, vec![i2c_read(204)]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_two_reads() -> Result<()> {
        let transactions = parse_transactions(&["r", "1", "r", "183"])?;
        assert_eq!(transactions, [i2c_read(1), i2c_read(183)]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_single_write() -> Result<()> {
        let transactions = parse_transactions(&["w", "101", "2", "56"])?;
        assert_eq!(transactions, [i2c_write(&[101, 2, 56])]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_two_writes() -> Result<()> {
        let transactions = parse_transactions(&["w", "99", "w", "222", "21", "88"])?;
        assert_eq!(transactions, vec![i2c_write(&[99]), i2c_write(&[222, 21, 88])]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_empty_write() -> Result<()> {
        let transactions = parse_transactions(&["w"])?;
        assert_eq!(transactions, vec![i2c_write(&[])]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_read_then_write() -> Result<()> {
        let transactions = parse_transactions(&["r", "80", "w", "255", "0"])?;
        assert_eq!(transactions, vec![i2c_read(80), i2c_write(&[255, 0])]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_write_then_read() -> Result<()> {
        let transactions = parse_transactions(&["w", "1", "199", "r", "64"])?;
        assert_eq!(transactions, vec![i2c_write(&[1, 199]), i2c_read(64)]);
        Ok(())
    }

    #[test]
    fn test_parse_transactions_multiple_reads_and_writes() -> Result<()> {
        let transactions = parse_transactions(&[
            "w", "w", "40", "99", "r", "50", "w", "9", "12", "r", "37", "w", "r", "106",
        ])?;
        assert_eq!(
            transactions,
            [
                i2c_write(&[]),
                i2c_write(&[40, 99]),
                i2c_read(50),
                i2c_write(&[9, 12]),
                i2c_read(37),
                i2c_write(&[]),
                i2c_read(106),
            ]
        );
        Ok(())
    }

    #[test]
    fn test_parse_transactions_invalid_transaction() -> Result<()> {
        let res = parse_transactions(&["x"]);
        assert!(res.is_err());
        Ok(())
    }

    #[test]
    fn test_parse_transactions_missing_read_length() -> Result<()> {
        let res = parse_transactions(&["r"]);
        assert!(res.is_err());
        Ok(())
    }

    #[test]
    fn test_parse_transactions_invalid_read_length() -> Result<()> {
        let res = parse_transactions(&["r", "foo"]);
        assert!(res.is_err());
        Ok(())
    }

    #[test]
    fn test_parse_transactions_invalid_write_data() -> Result<()> {
        let res = parse_transactions(&["w", "1", "bar", "2"]);
        assert!(res.is_err());
        Ok(())
    }
}
