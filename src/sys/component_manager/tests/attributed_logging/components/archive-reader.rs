// Copyright 2020 the Fuchsia Authors. All rights reserved.
// Use of this source code is goverened by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_data::Logs, diagnostics_reader::ArchiveReader, fuchsia_async as fasync,
    futures::stream::StreamExt, std::collections::HashMap, std::vec::Vec,
};

#[fasync::run_singlethreaded]
async fn main() {
    let reader = ArchiveReader::new();
    let mut non_matching_logs = vec![];

    let mut treasure = HashMap::<String, Vec<Vec<&str>>>::new();
    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-for-offer-from-parent".to_string(),
        vec![vec![
            "Failed to route",
            "fidl.test.components.Trigger",
            "target component `/routing-tests/offers-to-children-unavailable/child-for-offer-from-parent`",
            "`offer from parent` declaration was found at `/routing-tests/offers-to-children-unavailable`",
            "no matching `offer` declaration was found in the parent",
        ]],
    );
    treasure.insert(
        "routing-tests/child".to_string(),
        vec![vec![
            "Failed to route",
            "`fidl.test.components.Trigger`",
            "target component `/routing-tests/child`",
            "`use from parent`",
            "at `/routing-tests/child`",
            "no matching `offer` declaration was found in the parent",
        ]],
    );
    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-for-offer-from-sibling".to_string(),
        vec![vec![
            "Failed to route",
            "`fidl.test.components.Trigger`",
            "target component `/routing-tests/offers-to-children-unavailable/child-for-offer-from-sibling`",
            "`offer from #child-that-doesnt-expose` declaration was found",
            "no matching `expose` declaration",
        ]]
    );

    treasure.insert(
        "routing-tests/offers-to-children-unavailable/child-open-unrequested".to_string(),
        vec![vec![
            "No capability available",
            "fidl.test.components.Trigger",
            "/routing-tests/offers-to-children-unavailable/child-open-unrequested",
            "`use` declaration",
        ]],
    );

    if let Ok(mut results) = reader.snapshot_then_subscribe::<Logs>() {
        while let Some(Ok(log_record)) = results.next().await {
            if let Some(log_str) = log_record.msg() {
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
