// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    command::Command,
    reply::Reply,
    std::convert::TryFrom,
    std::io::{Read, Write},
};

mod command;
mod reply;

const MAX_PACKET_SIZE: usize = 64;

fn read_from_interface<T: Read>(interface: &mut T) -> Result<Reply, Error> {
    let mut buf: [u8; MAX_PACKET_SIZE] = [0; MAX_PACKET_SIZE];
    let size = interface.read(&mut buf)?;
    let (trimmed, _) = buf.split_at(size);
    Reply::try_from(trimmed.to_vec())
}

fn read_and_print_info<T: Read>(interface: &mut T) -> Result<Reply, Error> {
    loop {
        let reply = read_from_interface(interface)?;
        match reply {
            Reply::Info(msg) => println!("{}", msg),
            _ => return Ok(reply),
        }
    }
}

pub fn send<T: Read + Write>(cmd: Command, interface: &mut T) -> Result<Reply, Error> {
    interface.write(&Vec::<u8>::try_from(cmd)?)?;
    read_and_print_info(interface)
}

pub fn upload<T: Read + Write>(data: &[u8], interface: &mut T) -> Result<Reply, Error> {
    let reply = send(Command::Download(u32::try_from(data.len())?), interface)?;
    match reply {
        Reply::Data(s) => {
            if s != u32::try_from(data.len())? {
                return Err(anyhow!(
                    "Target responded with wrong data size - received:{} expected:{}",
                    s,
                    data.len()
                ));
            }
            // TODO - possibly split the data based on the speed of the USB connection.
            // It's possible the USB library might handle this, but it's something to note:
            // Max packet size must be 64 bytes for full-speed, 512 bytes for high-speed and
            // 1024 bytes for Super Speed USB
            interface.write(data)?;
            read_and_print_info(interface)
        }
        _ => Err(anyhow!("Did not get expected Data reply")),
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use command::ClientVariable;

    struct TestTransport {
        replies: Vec<Reply>,
    }

    impl Read for TestTransport {
        fn read(&mut self, buf: &mut [u8]) -> Result<usize, std::io::Error> {
            match self.replies.pop() {
                Some(r) => {
                    let reply = Vec::<u8>::from(r);
                    buf[..reply.len()].copy_from_slice(&reply);
                    Ok(reply.len())
                }
                None => Ok(0),
            }
        }
    }

    impl Write for TestTransport {
        fn write(&mut self, buf: &[u8]) -> Result<usize, std::io::Error> {
            Ok(buf.len())
        }

        fn flush(&mut self) -> std::result::Result<(), std::io::Error> {
            unimplemented!()
        }
    }

    impl TestTransport {
        pub fn new() -> Self {
            TestTransport { replies: Vec::new() }
        }

        pub fn push(&mut self, reply: Reply) {
            self.replies.push(reply);
        }
    }

    #[test]
    fn test_send_does_not_return_info_replies() {
        let mut test_transport = TestTransport::new();
        test_transport.push(Reply::Okay("0.4".to_string()));
        let response = send(Command::GetVar(ClientVariable::Version), &mut test_transport);
        assert!(!response.is_err());
        assert_eq!(response.unwrap(), Reply::Okay("0.4".to_string()));

        test_transport.push(Reply::Okay("0.4".to_string()));
        test_transport.push(Reply::Info("Test".to_string()));
        let response_with_info =
            send(Command::GetVar(ClientVariable::Version), &mut test_transport);
        assert!(!response_with_info.is_err());
        assert_eq!(response_with_info.unwrap(), Reply::Okay("0.4".to_string()));

        test_transport.push(Reply::Okay("0.4".to_string()));
        for i in 0..10 {
            test_transport.push(Reply::Info(format!("Test {}", i).to_string()));
        }
        let response_with_info =
            send(Command::GetVar(ClientVariable::Version), &mut test_transport);
        assert!(!response_with_info.is_err());
        assert_eq!(response_with_info.unwrap(), Reply::Okay("0.4".to_string()));
    }

    #[test]
    fn test_uploading_data_to_partition() {
        let data: [u8; 1024] = [0; 1024];
        let mut test_transport = TestTransport::new();
        test_transport.push(Reply::Okay("Done Writing".to_string()));
        test_transport.push(Reply::Info("Writing".to_string()));
        test_transport.push(Reply::Data(1024));

        let response = upload(&data, &mut test_transport);
        assert!(!response.is_err());
        assert_eq!(response.unwrap(), Reply::Okay("Done Writing".to_string()));
    }

    #[test]
    fn test_uploading_data_with_unexpected_reply() {
        let data: [u8; 1024] = [0; 1024];
        let mut test_transport = TestTransport::new();
        test_transport.push(Reply::Info("Writing".to_string()));

        let response = upload(&data, &mut test_transport);
        assert!(response.is_err());
    }

    #[test]
    fn test_uploading_data_with_unexpected_data_size_reply() {
        let data: [u8; 1024] = [0; 1024];
        let mut test_transport = TestTransport::new();
        test_transport.push(Reply::Data(1000));

        let response = upload(&data, &mut test_transport);
        assert!(response.is_err());
    }
}
