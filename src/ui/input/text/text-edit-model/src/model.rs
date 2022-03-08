// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code, unused_imports, unused_variables)]

use {
    crate::{
        chars::{CharOps, GetChars},
        errors::TextFieldError,
    },
    anyhow::Error,
    derivative::Derivative,
    fidl_fuchsia_input_text::{
        self as ftext, CompositionSegment as FidlCompositionSegment,
        CompositionUpdate as FidlCompositionUpdate, RevisionId, TransactionId,
    },
    fidl_fuchsia_input_text_ext::IntoRange,
    maplit::btreemap,
    num_traits::AsPrimitive,
    std::{
        cell::RefCell,
        collections::BTreeMap,
        convert::{TryFrom, TryInto},
        iter::Iterator,
        ops::{Bound, Range, RangeBounds},
    },
    tracing::*,
};

/// A model that stores the underlying data and implements business rules for a text field.
#[derive(Debug, Derivative)]
#[derivative(PartialEq, Eq)]
pub struct TextEditModel {
    // Model properties
    #[derivative(PartialEq = "ignore")]
    options: ftext::TextFieldOptions,

    // Model state
    active_revision_id: RevisionId,
    revisions: BTreeMap<RevisionId, Revision>,
    active_transaction: Option<Transaction>,
    composition_state: CompositionState,

    /// This is used solely as a counter to ensure unique transaction IDs. It should not be
    /// decremented if a transaction is cancelled or rolled back.
    last_transaction_id: u64,

    text_field_state_cache: Option<ftext::TextFieldState>,
}

impl TextEditModel {
    pub fn new(mut options: ftext::TextFieldOptions) -> Result<Self, Error> {
        options.input_type.get_or_insert(ftext::InputType::Text);

        let base_revision = Revision::default();
        Ok(Self {
            options,
            active_revision_id: base_revision.id.clone(),
            revisions: btreemap! {
                base_revision.id.clone() => base_revision,
            },
            active_transaction: None,
            composition_state: CompositionState::NotComposing,
            last_transaction_id: 0,
            text_field_state_cache: None,
        })
    }

    fn active_revision(&self) -> &Revision {
        self.revisions.get(&self.active_revision_id).expect("Active revision should exist")
    }

    fn assert_revision_id(&self, revision_id: &RevisionId) -> Result<(), TextFieldError> {
        let active_revision_id = &self.active_revision().id;
        if active_revision_id == revision_id {
            Ok(())
        } else {
            Err(TextFieldError::BadRevisionId {
                expected: Some(active_revision_id.clone()),
                found: Some(revision_id.clone()),
            })
        }
    }

    fn next_revision_id(&self) -> RevisionId {
        self.active_revision_id.next()
    }

    fn prune_revisions(&mut self, range: impl RangeBounds<RevisionId>) {
        assert!(!range.contains(&self.active_revision_id));
        self.revisions.retain(|k, v| !range.contains(k));
    }

    fn assert_transaction_id(&self, transaction_id: &TransactionId) -> Result<(), TextFieldError> {
        let active_transaction_id =
            self.active_transaction.as_ref().map(|transaction| &transaction.id);
        if active_transaction_id == Some(transaction_id) {
            Ok(())
        } else {
            Err(TextFieldError::BadTransactionId {
                expected: active_transaction_id.map(Clone::clone),
                found: Some(transaction_id.clone()),
            })
        }
    }

    // TODO: Cache; return reference.
    fn generate_state(&self) -> ftext::TextFieldState {
        let active_revision = self.active_revision();
        ftext::TextFieldState {
            revision_id: Some(active_revision.id.clone()),
            contents_range: Some(ftext::Range {
                start: 0,
                end: active_revision
                    .length_code_points
                    .try_into()
                    .expect("Content length should really fit into 2^32"),
            }),
            selection: Some(active_revision.selection),
            ..ftext::TextFieldState::EMPTY
        }
    }

    pub fn state(&self) -> ftext::TextFieldState {
        self.generate_state()
    }

    pub fn get_text_range(
        &self,
        char_range: impl RangeBounds<usize>,
    ) -> Result<&str, TextFieldError> {
        self.active_revision().text.get_chars(char_range).ok_or(TextFieldError::InvalidArgument)
    }

