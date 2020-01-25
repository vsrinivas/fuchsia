// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::mac, zerocopy::ByteSlice};

/// IEEE Std 802.11-2016, 11.3.3
#[derive(Copy, Clone, PartialOrd, PartialEq, Debug, Ord, Eq)]
pub enum FrameClass {
    Class1 = 1,
    Class2 = 2,
    Class3 = 3,
}

/// Converts a MacFrame into a FrameClass.
impl<B: ByteSlice> From<&mac::MacFrame<B>> for FrameClass {
    fn from(mac_frame: &mac::MacFrame<B>) -> FrameClass {
        match mac_frame {
            mac::MacFrame::Data { fixed_fields, .. } => frame_class(&{ fixed_fields.frame_ctrl }),
            mac::MacFrame::Mgmt { mgmt_hdr, .. } => frame_class(&{ mgmt_hdr.frame_ctrl }),
            mac::MacFrame::Ctrl { ctrl_hdr, .. } => frame_class(&{ ctrl_hdr.frame_ctrl }),
            mac::MacFrame::Unsupported { frame_ctrl } => frame_class(&frame_ctrl),
        }
    }
}

/// IEEE Std 802.11-2016, 11.3.3
/// Unlike IEEE which only considers Public and Self-Protected Action frames Class 1 frames,
/// Fuchsia considers all Action frames Class 1 frames when checking a frame's FrameControl.
/// Use `action_frame_class(category)` to determine an Action frame's proper frame Class.
/// Fuchsia supports neither IBSS nor PBSS, thus, every frame is evaluated in respect to an
/// Infrastructure BSS.
pub fn frame_class(fc: &mac::FrameControl) -> FrameClass {
    // Class 1 frames:
    match fc.frame_type() {
        mac::FrameType::CTRL => match fc.ctrl_subtype() {
            // Control extensions such as SSW and Grant are excluded as Fuchsia does not
            // support any of those frames yet.
            mac::CtrlSubtype::RTS
            | mac::CtrlSubtype::CTS
            | mac::CtrlSubtype::ACK
            | mac::CtrlSubtype::CF_END
            | mac::CtrlSubtype::CF_END_ACK => return FrameClass::Class1,
            _ => (),
        },
        mac::FrameType::MGMT => match fc.mgmt_subtype() {
            mac::MgmtSubtype::ACTION
            | mac::MgmtSubtype::PROBE_REQ
            | mac::MgmtSubtype::PROBE_RESP
            | mac::MgmtSubtype::BEACON
            | mac::MgmtSubtype::AUTH
            | mac::MgmtSubtype::DEAUTH
            | mac::MgmtSubtype::ATIM => return FrameClass::Class1,
            _ => (),
        },
        _ => (),
    };

    // Class 2 frames:
    // Data frames are excluded as DLS is not supported by Fuchsia
    if let mac::FrameType::MGMT = fc.frame_type() {
        match fc.mgmt_subtype() {
            mac::MgmtSubtype::ASSOC_REQ
            | mac::MgmtSubtype::ASSOC_RESP
            | mac::MgmtSubtype::REASSOC_REQ
            | mac::MgmtSubtype::REASSOC_RESP
            | mac::MgmtSubtype::DISASSOC => return FrameClass::Class2,
            _ => (),
        }
    };

    FrameClass::Class3
}

// IEEE Std 802.11-2016, 11.3.3
pub fn action_frame_class(category: mac::ActionCategory) -> FrameClass {
    match category {
        // Class 1 frames:
        mac::ActionCategory::PUBLIC | mac::ActionCategory::SELF_PROTECTED => FrameClass::Class1,
        // All other action frames are Class 3 frames:
        _ => FrameClass::Class3,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mgmt_frame_class() {
        // Class 1 frames:
        let mut fc = mac::FrameControl(0)
            .with_frame_type(mac::FrameType::MGMT)
            .with_mgmt_subtype(mac::MgmtSubtype::BEACON);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::AUTH);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::DEAUTH);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::PROBE_RESP);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::ATIM);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::ACTION);
        assert_eq!(FrameClass::Class1, frame_class(&fc));

        // Class 2 frames:
        fc.set_mgmt_subtype(mac::MgmtSubtype::ASSOC_REQ);
        assert_eq!(FrameClass::Class2, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::ASSOC_RESP);
        assert_eq!(FrameClass::Class2, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::REASSOC_REQ);
        assert_eq!(FrameClass::Class2, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::REASSOC_RESP);
        assert_eq!(FrameClass::Class2, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::DISASSOC);
        assert_eq!(FrameClass::Class2, frame_class(&fc));

        // Class 3 frames:
        fc.set_mgmt_subtype(mac::MgmtSubtype::TIMING_AD);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
        fc.set_mgmt_subtype(mac::MgmtSubtype::ACTION_NO_ACK);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
    }

    #[test]
    fn data_frame_class() {
        // Data frames are always Class 3 frames:
        let fc = mac::FrameControl(0).with_frame_type(mac::FrameType::DATA);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
    }

    #[test]
    fn ctrl_frame_class() {
        // Class 1 frames:
        let mut fc = mac::FrameControl(0)
            .with_frame_type(mac::FrameType::CTRL)
            .with_ctrl_subtype(mac::CtrlSubtype::ACK);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_ctrl_subtype(mac::CtrlSubtype::RTS);
        assert_eq!(FrameClass::Class1, frame_class(&fc));
        fc.set_ctrl_subtype(mac::CtrlSubtype::CTS);
        assert_eq!(FrameClass::Class1, frame_class(&fc));

        // Class 3 frames:
        fc.set_ctrl_subtype(mac::CtrlSubtype::PS_POLL);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
        fc.set_ctrl_subtype(mac::CtrlSubtype::BLOCK_ACK);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
        fc.set_ctrl_subtype(mac::CtrlSubtype::BLOCK_ACK_REQ);
        assert_eq!(FrameClass::Class3, frame_class(&fc));
    }

    #[test]
    fn action_frames() {
        // Class 1 frames:
        assert_eq!(FrameClass::Class1, action_frame_class(mac::ActionCategory::PUBLIC));
        assert_eq!(FrameClass::Class1, action_frame_class(mac::ActionCategory::SELF_PROTECTED));

        // Class 3 frames:
        assert_eq!(FrameClass::Class3, action_frame_class(mac::ActionCategory::BLOCK_ACK));
        assert_eq!(FrameClass::Class3, action_frame_class(mac::ActionCategory::MESH));
    }
}
