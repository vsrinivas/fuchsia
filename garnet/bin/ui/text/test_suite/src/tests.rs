// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_helpers::TextFieldWrapper;
use failure::{bail, Error};
use fidl_fuchsia_ui_text as txt;

// TextField::OnUpdate() tests
// #######################################################

pub async fn test_noop_causes_state_update(text_field: &mut TextFieldWrapper) -> Result<(), Error> {
    let rev = text_field.state().revision;

    text_field.proxy().begin_edit(rev)?;
    if await!(text_field.proxy().commit_edit())? != txt::TextError::Ok {
        bail!("Expected commit_edit to succeed");
    }
    await!(text_field.wait_for_update())?;

    // check that we get another state update, and revision was increased
    if text_field.state().revision <= rev {
        bail!(format!(
            "Expected new revision ({}) to be greater than old revision ({})",
            text_field.state().revision,
            rev
        ));
    }
    Ok(())
}

// TextField::PointOffset()/TextField::Distance()/TextField::Contents() tests
// #######################################################

pub async fn test_simple_content_request(text_field: &mut TextFieldWrapper) -> Result<(), Error> {
    await!(text_field.simple_insert("meow1 meow2 meow3"))?;

    // check length and contents of last two meows
    let mut doc_end = text_field.state().document.end;
    let start_point = await!(text_field.point_offset(&mut doc_end, -11))?;
    let mut range = txt::TextRange { start: start_point, end: doc_end };
    await!(text_field.validate_distance(&range, 11))?;
    await!(text_field.validate_contents(&range, "meow2 meow3"))?;
    let (meows, _true_start) = await!(text_field.contents(&mut range))?;
    if meows != "meow2 meow3" {
        bail!(format!(
            "Expected contents request to return \"meow2 meow3\", instead got {:?}",
            meows
        ))
    }
    Ok(())
}