    pub fn set_text_range(
        &mut self,
        transaction_id: &TransactionId,
        old_char_range: Range<usize>,
        new_text: impl Into<String>,
    ) -> Result<(), TextFieldError> {
        self.assert_transaction_id(transaction_id)?;
        match &mut self.active_transaction {
            Some(active_transaction) => {
                active_transaction.edits.push(Edit::SetText(SetTextParams {
                    old_char_range,
                    new_text: new_text.into(),
                }));
                Ok(())
            }
            None => Err(TextFieldError::BadState),
        }
    }

    pub fn set_selection(
        &mut self,
        transaction_id: &TransactionId,
        selection: ftext::Selection,
    ) -> Result<(), TextFieldError> {
        self.assert_transaction_id(&transaction_id)?;
        match &mut self.active_transaction {
            Some(active_transaction) => Ok({
                active_transaction.edits.push(Edit::SetSelection(SetSelectionParams { selection }))
            }),
            None => Err(TextFieldError::BadState),
        }
    }

    pub fn begin_transaction(
        &mut self,
        revision_id: &RevisionId,
    ) -> Result<TransactionId, TextFieldError> {
        self.assert_revision_id(revision_id)?;

        if let Some(_) = self.active_transaction {
            return Err(TextFieldError::BadState);
        }
        let new_id = self.last_transaction_id;
        self.last_transaction_id = self.last_transaction_id + 1;

        let new_id = TransactionId { id: new_id };
        self.active_transaction = Some(Transaction { id: new_id.clone(), edits: vec![] });
        Ok(new_id)
    }

    /// Commits the transaction into a new revision. If the transaction's operations cause an error,
    /// the entire transaction is rolled back.
    pub fn commit_transaction(
        &mut self,
        transaction_id: &TransactionId,
    ) -> Result<(), TextFieldError> {
        self.commit_transaction_internal(transaction_id, None)
    }

    /// During a composition, commits the transaction into a new revision. If the transaction's
    /// operations cause an error, the entire transaction is rolled back.
    pub fn commit_transaction_in_composition(
        &mut self,
        transaction_id: &TransactionId,
        composition_update: CompositionUpdate,
    ) -> Result<(), TextFieldError> {
        self.commit_transaction_internal(transaction_id, Some(composition_update))
    }

    fn commit_transaction_internal(
        &mut self,
        transaction_id: &TransactionId,
        composition_update: Option<CompositionUpdate>,
    ) -> Result<(), TextFieldError> {
        self.assert_transaction_id(transaction_id)?;
        // `expect()` because we just asserted that there's an active transaction.
        // Note that `.take()` means that we've moved the active transaction out of `self`, so if
        // the transaction fails, it will be dropped.
        let active_transaction =
            self.active_transaction.take().expect("expected in-progress transaction");
        self.push_new_revision(|mut temp_revision| {
            for edit in active_transaction.edits {
                match edit {
                    Edit::SetSelection(args) => {
                        temp_revision.selection = args.selection;
                    }
                    Edit::SetText(SetTextParams { old_char_range, new_text }) => {
                        let old_full_length = temp_revision.length_code_points;
                        let old_active_length = old_char_range.len();
                        let active_byte_range = temp_revision
                            .text
                            .to_byte_range(old_char_range.clone())
                            .ok_or_else(|| TextFieldError::InvalidSelection)?;

                        temp_revision.text.replace_range(active_byte_range, new_text.as_str());
                        temp_revision.length_code_points =
                            old_full_length - old_active_length + new_text.chars().count();

                        // Clear the selection, put the caret at the end of the edit
                        temp_revision.selection.base =
                            (old_char_range.start + new_text.chars().count()) as u32;
                        temp_revision.selection.extent = temp_revision.selection.base;
                    }
                }
            }
            if let Some(composition_update) = composition_update {
                temp_revision.composition_segments = composition_update.composition_segments;
                temp_revision.highlighted_segment = composition_update.highlighted_segment;
            }
            Ok(temp_revision)
        })
    }

    /// Clones the current revision to create a new one (while incrementing the revision ID),
    /// applies the given mutation function to update the revision data, and pushes the revision to
    /// the top of the stack as the active revision.
    fn push_new_revision(
        &mut self,
        update_revision: impl FnOnce(Revision) -> Result<Revision, TextFieldError>,
    ) -> Result<(), TextFieldError> {
        let new_revision_id = self.next_revision_id();
        let mut temp_revision = self.active_revision().clone();
        temp_revision.id = new_revision_id.clone();
        let new_revision = update_revision(temp_revision)?;
        self.revisions.insert(new_revision_id.clone(), new_revision);
        self.active_revision_id = new_revision_id;
        Ok(())
    }

