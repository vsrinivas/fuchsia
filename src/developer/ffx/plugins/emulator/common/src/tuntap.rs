// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Result};
use cfg_if::cfg_if;
use mockall::automock;
use nix::{
    ifaddrs::{getifaddrs, InterfaceAddress, InterfaceAddressIterator},
    net::if_::InterfaceFlags,
};

// TODO(fxbug.dev/100022): Make this configurable.
// The interface name, "qemu", provided here used to be provided by the qemu
// executable when it was left for qemu to start the upscript. Since we need
// to time the sudo prompt and the socket creation, we now launch the script
// ourselves, so we also have to provide the interface name. This will likely
// need to eventually be configurable to support running emulators on
// multiple tap interfaces.
pub const TAP_INTERFACE_NAME: &'static str = "qemu";

pub(crate) mod tap {
    use super::*;

    #[derive(Clone, Default)]
    pub(crate) struct QemuTunTap {}

    #[automock]
    #[allow(dead_code)]
    impl QemuTunTap {
        // These functions are not "dead", but because of the automocking and cfg_if use below,
        // the compiler doesn't realize the non-mocked versions are being used by available.
        pub(crate) fn interface_is_up(&self, interface: &InterfaceAddress) -> bool {
            interface.flags.contains(InterfaceFlags::IFF_UP)
        }

        // On MacOS, this library doesn't contain the IFF_TAP flag at all, so don't even check.
        #[cfg(target_os = "macos")]
        pub(crate) fn interface_is_tap(&self, interface: &InterfaceAddress) -> bool {
            #[cfg(test)]
            return interface.flags.contains(InterfaceFlags::IFF_DEBUG);

            #[cfg(not(test))]
            {
                tracing::debug!(
                    "Skipping Tun/Tap test for '{}' since it's not supported on Mac.",
                    interface.interface_name,
                );
                return false;
            }
        }

        // On non-Mac, we check this flag to determine the type of the interface.
        #[cfg(not(target_os = "macos"))]
        pub(crate) fn interface_is_tap(&self, interface: &InterfaceAddress) -> bool {
            interface.flags.contains(InterfaceFlags::IFF_TAP)
        }

        pub(crate) fn interface_is_in_use(&self, interface: &InterfaceAddress) -> bool {
            interface.flags.contains(InterfaceFlags::IFF_RUNNING)
        }

        pub(crate) fn get_interface_details(&self) -> Result<InterfaceAddressIterator> {
            // getifaddrs() uses non-standard error types, which cannot be converted to
            // anyhow::Result in the usual ways, so we wrap it in a match to convert the error
            // instead.
            match getifaddrs() {
                Ok(iff) => Ok(iff),
                Err(e) => bail!("Failed to get interface addresses: {}", e),
            }
        }

        pub(crate) fn host_is_mac(&self) -> bool {
            crate::host_is_mac()
        }

        pub(crate) fn parse_interface_from_details(
            &self,
            mut iter: InterfaceAddressIterator,
        ) -> Result<InterfaceAddress> {
            if let Some(interface) =
                iter.find(|i| i.interface_name == String::from(TAP_INTERFACE_NAME))
            {
                // InterfaceAddressIterator doesn't persist once it's been iterated over, so this
                // clone() is needed to take ownership of the data.
                Ok(interface.clone())
            } else {
                bail!(format!(
                    "Couldn't find an interface named '{}'. \
                    Configure Tun/Tap on your host or try --net user.\n\
                    To use emu with Tun/Tap networking on Linux, run:\n    \
                        sudo ip tuntap add dev {} mode tap user $USER && sudo ip link set {} up",
                    TAP_INTERFACE_NAME, TAP_INTERFACE_NAME, TAP_INTERFACE_NAME
                ))
            }
        }
    }
}

cfg_if! {
    if #[cfg(test)] {
        use self::tap::MockQemuTunTap as QemuTunTap;
    } else {
        use self::tap::QemuTunTap;
    }
}

/// A utility function for testing if a Tap interface is configured. Assumes the existence
/// of the "ip" program for finding the interface, which is usually preinstalled on Linux hosts
/// but not MacOS hosts. Conservatively assumes any error indicates Tap is unavailable.
pub fn tap_available() -> Result<()> {
    let tap = QemuTunTap::default();
    tap_inner(&tap, false)
}

