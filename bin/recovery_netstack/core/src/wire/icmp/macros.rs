// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros for parsing and serialization of ICMP packets.

/// Create an enum for a network packet field.
///
/// Create a `repr(u8)` enum with a `from_u8(u8) -> Option<Self>` constructor
/// that implements `Into<u8>`.
macro_rules! create_net_enum {
    ($t:ident, $($val:ident: $const:ident = $value:expr,)*) => {
      create_net_enum!($t, $($val: $const = $value),*);
    };

    ($t:ident, $($val:ident: $const:ident = $value:expr),*) => {
      #[allow(missing_docs)]
      #[derive(Debug, PartialEq, Copy, Clone)]
      #[repr(u8)]
      pub enum $t {
        $($val = $t::$const),*
      }

      impl $t {
        $(const $const: u8 = $value;)*

        fn from_u8(u: u8) -> Option<$t> {
          match u {
            $($t::$const => Some($t::$val)),*,
            _ => None,
          }
        }
      }

      impl Into<u8> for $t {
          fn into(self) -> u8 {
              self as u8
          }
      }

      impl ::std::convert::TryFrom<u8> for $t {
        type Error = ();

        fn try_from(value: u8) -> Result<$t, ()> {
          $t::from_u8(value).ok_or(())
        }
      }
    };
}

/// Implement `IcmpMessage` for a type.
///
/// The arguments are:
/// - `$ip` - `Ipv4` or `Ipv6`
/// - `$typ` - the type to implement for
/// - `$msg_variant` - the variant of `Icmpv4MessageType` or `icmpv6::MessageType`
///   associated with this message type
/// - `$code` - the type to use for `IcmpMessage::Code`; if `()` is used, 0 will
///   be the only valid code
/// - `$has_body` - `true` or `false` depending on whether this message type
///   supports a body
macro_rules! impl_icmp_message {
    ($ip:ident, $type:ident, $msg_variant:ident, $code:tt, $has_body:expr) => {
        impl crate::wire::icmp::IcmpMessage<$ip> for $type {
            type Code = $code;

            const TYPE: <$ip as IcmpIpExt>::IcmpMessageType =
                impl_icmp_message_inner_message_type!($ip, $msg_variant);

            const HAS_BODY: bool = $has_body;

            fn code_from_u8(u: u8) -> Option<Self::Code> {
                impl_icmp_message_inner_code_from_u8!($code, u)
            }
        }
    };
}

macro_rules! impl_icmp_message_inner_message_type {
    (Ipv4, $msg_variant:ident) => {
        crate::wire::icmp::icmpv4::MessageType::$msg_variant
    };
    (Ipv6, $msg_variant:ident) => {
        crate::wire::icmp::icmpv6::MessageType::$msg_variant
    };
}

macro_rules! impl_icmp_message_inner_code_from_u8 {
    (IcmpUnusedCode, $var:ident) => {
        if $var == 0 {
            Some(IcmpUnusedCode)
        } else {
            None
        }
    };
    ($code:tt, $var:ident) => {
        $code::from_u8($var)
    };
}

macro_rules! impl_from_bytes_as_bytes_unaligned {
    ($type:ty) => {
        unsafe impl zerocopy::FromBytes for $type {}
        unsafe impl zerocopy::AsBytes for $type {}
        unsafe impl zerocopy::Unaligned for $type {}
    };
}
