// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, format_err, Context as _, Error};
use fidl_fuchsia_ui_text as txt;
use fuchsia_async::TimeoutExt;
use futures::prelude::*;
use std::collections::HashSet;
use std::convert::TryInto;
use text::text_field_state::TextFieldState;

pub struct TextFieldWrapper {
    proxy: txt::TextFieldProxy,
    last_state: TextFieldState,
    defunct_point_ids: HashSet<u64>,
    current_point_ids: HashSet<u64>,
}

/// This wraps the TextFieldProxy, and provides convenient features like storing the last state
/// update, various validation functions for tests. It also a great place to add checks that work
/// across all tests; for instance, right now, it validates that all TextPoints in all function
/// calls are not reused across revisions, and that any distance or contents check works even if
/// the range is inverted.
impl TextFieldWrapper {
    /// Creates a new TextFieldWrapper from a proxy. This is a async function and can fail, since it
    /// waits for the initial state update to come from the TextField.
    pub async fn new(proxy: txt::TextFieldProxy) -> Result<TextFieldWrapper, Error> {
        let state = get_update(&proxy).await.context("Receiving initial state.")?;
        Ok(TextFieldWrapper {
            proxy,
            current_point_ids: all_point_ids_for_state(&state),
            defunct_point_ids: HashSet::new(),
            last_state: state,
        })
    }

    /// Returns a cloned version of the latest state from the server. To update this, either use one
    /// of the editing methods on the TextFieldWrapper, or if making calls on the proxy directly,
    /// call `text_field_wrapper.wait_for_update().await` after you expect a new state update from
    /// the TextField.
    pub fn state(&self) -> TextFieldState {
        self.last_state.clone()
    }

    /// Waits for an on_update event from the TextFieldProxy, and updates the last state tracked
    /// by TextFieldWrapper. Edit functions on TextFieldWrapper itself already call this; only
    /// use it if you're doing something with the TextFieldProxy directly. This also validates
    /// that document, selection, and revision are all set on last_state, so these fields can be
    /// unwrapped in other parts of the code.
    pub async fn wait_for_update(&mut self) -> Result<(), Error> {
        self.defunct_point_ids =
            &self.defunct_point_ids | &all_point_ids_for_state(&self.last_state);
        self.last_state = match get_update(&self.proxy).await {
            Ok(v) => v,
            Err(e) => return Err(format_err!(format!("{}", e))),
        };
        self.current_point_ids = all_point_ids_for_state(&self.last_state);
        self.validate_point_ids()
    }

    /// An internal function that validates the current_point_ids and defunct_point_ids are
    /// disjoint sets. If they aren't disjoint, then the TextField incorrectly reused a
    /// point ID between two revisions.
    fn validate_point_ids(&mut self) -> Result<(), Error> {
        let in_both_sets = &self.current_point_ids & &self.defunct_point_ids;
        if in_both_sets.len() != 0 {
            let as_strings: Vec<String> =
                in_both_sets.into_iter().map(|i| format!("{}", i)).collect();
            bail!(format!(
                "Expected TextPoint ids to not be reused between revisions: {}",
                as_strings.join(", ")
            ))
        }
        Ok(())
    }

    /// Returns a handle to the raw TextFieldProxy, useful for sending it weird unexpected
    /// input and making sure it responds correctly.
    pub fn proxy(&self) -> &txt::TextFieldProxy {
        &self.proxy
    }

    /// Inserts text as though the user just typed it, at the caret, replacing any selected text.
    /// Also waits for an on_update state update event before returning.
    pub async fn simple_insert(&mut self, contents: &'static str) -> Result<(), Error> {
        let rev = self.last_state.revision;
        self.proxy.begin_edit(rev)?;
        self.proxy.replace(&mut self.last_state.selection.range, contents)?;
        if self.proxy.commit_edit().await? != txt::Error::Ok {
            return Err(format_err!("Expected commit_edit to succeed"));
        }
        self.wait_for_update().await?;
        Ok(())
    }