/// A utility function for testing is the Tap interface is available and ready for use. Same
/// assumptions as tap_available(), but also ensures that the interface is UP and not in use by
/// another process.
pub fn tap_ready() -> Result<()> {
    let tap = QemuTunTap::default();
    tap_inner(&tap, true)
}

fn tap_inner(tap: &QemuTunTap, check_for_ready: bool) -> Result<()> {
    // Mac's don't include the "ip" program by default.
    if tap.host_is_mac() {
        bail!("Tun/Tap isn't supported on MacOS.")
    }

    // Make sure we have an interface named TAP_INTERFACE_NAME.
    let interface = tap.parse_interface_from_details(tap.get_interface_details()?)?;

    // It's there, now make sure it's tap.
    if !tap.interface_is_tap(&interface) {
        bail!(format!(
            "The '{}' interface exists, but it's not a Tun/Tap interface.",
            TAP_INTERFACE_NAME,
        ))
    }

    // We don't check for ready when resolving --net auto, only during validation.
    if check_for_ready {
        if !tap.interface_is_up(&interface) {
            bail!(format!(
                "The Tun/Tap interface '{}' exists, but it's currently DOWN.\n\
                To bring it up, you can run:\n    \
                    sudo ip link set {} up",
                TAP_INTERFACE_NAME, TAP_INTERFACE_NAME
            ))
        }
    }

    // Also check for busy-ness.
    if tap.interface_is_in_use(&interface) {
        bail!(format!(
            "The Tun/Tap interface '{}' exists, but it's in use by another process.",
            TAP_INTERFACE_NAME,
        ))
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // When we're running on MacOS, there's no such thing as IFF_TAP. This function allows
    // us to write the following tests without worrying about that. Then we disable the
    // test_interface_is_tap() test, since that function will always return false.
    fn tap_flag() -> InterfaceFlags {
        #[cfg(target_os = "macos")]
        return InterfaceFlags::IFF_DEBUG;

        #[cfg(not(target_os = "macos"))]
        return InterfaceFlags::IFF_TAP;
    }

    // Test harness to provide a fake iterator
    #[allow(dead_code)]
    struct TestInterfaceAddressIterator {
        pub base: *mut libc::ifaddrs,
        pub next: *mut libc::ifaddrs,
    }
    impl TestInterfaceAddressIterator {
        fn new() -> Self {
            Self {
                base: std::ptr::null_mut::<libc::ifaddrs>(),
                next: std::ptr::null_mut::<libc::ifaddrs>(),
            }
        }
    }

    fn loopback() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("qemu"),
            flags: InterfaceFlags::IFF_UP
                | InterfaceFlags::IFF_LOOPBACK
                | InterfaceFlags::IFF_RUNNING,
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    fn tap_up() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("qemu"),
            flags: InterfaceFlags::IFF_BROADCAST
                | InterfaceFlags::IFF_MULTICAST
                | InterfaceFlags::IFF_UP
                | tap_flag(),
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    fn wrong_name() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("br0"),
            flags: InterfaceFlags::IFF_BROADCAST
                | InterfaceFlags::IFF_MULTICAST
                | InterfaceFlags::IFF_UP,
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    fn bridge() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("brqemu"),
            flags: InterfaceFlags::IFF_UP
                | InterfaceFlags::IFF_BROADCAST
                | InterfaceFlags::IFF_RUNNING
                | InterfaceFlags::IFF_MULTICAST
                | tap_flag(),
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    fn tap_down() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("qemu"),
            flags: tap_flag(),
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    fn connected() -> InterfaceAddress {
        InterfaceAddress {
            interface_name: String::from("qemu"),
            flags: InterfaceFlags::IFF_BROADCAST
                | InterfaceFlags::IFF_MULTICAST
                | tap_flag()
                | InterfaceFlags::IFF_UP
                | InterfaceFlags::IFF_RUNNING,
            address: None,
            netmask: None,
            broadcast: None,
            destination: None,
        }
    }

    #[test]
    fn test_interface_is_up() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(tap.interface_is_up(&loopback()), "{:#?}", loopback().flags);
        assert!(tap.interface_is_up(&tap_up()), "{:#?}", tap_up().flags);
        assert!(tap.interface_is_up(&bridge()), "{:#?}", bridge().flags);
        assert!(!tap.interface_is_up(&tap_down()), "{:#?}", tap_down().flags);
        assert!(tap.interface_is_up(&wrong_name()), "{:#?}", wrong_name().flags);
        assert!(tap.interface_is_up(&connected()), "{:#?}", connected().flags);
        Ok(())
    }

    #[cfg(not(target_os = "macos"))]
    #[test]
    fn test_interface_is_tap() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(!tap.interface_is_tap(&loopback()), "{:#?}", loopback().flags);
        assert!(tap.interface_is_tap(&tap_up()), "{:#?}", tap_up().flags);
        assert!(tap.interface_is_tap(&bridge()), "{:#?}", bridge().flags);
        assert!(tap.interface_is_tap(&tap_down()), "{:#?}", tap_down().flags);
        assert!(tap.interface_is_tap(&wrong_name()), "{:#?}", wrong_name().flags);
        assert!(tap.interface_is_tap(&connected()), "{:#?}", connected().flags);
        Ok(())
    }

    #[test]
    fn test_interface_is_in_use() -> Result<()> {
        let tap = tap::QemuTunTap::default();
        assert!(tap.interface_is_in_use(&loopback()), "{:#?}", loopback().flags);
        assert!(!tap.interface_is_in_use(&tap_up()), "{:#?}", tap_up().flags);
        assert!(tap.interface_is_in_use(&bridge()), "{:#?}", bridge().flags);
        assert!(!tap.interface_is_in_use(&tap_down()), "{:#?}", tap_down().flags);
        assert!(!tap.interface_is_in_use(&wrong_name()), "{:#?}", wrong_name().flags);
        assert!(tap.interface_is_in_use(&connected()), "{:#?}", connected().flags);
        Ok(())
    }

    #[test]
    fn test_tap_inner() -> Result<()> {
        let mut tap = QemuTunTap::default();
        let real_thing = tap::QemuTunTap::default();
        let clone1 = real_thing.clone();
        let clone2 = real_thing.clone();
        tap.expect_interface_is_up().returning(move |i| real_thing.interface_is_up(&i));
        tap.expect_interface_is_tap().returning(move |i| clone1.interface_is_tap(&i));
        tap.expect_interface_is_in_use().returning(move |i| clone2.interface_is_in_use(&i));
        tap.expect_host_is_mac().returning(|| false).times(12);
        tap.expect_get_interface_details()
            // The "unsafe" here is a transmutation between identical types. This is because the
            // fields in the real InterfaceAddressIterator are private, so we can't construct one
            // directly for testing. Instead, we create the test variant defined above with two
            // null pointers, then convert it into the real type. This is only safe here because we
            // are mocking the call to parse_interface_from_details(), so the Iterator is never
            // actually used.
            .returning(|| Ok(unsafe { std::mem::transmute(TestInterfaceAddressIterator::new()) }));

        // Error condition means no interface.
        tap.expect_parse_interface_from_details().returning(|_| bail!("error")).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Loopback interface isn't tap.
        tap.expect_parse_interface_from_details().returning(|_| Ok(loopback())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Bridge isn't tap.
        tap.expect_parse_interface_from_details().returning(|_| Ok(bridge())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap, but administratively DOWN.
        // Should be an error if check_for_ready is true, Ok otherwise.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tap_down())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap interface, but in use by another process.
        tap.expect_parse_interface_from_details().returning(|_| Ok(connected())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        // Valid tap interface, ready to use.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tap_up())).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());

        // Add a check for Macs. If this was on Linux, it would be Ok, but Mac is unsupported.
        tap.expect_parse_interface_from_details().returning(|_| Ok(tap_up())).times(0);
        tap.expect_host_is_mac().returning(|| true).times(2);
        let result = tap_inner(&tap, false);
        assert!(result.is_err());
        let result = tap_inner(&tap, true);
        assert!(result.is_err());

        Ok(())
    }
}
