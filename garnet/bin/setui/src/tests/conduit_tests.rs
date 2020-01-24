// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use crate::conduit::base::{Conduit, ConduitData};
use crate::conduit::conduit_impl::ConduitImpl;
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent, SettingType};
use futures::stream::StreamExt;

#[fuchsia_async::run_singlethreaded(test)]
async fn test_propagation() {
    let conduit_handle = ConduitImpl::create();
    let (upstream_tx, mut upstream_rx) = conduit_handle.lock().await.create_waypoint();
    let (downstream_tx, mut downstream_rx) = conduit_handle.lock().await.create_waypoint();

    let action = SettingAction {
        id: 0,
        setting_type: SettingType::Unknown,
        data: SettingActionData::Listen(2),
    };
    upstream_tx.send(ConduitData::Action(action.clone()));

    if let Some(ConduitData::Action(received_action)) = downstream_rx.next().await {
        assert_eq!(action, received_action);
    } else {
        panic!("should receive action");
    }

    let event = SettingEvent::Changed(SettingType::Unknown);

    downstream_tx.send(ConduitData::Event(event));

    if let Some(ConduitData::Event(SettingEvent::Changed(changed_type))) = upstream_rx.next().await
    {
        assert_eq!(changed_type, SettingType::Unknown);
    } else {
        panic!("should receive event");
    }
}