    /// Returns a new TextPoint offset from the specified one. Use this function instead of
    /// `text_field_wrapper.proxy().point_offset()` when possible, since it also double checks
    /// any points returned aren't used across revisions. You may need the proxy's
    /// point_offset method when giving weird data to the proxy, though, like incorrect
    /// revision numbers.
    pub async fn point_offset<'a>(
        &'a mut self,
        mut point: &'a mut txt::Position,
        offset: i64,
    ) -> Result<txt::Position, Error> {
        let (new_point, err) =
            self.proxy.position_offset(&mut point, offset, self.last_state.revision).await?;
        if err != txt::Error::Ok {
            return Err(format_err!(format!(
                "Expected point_offset request to succeed, returned {:?} instead",
                err
            )));
        }
        self.current_point_ids.insert(new_point.id);
        if let Err(e) = self.validate_point_ids() {
            return Err(e);
        }
        Ok(new_point)
    }

    /// A convenience function that returns the string contents of a range.
    pub async fn contents<'a>(
        &'a mut self,
        range: &'a mut txt::Range,
    ) -> Result<(String, txt::Position), Error> {
        let (contents, actual_start, err) =
            self.proxy.contents(range, self.last_state.revision).await?;
        if err != txt::Error::Ok {
            return Err(format_err!(format!(
                "Expected contents request to succeed, returned {:?} instead",
                err
            )));
        }
        Ok((contents, actual_start))
    }

    /// A convenience function that returns the distance of a range.
    pub async fn distance<'a>(&'a mut self, range: &'a mut txt::Range) -> Result<i64, Error> {
        let (length, err) = self.proxy.distance(range, self.last_state.revision).await?;
        if err != txt::Error::Ok {
            return Err(format_err!(format!(
                "Expected length request to succeed, returned {:?} instead",
                err
            )));
        }
        Ok(length)
    }

    /// A convenience function that validates that a distance call returns an expected value. Also
    /// double checks that inverting the range correctly negates the returned distance.
    pub async fn validate_distance<'a>(
        &'a mut self,
        range: &'a txt::Range,
        expected_result: i64,
    ) -> Result<(), Error> {
        // try forwards
        let mut new_range = txt::Range {
            start: txt::Position { id: range.start.id },
            end: txt::Position { id: range.end.id },
        };
        let length = self.distance(&mut new_range).await?;
        if length != expected_result {
            bail!(format!(
                "Expected distance request to return {:?}, instead got {:?}",
                expected_result, length
            ))
        };
        // try backwards
        let inverted_expected_result = -expected_result;
        let mut new_range = txt::Range {
            start: txt::Position { id: range.end.id },
            end: txt::Position { id: range.start.id },
        };
        let length = self.distance(&mut new_range).await?;
        if length != inverted_expected_result {
            bail!(format!(
                "Expected distance request to return {:?}, instead got {:?}",
                inverted_expected_result, length
            ))
        };
        Ok(())
    }

    /// A convenience function that validates that a contents call returns an expected value. Also
    /// double checks that inverting the range correctly returns an identical string.
    pub async fn validate_contents<'a>(
        &'a mut self,
        range: &'a txt::Range,
        expected_result: &'a str,
    ) -> Result<(), Error> {
        // try forwards
        let mut new_range = txt::Range {
            start: txt::Position { id: range.start.id },
            end: txt::Position { id: range.end.id },
        };
        let (contents, _true_start_point) = self.contents(&mut new_range).await?;
        if contents != expected_result {
            bail!(format!(
                "Expected contents request to return {:?}, instead got {:?}",
                expected_result, contents
            ))
        };
        // try backwards
        let mut new_range = txt::Range {
            start: txt::Position { id: range.end.id },
            end: txt::Position { id: range.start.id },
        };
        let (contents, _true_start_point) = self.contents(&mut new_range).await?;
        if contents != expected_result {
            bail!(format!(
                "Expected contents request to return {:?}, instead got {:?}",
                expected_result, contents
            ))
        };
        Ok(())
    }
}

