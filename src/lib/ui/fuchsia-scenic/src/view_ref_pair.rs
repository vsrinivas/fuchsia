// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_ui_views::{ViewRef, ViewRefControl},
    fuchsia_zircon::{AsHandleRef, EventPair, HandleBased, Rights},
};

pub struct ViewRefPair {
    pub control_ref: ViewRefControl,
    pub view_ref: ViewRef,
}

impl ViewRefPair {
    pub fn new() -> Result<ViewRefPair, Error> {
        let (raw_control_ref, raw_view_ref) = EventPair::create()?;

        // Remove duplication from the control ref. This is the same
        // as `ZX_DEFAULT_EVENTPAIR_RIGHTS & (~ZX_RIGHT_DUPLICATE)`
        let new_rights = (Rights::BASIC | Rights::SIGNAL | Rights::SIGNAL_PEER) - Rights::DUPLICATE;
        let control_ref = raw_control_ref.into_handle().replace(new_rights)?;

        // Remove signaling from the view_ref
        let view_ref = raw_view_ref.into_handle().replace(Rights::BASIC)?;

        Ok(ViewRefPair {
            control_ref: ViewRefControl { reference: control_ref.into() },
            view_ref: ViewRef { reference: view_ref.into() },
        })
    }
}

impl From<ViewRefPair> for fidl_fuchsia_ui_views::ViewIdentityOnCreation {
    fn from(item: ViewRefPair) -> Self {
        fidl_fuchsia_ui_views::ViewIdentityOnCreation {
            view_ref: item.view_ref,
            view_ref_control: item.control_ref,
        }
    }
}

/// Given a ViewRef, returns a new version which has been duplicated.
pub fn duplicate_view_ref(view_ref: &ViewRef) -> Result<ViewRef, Error> {
    let handle = view_ref.reference.as_handle_ref().duplicate(Rights::SAME_RIGHTS)?;
    Ok(ViewRef { reference: handle.into() })
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! assert_not_contains_rights {
        ($handle:expr, $test_rights:expr) => {
            let basic_info = $handle.reference.as_handle_ref().basic_info().unwrap();
            assert_eq!(basic_info.rights & $test_rights, Rights::NONE);
        };
    }

    macro_rules! assert_contains_rights {
        ($handle:expr, $test_rights:expr) => {
            let basic_info = $handle.reference.as_handle_ref().basic_info().unwrap();
            assert_eq!(basic_info.rights & $test_rights, $test_rights);
        };
    }

    #[test]
    fn removes_duplication_from_control_ref() {
        let ViewRefPair { control_ref, view_ref: _ } = ViewRefPair::new().unwrap();
        assert_not_contains_rights!(control_ref, Rights::DUPLICATE);
    }

    #[test]
    fn control_ref_can_signal() {
        let ViewRefPair { control_ref, view_ref: _ } = ViewRefPair::new().unwrap();
        assert_contains_rights!(control_ref, Rights::SIGNAL);
    }

    #[test]
    fn control_ref_can_signal_peer() {
        let ViewRefPair { control_ref, view_ref: _ } = ViewRefPair::new().unwrap();
        assert_contains_rights!(control_ref, Rights::SIGNAL_PEER);
    }

    #[test]
    fn view_ref_can_duplicate() {
        let ViewRefPair { control_ref: _, view_ref } = ViewRefPair::new().unwrap();
        assert_contains_rights!(view_ref, Rights::DUPLICATE);
    }

    #[test]
    fn view_ref_duplicate() {
        let ViewRefPair { control_ref: _, view_ref } = ViewRefPair::new().unwrap();
        assert!(duplicate_view_ref(&view_ref).is_ok(), "failed to duplicate view_ref");
    }
}
