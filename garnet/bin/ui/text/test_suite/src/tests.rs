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
    let range = txt::TextRange { start: start_point, end: doc_end };
    await!(text_field.validate_distance(&range, 11))?;
    Ok(())
}

pub async fn test_multibyte_unicode_content_request(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    await!(text_field.simple_insert("meow1 🐱 meow3"))?;

    // check length and contents of last two meows
    let mut doc_end = text_field.state().document.end;
    let start_point = await!(text_field.point_offset(&mut doc_end, -7))?;
    let range = txt::TextRange { start: start_point, end: doc_end };
    await!(text_field.validate_distance(&range, 7))?;
    await!(text_field.validate_contents(&range, "🐱 meow3"))?;
    Ok(())
}

// TextField::CommitEdit() tests
// #######################################################

pub async fn test_multiple_edit_moves_points(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    await!(text_field.simple_insert("meow1 meow2 meow3 meow4"))?;

    text_field.proxy().begin_edit(text_field.state().revision)?;
    // replace "meow1" => "5" and "meow3" => "6" in a single transaction
    {
        let mut doc_start = text_field.state().document.start;
        let end_point = await!(text_field.point_offset(&mut doc_start, 5))?;
        let mut range = txt::TextRange { start: doc_start, end: end_point };
        text_field.proxy().replace(&mut range, "5")?;
    }
    {
        let mut doc_start = text_field.state().document.start;
        let start_point = await!(text_field.point_offset(&mut doc_start, 12))?;
        let end_point = await!(text_field.point_offset(&mut doc_start, 17))?;
        let mut range = txt::TextRange { start: start_point, end: end_point };
        text_field.proxy().replace(&mut range, "6")?;
    }
    await!(text_field.proxy().commit_edit())?;
    await!(text_field.wait_for_update())?;
    let doc = &mut text_field.state().document;
    await!(text_field.validate_contents(doc, "5 meow2 6 meow4"))?;
    await!(text_field.validate_distance(doc, 15))?;
    Ok(())
}

pub async fn test_invalid_delete_off_end_of_field(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    await!(text_field.simple_insert("meow1 meow2 meow3"))?;

    text_field.proxy().begin_edit(text_field.state().revision)?;
    // delete from the end of the doc to one character *past* the end of the doc
    {
        let mut doc_end = text_field.state().document.end;
        let end_point = await!(text_field.point_offset(&mut doc_end, 1))?;
        let mut range = txt::TextRange { start: doc_end, end: end_point };
        text_field.proxy().replace(&mut range, "")?;
    }
    await!(text_field.proxy().commit_edit())?;
    await!(text_field.wait_for_update())?;
    let doc = &mut text_field.state().document;
    // contents should be unchanged
    await!(text_field.validate_contents(doc, "meow1 meow2 meow3"))?;
    await!(text_field.validate_distance(doc, 17))?;
    Ok(())
}
