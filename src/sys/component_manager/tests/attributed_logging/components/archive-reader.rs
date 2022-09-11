// Copyright 2020 the Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use tracing::info;
use {
    diagnostics_data::Logs, diagnostics_reader::ArchiveReader, fuchsia_async as fasync,
    futures::stream::StreamExt, std::collections::HashMap, std::vec::Vec,
};

#[fasync::run_singlethreaded]
async fn main() {
    diagnostics_log::init!(
        &["archive-reader"],
        diagnostics_log::Interest {
            min_severity: Some(diagnostics_log::Severity::Info),
            ..diagnostics_log::Interest::EMPTY
        }
    );

    let reader = ArchiveReader::new();
    let mut non_matching_logs = vec![];

    type Fingerprint = Vec<&'static str>;
    let mut treasure = HashMap::<String, Vec<Fingerprint>>::new();
    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-for-offer-from-parent".to_string(),
        vec![vec![
            "Required",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/offers-to-children-unavailable/child-for-offer-from-parent`",
            "`/root/routing-tests/offers-to-children-unavailable` tried to offer \
            `fidl.test.components.Trigger` from its parent",
            "but the parent does not offer",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable-but-optional/child-for-offer-from-parent".to_string(),
        vec![vec![
            "Optional",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/offers-to-children-unavailable-but-optional/child-for-offer-from-parent`",
            "Target optionally uses capability that was not available",
            "`/root/routing-tests/offers-to-children-unavailable-but-optional` tried to offer \
            `fidl.test.components.Trigger` from its parent",
            "but the parent does not offer",
        ]],
    );
    treasure.insert(
        "routing-tests/child".to_string(),
        vec![vec![
            "Required",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/child`",
            "`/root/routing-tests/child` tried to use `fidl.test.components.Trigger` from its parent",
            "but the parent does not offer",
        ]],
    );
    treasure.insert(
        "routing-tests/child-with-optional-use".to_string(),
        vec![vec![
            "Optional",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/child-with-optional-use`",
            "Target optionally uses capability that was not available",
            "`/root/routing-tests/child-with-optional-use` tried to use \
            `fidl.test.components.Trigger` from its parent",
            "but the parent does not offer",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-for-offer-from-sibling".to_string(),
        vec![vec![
            "Required",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/offers-to-children-unavailable/child-for-offer-from-sibling`",
            "`/root/routing-tests/offers-to-children-unavailable` tried to offer",
            "from its child `#child-that-doesnt-expose`",
            "`#child-that-doesnt-expose` does not expose `fidl.test.components.Trigger`",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable-but-optional/child-for-offer-from-sibling".to_string(),
        vec![vec![
            "Optional",
            "protocol `fidl.test.components.Trigger`",
            "not available for target component \
            `/root/routing-tests/offers-to-children-unavailable-but-optional/child-for-offer-from-sibling`",
            "Target optionally uses capability that was not available",
            "`/root/routing-tests/offers-to-children-unavailable-but-optional` tried to offer",
            "from its child `#child-that-doesnt-expose`",
            "`#child-that-doesnt-expose` does not expose `fidl.test.components.Trigger`",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-open-unrequested".to_string(),
        vec![vec![
            "No capability available",
            "fidl.test.components.Trigger",
            "/root/routing-tests/offers-to-children-unavailable/child-open-unrequested",
            "`use` declaration",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable-but-optional/child-open-unrequested".to_string(),
        vec![vec![
            "No capability available",
            "fidl.test.components.Trigger",
            "/root/routing-tests/offers-to-children-unavailable-but-optional/child-open-unrequested",
            "`use` declaration",
        ]],
    );

    if let Ok(mut results) = reader.snapshot_then_subscribe::<Logs>() {
        while let Some(Ok(log_record)) = results.next().await {
            if let Some(log_str) = log_record.msg() {
                info!("Log from {}: {}", log_record.moniker, log_str);
                match treasure.get_mut(&log_record.moniker) {
                    None => non_matching_logs.push(log_record),
                    Some(log_fingerprints) => {
                        let removed = {
                            let print_count = log_fingerprints.len();
                            log_fingerprints.retain(|fingerprint| {
                                // If all the part of the fingerprint match, remove
                                // the fingerprint, otherwise keep it.
                                let has_all_features =
                                    fingerprint.iter().all(|feature| log_str.contains(feature));
                                !has_all_features
                            });

                            print_count != log_fingerprints.len()
                        };

                        // If there are no more fingerprint sets for this
                        // component, remove it
                        if log_fingerprints.is_empty() {
                            treasure.remove(&log_record.moniker);
                        }
                        // If we didn't remove any fingerprints, this log didn't
                        // match anything, so push it into the non-matching logs.
                        if !removed {
                            non_matching_logs.push(log_record);
                        }
                        if treasure.is_empty() {
                            return;
                        }
                    }
                }
            }
        }
    }
    panic!(
        "One or more logs were not found, remaining fingerprints: {:?}\n\n
        These log records were read, but did not match any fingerprints: {:?}",
        treasure, non_matching_logs
    );
}