    pub fn cancel_transaction(
        &mut self,
        transaction_id: &TransactionId,
    ) -> Result<(), TextFieldError> {
        self.assert_transaction_id(transaction_id)?;
        self.active_transaction = None;
        Ok(())
    }

    pub fn begin_composition(&mut self, revision_id: &RevisionId) -> Result<(), TextFieldError> {
        self.assert_revision_id(revision_id)?;
        self.composition_state = CompositionState::Composing(ComposingDetails {
            before_composition_revision_id: revision_id.clone(),
        });
        Ok(())
    }

    pub fn complete_composition(&mut self) -> Result<(), TextFieldError> {
        self.push_new_revision(|mut new_revision| {
            new_revision.composition_segments = vec![];
            new_revision.highlighted_segment = None;
            Ok(new_revision)
        })
    }

    pub fn cancel_composition(&mut self) -> Result<(), TextFieldError> {
        let revert_to = match &self.composition_state {
            CompositionState::NotComposing => {
                return Err(TextFieldError::BadState);
            }
            CompositionState::Composing(details) => details.before_composition_revision_id,
        };
        self.active_revision_id = revert_to.clone();
        // Discard all the temporary revisions created during the composition.
        let range_to_prune = revert_to.next()..;
        self.prune_revisions(range_to_prune);
        Ok(())
    }

    pub fn composition_segments(&self) -> &[CompositionSegment] {
        &self.active_revision().composition_segments[..]
    }

