// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros for parsing and serialization of ICMP packets.

/// Implement `IcmpMessage` for a type.
///
/// The arguments are:
/// - `$ip` - `Ipv4` or `Ipv6`
/// - `$type` - the type to implement for
/// - `$msg_variant` - the variant of `Icmpv4MessageType` or `icmpv6::MessageType`
///   associated with this message type
/// - `$code` - the type to use for `IcmpMessage::Code`; if `()` is used, 0 will
///   be the only valid code
/// - `$has_body` - `true` or `false` depending on whether this message type
///   supports a body
macro_rules! impl_icmp_message {
    ($ip:ident, $type:ident, $msg_variant:ident, $code:tt, $body_type:ty) => {
        impl<B: ByteSlice> crate::icmp::IcmpMessage<$ip, B> for $type {
            type Code = $code;

            type Body = $body_type;

            const TYPE: <$ip as IcmpIpExt>::IcmpMessageType =
                impl_icmp_message_inner_message_type!($ip, $msg_variant);

            fn code_from_u8(u: u8) -> Option<Self::Code> {
                impl_icmp_message_inner_code_from_u8!($code, u)
            }
        }
    };

    ($ip:ident, $type:ident, $msg_variant:ident, $code:tt) => {
        impl_icmp_message!($ip, $type, $msg_variant, $code, ());
    };
}

macro_rules! impl_icmp_message_inner_message_type {
    (Ipv4, $msg_variant:ident) => {
        crate::icmp::icmpv4::Icmpv4MessageType::$msg_variant
    };
    (Ipv6, $msg_variant:ident) => {
        crate::icmp::icmpv6::Icmpv6MessageType::$msg_variant
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
        $code::try_from($var).ok()
    };
}