async fn get_update(text_field: &txt::TextFieldProxy) -> Result<TextFieldState, Error> {
    let mut stream = text_field.take_event_stream();
    let msg_future = stream
        .try_next()
        .map_err(|e| format_err!(format!("{}", e)))
        .on_timeout(*crate::TEST_TIMEOUT, || {
            Err(format_err!("Waiting for on_update event timed out"))
        });
    let msg = msg_future.await?.ok_or(format_err!("TextMgr event stream unexpectedly closed"))?;
    match msg {
        txt::TextFieldEvent::OnUpdate { state, .. } => Ok(state.try_into()?),
    }
}

fn all_point_ids_for_state(state: &TextFieldState) -> HashSet<u64> {
    let mut point_ids = HashSet::new();
    let mut point_ids_for_range = |range: &txt::Range| {
        point_ids.insert(range.start.id);
        point_ids.insert(range.end.id);
    };
    point_ids_for_range(&state.selection.range);
    state.composition.as_ref().map(|range| point_ids_for_range(&*range));
    state.composition_highlight.as_ref().map(|range| point_ids_for_range(&*range));
    state.dead_key_highlight.as_ref().map(|range| point_ids_for_range(&*range));
    point_ids_for_range(&state.document);
    point_ids
}

#[cfg(test)]
mod test {
    use super::*;
    fn default_range(n: u64) -> txt::Range {
        txt::Range { start: txt::Position { id: n }, end: txt::Position { id: n + 1 } }
    }
    fn default_state(n: u64) -> TextFieldState {
        TextFieldState {
            document: default_range(n),
            selection: txt::Selection {
                range: default_range(n + 2),
                anchor: txt::SelectionAnchor::AnchoredAtStart,
                affinity: txt::Affinity::Upstream,
            },
            composition: None,
            composition_highlight: None,
            dead_key_highlight: None,
            revision: n + 4,
        }
    }

    #[fuchsia_async::run_singlethreaded]
    #[test]
    async fn test_wrapper_insert() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<txt::TextFieldMarker>()
            .expect("Should have created proxy");
        let (mut stream, control_handle) = server_end
            .into_stream_and_control_handle()
            .expect("Should have created stream and control handle");
        control_handle.send_on_update(default_state(0).into()).expect("Should have sent update");
        fuchsia_async::spawn(async {
            let mut wrapper =
                TextFieldWrapper::new(proxy).await.expect("Should have created text field wrapper");
            wrapper.simple_insert("meow!").await.expect("Should have inserted successfully");
        });
        let (revision, _ch) = stream
            .try_next()
            .await
            .expect("Waiting for message failed")
            .expect("Should have sent message")
            .into_begin_edit()
            .expect("Expected BeginEdit");
        assert_eq!(revision, 4);

        let (_range, new_text, _ch) = stream
            .try_next()
            .await
            .expect("Waiting for message failed")
            .expect("Should have sent message")
            .into_replace()
            .expect("Expected Replace");
        assert_eq!(new_text, "meow!");

        let _responder = stream
            .try_next()
            .await
            .expect("Waiting for message failed")
            .expect("Should have sent message")
            .into_commit_edit()
            .expect("Expected CommitEdit");
    }

    #[fuchsia_async::run_singlethreaded]
    #[test]
    async fn test_duplicate_points_cause_error() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<txt::TextFieldMarker>()
            .expect("Should have created proxy");
        let (_stream, control_handle) = server_end
            .into_stream_and_control_handle()
            .expect("Should have created stream and control handle");
        control_handle.send_on_update(default_state(0).into()).expect("Should have sent update");
        let mut wrapper =
            TextFieldWrapper::new(proxy).await.expect("Should have created text field wrapper");

        // send a valid update and make sure it works as expected
        let mut state = default_state(10);
        control_handle.send_on_update(state.clone().into()).expect("Should have sent update");
        let res = wrapper.wait_for_update().await;
        assert!(res.is_ok());
        assert_eq!(wrapper.state().document.start.id, 10);

        // send an update with the same points but an incremented revision
        state.revision += 1;
        control_handle.send_on_update(state.into()).expect("Should have sent update");
        let res = wrapper.wait_for_update().await;
        assert!(res.is_err()); // should fail since some points were reused
    }
}