    pub fn highlighted_segment(&self) -> Option<&CompositionSegment> {
        let active_revision = self.active_revision();
        active_revision.highlighted_segment.map(|i| &active_revision.composition_segments[i])
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum TextEditModelProperty {
    /// Text contents changed
    Text,
    Selection,
    CompositionSegments,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Revision {
    text: String,
    /// Cached length of the text `char`s (code points), _not_ bytes.
    length_code_points: usize,
    id: RevisionId,
    selection: ftext::Selection,
    /// Empty when not composing.
    composition_segments: Vec<CompositionSegment>,
    highlighted_segment: Option<usize>,
}

impl Default for Revision {
    fn default() -> Self {
        Self {
            text: "".to_string(),
            length_code_points: 0,
            id: RevisionId { id: 0 },
            selection: ftext::Selection {
                base: 0,
                extent: 0,
                affinity: ftext::TextAffinity::Downstream,
            },
            composition_segments: vec![],
            highlighted_segment: None,
        }
    }
}

/// Whether or not the text field is currently composing, and if it is, additional state properties.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CompositionState {
    NotComposing,
    Composing(ComposingDetails),
}

/// Additional text field state needed while composing.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ComposingDetails {
    // Last revision ID before the composition began
    before_composition_revision_id: RevisionId,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct Transaction {
    id: TransactionId,
    edits: Vec<Edit>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum Edit {
    SetSelection(SetSelectionParams),
    SetText(SetTextParams),
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct SetSelectionParams {
    selection: ftext::Selection,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct SetTextParams {
    old_char_range: Range<usize>,
    new_text: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CompositionUpdate {
    composition_segments: Vec<CompositionSegment>,
    highlighted_segment: Option<usize>,
}

impl TryFrom<FidlCompositionUpdate> for CompositionUpdate {
    type Error = TextFieldError;

    fn try_from(source: FidlCompositionUpdate) -> Result<Self, Self::Error> {
        let u = CompositionUpdate {
            composition_segments: source
                .composition_segments
                .into_iter()
                .flat_map(|vec| vec.into_iter())
                .map(|s| s.try_into())
                .collect::<Result<Vec<CompositionSegment>, TextFieldError>>()?,
            highlighted_segment: source.highlighted_segment.map(|i| i as usize),
        };
        Ok(u)
    }
}

/// Needed because the FIDL `CompositionSegment` doesn't implement `Eq`.
#[derive(Debug, Clone, PartialEq)]
pub struct CompositionSegment(FidlCompositionSegment);

impl Eq for CompositionSegment {}

impl CompositionSegment {
    pub fn new(
        range: Range<u32>,
        raw_input_text: impl Into<String>,
    ) -> Result<Self, TextFieldError> {
        if range.end < range.start {
            return Err(TextFieldError::InvalidSelection);
        }
        Ok(CompositionSegment(FidlCompositionSegment {
            range: Some(ftext::Range { start: range.start, end: range.end }),
            raw_input_text: Some(raw_input_text.into()),
            ..FidlCompositionSegment::EMPTY
        }))
    }

    fn range(&self) -> Range<usize> {
        let raw_range = self.0.range.expect("expected range");
        raw_range.start as usize..raw_range.end as usize
    }

    fn raw_input_text(&self) -> &str {
        self.0.raw_input_text.as_ref().expect("expected raw_input_text").as_str()
    }
}

impl TryFrom<FidlCompositionSegment> for CompositionSegment {
    type Error = TextFieldError;

    /// The `range` and `raw_input_text` fields must both be populated for this to succeed.
    fn try_from(value: FidlCompositionSegment) -> Result<Self, Self::Error> {
        if value.range.is_none() || value.raw_input_text.is_none() {
            Err(TextFieldError::InvalidArgument)
        } else {
            Ok(CompositionSegment(value))
        }
    }
}

trait RevisionIdExt {
    fn next(&self) -> Self;
}

impl RevisionIdExt for ftext::RevisionId {
    fn next(&self) -> Self {
        ftext::RevisionId { id: self.id + 1 }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Result, fable_lib::fable, fuchsia_async as fasync};

    fn text_field_options() -> ftext::TextFieldOptions {
        ftext::TextFieldOptions {
            input_type: Some(ftext::InputType::Text),
            ..ftext::TextFieldOptions::EMPTY
        }
    }

    #[test]
    fn initial_state_is_valid() {
        let options = text_field_options();
        let model = TextEditModel::new(options).unwrap();
        let state = model.state();
        let range = state.contents_range.unwrap().into_range();
        assert_eq!(0..0, range);

        let contents = model.get_text_range(range).unwrap();
        assert_eq!("", contents);
    }

    #[test]
    fn basic_appending() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc😀").unwrap();
        assert_eq!("", model.get_text_range(0..).unwrap());

        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc😀", model.get_text_range(..).unwrap());
        assert_eq!(
            ftext::Selection { base: 4, extent: 4, affinity: ftext::TextAffinity::Downstream },
            model.state().selection.unwrap()
        );
        let revision_id_2 = model.state().revision_id.unwrap();
        assert_ne!(revision_id_1, revision_id_2);

        let len: usize = model.state().contents_range.unwrap().end.try_into().unwrap();

        let transaction_id = model.begin_transaction(&revision_id_2).unwrap();
        model.set_text_range(&transaction_id, len..len, "def").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc😀def", model.get_text_range(..).unwrap());
    }

    #[test]
    fn insertion() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc😀").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc😀", model.get_text_range(..).unwrap());

        let revision_id_2 = model.state().revision_id.unwrap();
        let len: usize = model.state().contents_range.unwrap().end.try_into().unwrap();
        let transaction_id = model.begin_transaction(&revision_id_2).unwrap();
        model.set_text_range(&transaction_id, 3..3, "def").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abcdef😀", model.get_text_range(..).unwrap());
        assert_eq!(
            ftext::Selection { base: 6, extent: 6, affinity: ftext::TextAffinity::Downstream },
            model.state().selection.unwrap()
        );
    }

    #[test]
    fn prior_selection_does_not_affect_replacement() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc🇧🇪def").unwrap();
        model
            .set_selection(
                &transaction_id,
                ftext::Selection { base: 5, extent: 8, affinity: ftext::TextAffinity::Downstream },
            )
            .unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc🇧🇪def", model.get_text_range(..).unwrap());
        assert_eq!(
            model.state().selection,
            Some(ftext::Selection {
                base: 5,
                extent: 8,
                affinity: ftext::TextAffinity::Downstream,
            })
        );

        let revision_id_2 = model.state().revision_id.unwrap();
        let len: usize = model.state().contents_range.unwrap().end.try_into().unwrap();
        let transaction_id = model.begin_transaction(&revision_id_2).unwrap();
        // Note that we're replacing the 🇧 in `🇧 🇪` with a 🇩.
        model.set_text_range(&transaction_id, 0..4, "ghi🇩").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("ghi🇩🇪def", model.get_text_range(..).unwrap());
        // Note that the caret ends up in the middle of the flag. It would be up to the text server
        // to set the caret/selection properly after making this kind of edit.
        assert_eq!(
            ftext::Selection { base: 4, extent: 4, affinity: ftext::TextAffinity::Downstream },
            model.state().selection.unwrap()
        );
    }

    #[test]
    fn backspace() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc😀").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc😀", model.get_text_range(..).unwrap());

        let revision_id_2 = model.state().revision_id.unwrap();
        let len: usize = model.state().contents_range.unwrap().end.try_into().unwrap();
        let transaction_id = model.begin_transaction(&revision_id_2).unwrap();
        model.set_text_range(&transaction_id, len - 1..len, "").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc", model.get_text_range(..).unwrap());
        assert_eq!(
            ftext::Selection { base: 3, extent: 3, affinity: ftext::TextAffinity::Downstream },
            model.state().selection.unwrap()
        );
    }

