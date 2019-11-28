// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::ResultExt;

#[fuchsia_async::run_singlethreaded(test)]
async fn socket_counts() -> Result<(), failure::Error> {
    // NB: netstack aggregates observations and logs them to cobalt once per minute.
    // We wait for calls to LogCobaltEvents to be made, so this test takes about 180 seconds to
    // run. If you're modifying this test and have the ability to change the netstack
    // implementation, reducing that log period will improve the cycle time of this test.

    let logger_querier = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_cobalt_test::LoggerQuerierMarker,
    >()
    .context("failed to connect to cobalt logger querier")?;
    let address = "127.0.0.1:8080";

    // NB: Creating a socket here also causes netstack to be launched.
    let s1 = std::net::TcpListener::bind(&address)
        .context("failed to bind to localhost")
        .map_err(|e| failure::format_err!("failed calling bind: {:?}", e))?;

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| failure::format_err!("queryerror: {:?}", e))?;

    let socket_count_max_events =
        events_with_id(&events, networking_metrics::SOCKET_COUNT_MAX_METRIC_ID);
    assert_eq!(socket_count_max_events.len(), 1);
    assert_eq!(socket_count_max_events[0].count, 1);

    let sockets_created_events =
        events_with_id(&events, networking_metrics::SOCKETS_CREATED_METRIC_ID);
    assert_eq!(sockets_created_events.len(), 1);
    assert_eq!(sockets_created_events[0].count, 1);
    let sockets_destroyed_events =
        events_with_id(&events, networking_metrics::SOCKETS_DESTROYED_METRIC_ID);
    assert_eq!(sockets_destroyed_events.len(), 1);
    assert_eq!(sockets_destroyed_events[0].count, 0);

    let s2 = std::net::TcpStream::connect(&address)?;
    let s3 = s1.accept()?;

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| failure::format_err!("queryerror: {:?}", e))?;

    assert_eq!(
        3,
        events_with_id(&events, networking_metrics::SOCKET_COUNT_MAX_METRIC_ID)
            .iter()
            .fold(0, |max, ev| std::cmp::max(max, ev.count)),
        "events: {:?}",
        events
    );

    std::mem::drop(s1);
    std::mem::drop(s2);
    std::mem::drop(s3);

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| failure::format_err!("queryerror: {:?}", e))?;
    assert_eq!(
        3,
        events_with_id(&events, networking_metrics::SOCKETS_DESTROYED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .fold(0, |acc, c| acc + c)
    );
    Ok(())
}

// Returns the internal CountEvents of `events` that have the given `id`.
fn events_with_id<'a, I>(events: I, id: u32) -> Vec<&'a fidl_fuchsia_cobalt::CountEvent>
where
    I: IntoIterator<Item = &'a fidl_fuchsia_cobalt::CobaltEvent> + 'a,
{
    events
        .into_iter()
        .filter_map(move |e| match e {
            fidl_fuchsia_cobalt::CobaltEvent {
                payload: fidl_fuchsia_cobalt::EventPayload::EventCount(count_event),
                ..
            } if e.metric_id == id => Some(count_event),
            _other_payload => None,
        })
        .collect()
}
