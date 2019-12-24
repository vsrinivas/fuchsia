// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test_helpers::TextFieldWrapper;
use anyhow::{bail, format_err, Error};
use fidl_fuchsia_ui_text as txt;

// TextField::OnUpdate() tests
// #######################################################

/// Inserts an empty commit to check that it increments the revision number.
pub async fn test_noop_causes_state_update(text_field: &mut TextFieldWrapper) -> Result<(), Error> {
    let rev = text_field.state().revision;

    text_field.proxy().begin_edit(rev)?;
    if text_field.proxy().commit_edit().await? != txt::Error::Ok {
        return Err(format_err!("Expected commit_edit to succeed"));
    }
    text_field.wait_for_update().await?;

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

/// Inserts "meow1 meow2 meow3" and validates some simple range and distance requests.
pub async fn test_simple_content_request(text_field: &mut TextFieldWrapper) -> Result<(), Error> {
    text_field.simple_insert("meow1 meow2 meow3").await?;

    // check length and contents of last two meows
    let mut doc_end = text_field.state().document.end;
    let start_point = text_field.point_offset(&mut doc_end, -11).await?;
    let range = txt::Range { start: start_point, end: doc_end };
    text_field.validate_distance(&range, 11).await?;
    Ok(())
}

/// Inserts "meow1 🐱 meow3" and validates some simple range and distance requests, this time across
/// the emoji boundary, to ensure distances are correctly calculated around multibyte unicode chars.
pub async fn test_multibyte_unicode_content_request(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    text_field.simple_insert("meow1 🐱 meow3").await?;

    // check length and contents of last two meows
    let mut doc_end = text_field.state().document.end;
    let start_point = text_field.point_offset(&mut doc_end, -7).await?;
    let range = txt::Range { start: start_point, end: doc_end };
    text_field.validate_distance(&range, 7).await?;
    text_field.validate_contents(&range, "🐱 meow3").await?;
    Ok(())
}

// TextField::CommitEdit() tests
// #######################################################

/// Double checks that within a transaction, if a `Point` A appears after an initial replacement,
/// its position in the string is properly adjusted to account for that initial replacement before
/// any subsequent edits in that transaction are applied.
pub async fn test_multiple_edit_moves_points(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    text_field.simple_insert("meow1 meow2 meow3 meow4").await?;

    text_field.proxy().begin_edit(text_field.state().revision)?;
    // replace "meow1" => "5" and "meow3" => "6" in a single transaction
    {
        let mut doc_start = text_field.state().document.start;
        let end_point = text_field.point_offset(&mut doc_start, 5).await?;
        let mut range = txt::Range { start: doc_start, end: end_point };
        text_field.proxy().replace(&mut range, "5")?;
    }
    {
        let mut doc_start = text_field.state().document.start;
        let start_point = text_field.point_offset(&mut doc_start, 12).await?;
        let end_point = text_field.point_offset(&mut doc_start, 17).await?;
        let mut range = txt::Range { start: start_point, end: end_point };
        text_field.proxy().replace(&mut range, "6")?;
    }
    text_field.proxy().commit_edit().await?;
    text_field.wait_for_update().await?;
    let doc = &mut text_field.state().document;
    text_field.validate_contents(doc, "5 meow2 6 meow4").await?;
    text_field.validate_distance(doc, 15).await?;
    Ok(())
}

/// Attempts to create a point one character after the end of the document, and then delete that one
/// character. Since that character doesn't exist, the document should remain unchanged. The correct
/// behavior is the new point should clamp back to the end of the document, resulting in a
/// zero-width delete.
pub async fn test_invalid_delete_off_end_of_field(
    text_field: &mut TextFieldWrapper,
) -> Result<(), Error> {
    text_field.simple_insert("meow1 meow2 meow3").await?;

    text_field.proxy().begin_edit(text_field.state().revision)?;
    // delete from the end of the doc to one character *past* the end of the doc
    {
        let mut doc_end = text_field.state().document.end;
        let end_point = text_field.point_offset(&mut doc_end, 1).await?;
        let mut range = txt::Range { start: doc_end, end: end_point };
        text_field.proxy().replace(&mut range, "")?;
    }
    text_field.proxy().commit_edit().await?;
    text_field.wait_for_update().await?;
    let doc = &mut text_field.state().document;
    // contents should be unchanged
    text_field.validate_contents(doc, "meow1 meow2 meow3").await?;
    text_field.validate_distance(doc, 17).await?;
    Ok(())
}