    #[test]
    fn multiple_operations_in_transaction() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc").unwrap();
        model.set_text_range(&transaction_id, 3..3, "😀").unwrap();
        model.set_text_range(&transaction_id, 0..0, "def").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("defabc😀", model.get_text_range(..).unwrap());
        assert_eq!(
            ftext::Selection { base: 3, extent: 3, affinity: ftext::TextAffinity::Downstream },
            model.state().selection.unwrap()
        );
    }

    #[test]
    fn transactions_can_be_cancelled() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();
        model.set_text_range(&transaction_id, 0..0, "abc").unwrap();
        model.commit_transaction(&transaction_id).unwrap();
        assert_eq!("abc", model.get_text_range(0..).unwrap());
        let revision_id_2 = model.state().revision_id.unwrap();

        let transaction_id = model.begin_transaction(&revision_id_2).unwrap();
        model.set_text_range(&transaction_id, 0..3, "def").unwrap();
        model.set_text_range(&transaction_id, 3..3, "ghi").unwrap();
        model.cancel_transaction(&transaction_id).unwrap();
        assert_eq!("abc", model.get_text_range(..).unwrap());
        assert_eq!(revision_id_2, model.state().revision_id.unwrap());
    }

    // TODO: Can't use wrong revision ID
    // TODO: Can't start overlapping transactions
    // TODO: Can't cancel a transaction if one isn't in progress
    // TODO: Can't use wrong transaction ID

    #[test]
    fn basic_composition() {
        let options = text_field_options();
        let mut model = TextEditModel::new(options).unwrap();
        let revision_id_1 = model.state().revision_id.unwrap();

        model.begin_composition(&revision_id_1).unwrap();
        assert!(model.composition_segments().is_empty());
        assert!(model.highlighted_segment().is_none());

        let transaction_id = model.begin_transaction(&revision_id_1).unwrap();

        model.set_text_range(&transaction_id, 0..0, "добрый день").unwrap();
        model
            .commit_transaction_in_composition(
                &transaction_id,
                CompositionUpdate {
                    composition_segments: vec![
                        CompositionSegment::new(0..6, "dobryj").unwrap(),
                        CompositionSegment::new(7..11, "den'").unwrap(),
                    ],
                    highlighted_segment: Some(1),
                },
            )
            .unwrap();

        assert_eq!(model.get_text_range(0..11).unwrap(), "добрый день");
        assert_eq!(
            &[
                CompositionSegment::new(0..6, "dobryj").unwrap(),
                CompositionSegment::new(7..11, "den'").unwrap()
            ],
            model.composition_segments()
        );
        assert_eq!(
            Some(&CompositionSegment::new(7..11, "den'").unwrap()),
            model.highlighted_segment()
        );
        let revision_id_2 = model.state().revision_id.unwrap();
        assert_ne!(revision_id_1, revision_id_2);

        model.complete_composition().unwrap();
        assert_eq!(model.get_text_range(0..11).unwrap(), "добрый день");
        assert_ne!(revision_id_2, model.state().revision_id.unwrap());
        assert!(model.composition_segments().is_empty());
        assert_eq!(None, model.highlighted_segment());
    }

    // TODO: Can't begin composition during transaction
    // TODO: Can't commit composition if one hasn't started
    // TODO: Can't cancel composition if one hasn't started
    // TODO: Many more tests, edge cases.
}
