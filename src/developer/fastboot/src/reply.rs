// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    std::convert::{From, TryFrom},
    std::fmt,
};

// Client response with a single packet no greater than 64 bytes. The first four bytes of the
// response are “OKAY”, “FAIL”, “DATA”, or “INFO”. Additional bytes may contain an (ascii)
// informative message.
#[derive(PartialEq, Clone)]
pub enum Reply {
    // INFO -> the remaining 60 bytes are an informative message (providing progress or diagnostic
    // messages).
    Info(String),
    // FAIL -> the requested command failed. The remaining 60 bytes of the response (if present)
    // provide a textual failure message to present to the user.
    Fail(String),
    // OKAY -> the requested command completed successfully.
    Okay(String),
    // DATA -> the requested command is ready for the data phase. A DATA response packet will be 12
    // bytes long, in the form of DATA00000000 where the 8 digit hexadecimal number represents the
    // total data size to transfer.
    Data(u32),
}

const MIN_REPLY_LENGTH: usize = 4;
const MAX_REPLY_LENGTH: usize = 64;
const DATA_SIZE_LENGTH: usize = 8;

impl TryFrom<Vec<u8>> for Reply {
    type Error = anyhow::Error;

    fn try_from(byte_vec: Vec<u8>) -> Result<Self, Self::Error> {
        if byte_vec.len() < MIN_REPLY_LENGTH {
            return Err(anyhow!("Fastboot reply must have {} bytes at least!", MIN_REPLY_LENGTH));
        }
        if byte_vec.len() > MAX_REPLY_LENGTH {
            return Err(anyhow!(
                "Fastboot reply must not have more than {} bytes",
                MAX_REPLY_LENGTH
            ));
        }

        let (reply_type, reply_data) = byte_vec.split_at(MIN_REPLY_LENGTH);
        let reply_type_str = String::from_utf8_lossy(reply_type).to_string().to_ascii_uppercase();
        let reply_data_str = String::from_utf8_lossy(reply_data).to_string();
        match reply_type_str.as_ref() {
            "INFO" => Ok(Reply::Info(reply_data_str)),
            "FAIL" => Ok(Reply::Fail(reply_data_str)),
            "OKAY" => Ok(Reply::Okay(reply_data_str)),
            "DATA" => {
                if reply_data_str.len() != DATA_SIZE_LENGTH {
                    return Err(anyhow!(
                        "DATA response packet size is {} expected {}",
                        reply_data_str.len(),
                        DATA_SIZE_LENGTH
                    ));
                }
                match u32::from_str_radix(&reply_data_str, 16) {
                    Ok(ds) => Ok(Reply::Data(ds)),
                    Err(e) => Err(anyhow!("Error parsing DATA reply size: {}", e)),
                }
            }
            _ => Err(anyhow!("Unknown reply type: {}", String::from_utf8_lossy(reply_type))),
        }
    }
}

impl From<Reply> for Vec<u8> {
    fn from(reply: Reply) -> Vec<u8> {
        match reply {
            Reply::Info(s) => [b"INFO", &s.into_bytes()[..]].concat(),
            Reply::Fail(s) => [b"FAIL", &s.into_bytes()[..]].concat(),
            Reply::Okay(s) => [b"OKAY", &s.into_bytes()[..]].concat(),
            Reply::Data(s) => [b"DATA", &format!("{:08X}", s).into_bytes()[..]].concat(),
        }
    }
}

fn write_output(reply: &Reply, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    match reply {
        Reply::Info(s) => write!(f, "INFO {}", s),
        Reply::Fail(s) => write!(f, "FAIL {}", s),
        Reply::Okay(s) => write!(f, "OKAY {}", s),
        Reply::Data(s) => write!(f, "DATA - size {}", s),
    }
}

impl fmt::Display for Reply {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write_output(self, f)
    }
}

