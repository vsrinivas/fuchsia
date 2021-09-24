// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_cobalt::{CobaltEvent, EventPayload};
use fidl_fuchsia_cobalt_test::LoggerQuerierProxy;

pub struct ExpectedEvent {
    pub metric_id: u32,
    pub value: i64,
}

pub fn verify_event_present_once(events: &Vec<CobaltEvent>, expected_event: ExpectedEvent) -> bool {
    let mut observed_count = 0;
    for event in events {
        match event.payload {
            EventPayload::EventCount(event_count) => {
                if event.metric_id == expected_event.metric_id
                    && event_count.count == expected_event.value
                {
                    observed_count += 1;
                }
            }
            EventPayload::MemoryBytesUsed(value) => {
                if event.metric_id == expected_event.metric_id && value == expected_event.value {
                    observed_count += 1;
                }
            }
            _ => panic!("Only should be observing EventCount or MemoryBytesUsed; got {:?}", event),
        }
    }
    observed_count == 1
}

pub struct LogQuerierConfig {
    pub project_id: u32,
    pub expected_batch_size: usize,
}

pub async fn gather_sample_group(
    log_querier_config: LogQuerierConfig,
    logger_querier: &LoggerQuerierProxy,
) -> Vec<CobaltEvent> {
    let mut events: Vec<CobaltEvent> = Vec::new();
    loop {
        let watch = logger_querier.watch_logs2(
            log_querier_config.project_id,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvent,
        );

        let (mut new_events, more) = watch.await.unwrap();
        assert!(!more);

        events.append(&mut new_events);
        if events.len() < log_querier_config.expected_batch_size {
            continue;
        }

        if events.len() == log_querier_config.expected_batch_size {
            break;
        }

        panic!("Sampler should provide descrete groups of cobalt events. Shouldn't see more than {:?} here.", log_querier_config.expected_batch_size);
    }

    events
}