impl fmt::Debug for Reply {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write_output(self, f)
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_info() {
        let info_prefix = vec![b'I', b'N', b'F', b'O'];
        let reply = Reply::try_from(info_prefix.clone()).unwrap();
        assert_eq!(reply, Reply::Info("".to_string()));

        let info_prefix_mix_case = vec![b'I', b'n', b'f', b'O'];
        let reply_mix_case = Reply::try_from(info_prefix_mix_case.clone()).unwrap();
        assert_eq!(reply_mix_case, Reply::Info("".to_string()));

        let info_message = vec![b'T', b'e', b's', b't'];
        let concat = [&info_prefix[..], &info_message[..]].concat();
        let reply_with_message = Reply::try_from(concat).unwrap();
        assert_eq!(reply_with_message, Reply::Info("Test".to_string()));

        let max_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH];
        let max = [&info_prefix[..], &max_message[..]].concat();
        let reply_with_max = Reply::try_from(max);
        assert!(
            !reply_with_max.is_err(),
            format!(
                "Max message length of {} bytes should not be an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
        assert_eq!(reply_with_max.unwrap(), Reply::Info(String::from_utf8(max_message).unwrap()));

        let overflow_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH + 1];
        let overflow = [&info_prefix[..], &overflow_message[..]].concat();
        let reply_with_overflow = Reply::try_from(overflow);
        assert!(
            reply_with_overflow.is_err(),
            format!(
                "Messages over {} bytes should throw an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
    }

    #[test]
    fn test_fail() {
        let fail_prefix = vec![b'F', b'A', b'I', b'L'];
        let reply = Reply::try_from(fail_prefix.clone()).unwrap();
        assert_eq!(reply, Reply::Fail("".to_string()));

        let fail_prefix_mix_case = vec![b'f', b'A', b'i', b'L'];
        let reply_mix_case = Reply::try_from(fail_prefix_mix_case.clone()).unwrap();
        assert_eq!(reply_mix_case, Reply::Fail("".to_string()));

        let fail_message = vec![b'T', b'e', b's', b't'];
        let concat = [&fail_prefix[..], &fail_message[..]].concat();
        let reply_with_message = Reply::try_from(concat).unwrap();
        assert_eq!(reply_with_message, Reply::Fail("Test".to_string()));

        let max_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH];
        let max = [&fail_prefix[..], &max_message[..]].concat();
        let reply_with_max = Reply::try_from(max);
        assert!(
            !reply_with_max.is_err(),
            format!(
                "Max message length of {} bytes should not be an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
        assert_eq!(reply_with_max.unwrap(), Reply::Fail(String::from_utf8(max_message).unwrap()));

        let overflow_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH + 1];
        let overflow = [&fail_prefix[..], &overflow_message[..]].concat();
        let reply_with_overflow = Reply::try_from(overflow);
        assert!(
            reply_with_overflow.is_err(),
            format!(
                "Messages over {} bytes should throw an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
    }

    #[test]
    fn test_okay() {
        let okay_prefix = vec![b'O', b'K', b'A', b'Y'];
        let reply = Reply::try_from(okay_prefix.clone()).unwrap();
        assert_eq!(reply, Reply::Okay("".to_string()));

        let okay_prefix_mix_case = vec![b'O', b'k', b'A', b'y'];
        let reply_mix_case = Reply::try_from(okay_prefix_mix_case.clone()).unwrap();
        assert_eq!(reply_mix_case, Reply::Okay("".to_string()));

        let okay_message = vec![b'T', b'e', b's', b't'];
        let concat = [&okay_prefix[..], &okay_message[..]].concat();
        let reply_with_message = Reply::try_from(concat).unwrap();
        assert_eq!(reply_with_message, Reply::Okay("Test".to_string()));

        let max_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH];
        let max = [&okay_prefix[..], &max_message[..]].concat();
        let reply_with_max = Reply::try_from(max);
        assert!(
            !reply_with_max.is_err(),
            format!(
                "Max message length of {} bytes should not be an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
        assert_eq!(reply_with_max.unwrap(), Reply::Okay(String::from_utf8(max_message).unwrap()));

        let overflow_message = vec![b'X'; MAX_REPLY_LENGTH - MIN_REPLY_LENGTH + 1];
        let overflow = [&okay_prefix[..], &overflow_message[..]].concat();
        let reply_with_overflow = Reply::try_from(overflow);
        assert!(
            reply_with_overflow.is_err(),
            format!(
                "Messages over {} bytes should throw an error",
                MAX_REPLY_LENGTH - MIN_REPLY_LENGTH
            )
        );
    }

    #[test]
    fn test_data() {
        let data_prefix = vec![b'D', b'A', b'T', b'A'];
        let reply_with_no_size = Reply::try_from(data_prefix.clone());
        assert!(
            reply_with_no_size.is_err(),
            format!("DATA replies must have an {} byte size associated", DATA_SIZE_LENGTH)
        );

        let zero = vec![b'0'; DATA_SIZE_LENGTH];
        let concat_zero = [&data_prefix[..], &zero[..]].concat();
        let reply = Reply::try_from(concat_zero.clone()).unwrap();
        assert_eq!(reply, Reply::Data(0));

        let data_prefix_mix_case = vec![b'd', b'A', b'T', b'A'];
        let concat_zero_mix_case = [&data_prefix_mix_case[..], &zero[..]].concat();
        let reply_mix_case = Reply::try_from(concat_zero_mix_case.clone()).unwrap();
        assert_eq!(reply_mix_case, Reply::Data(0));

        let data_max = vec![b'f'; DATA_SIZE_LENGTH];
        let concat = [&data_prefix[..], &data_max[..]].concat();
        let reply_with_message = Reply::try_from(concat).unwrap();
        assert_eq!(reply_with_message, Reply::Data(0xffffffff));

        let data_message_uppercase = vec![b'F'; DATA_SIZE_LENGTH];
        let concat_uppercase = [&data_prefix[..], &data_message_uppercase[..]].concat();
        let reply_with_message_uppercase = Reply::try_from(concat_uppercase).unwrap();
        assert_eq!(reply_with_message_uppercase, Reply::Data(0xffffffff));
    }

    #[test]
    fn test_unknown() {
        let unknown_prefix = vec![b'T', b'e', b's', b't'];
        let unknown_reply = Reply::try_from(unknown_prefix.clone());
        assert!(unknown_reply.is_err(), "Unknown replies throw an error");
    }
}
