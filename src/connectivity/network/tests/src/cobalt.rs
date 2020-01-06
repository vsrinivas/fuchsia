// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;

#[fuchsia_async::run_singlethreaded(test)]
async fn cobalt_metrics() -> Result<(), anyhow::Error> {
    // NB: netstack aggregates observations and logs them to cobalt once per minute.  We wait for
    // calls to LogCobaltEvents to be made, so this test takes about 180 seconds to run at the time
    // of writing. If you're modifying this test and have the ability to change the netstack
    // implementation, reducing that log period will improve the cycle time of this test.

    let logger_querier = fuchsia_component::client::connect_to_service::<
        fidl_fuchsia_cobalt_test::LoggerQuerierMarker,
    >()
    .context("failed to connect to cobalt logger querier")?;
    let address = "127.0.0.1:8080";

    // NB: Creating a socket here also causes netstack to be launched.
    let s1 = std::net::TcpListener::bind(&address).context("failed to bind to localhost")?;

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        // fidl_fuchsia_cobalt::QueryError doesn't implement thiserror::Error.
        .map_err(|e| anyhow::format_err!("queryerror: {:?}", e))?;

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
    let (s3, _sockaddr) = s1.accept()?;

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| anyhow::format_err!("queryerror: {:?}", e))?;

    assert_eq!(
        // We think this i64 suffix is necessary because rustc assigns this constant a type before unifying
        // it with the type of the events_with_id expression below; this may be fixed when
        // https://github.com/rust-lang/rust/issues/57009 is fixed and should be reevaluated then.
        3i64,
        events_with_id(&events, networking_metrics::SOCKET_COUNT_MAX_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .max()
            .expect("expect at least one socket count max event"),
        "events:\n{}\n",
        display_events(&events),
    );

    // The stack sees both the client and server side of the TCP connection.
    // Hence we see the TCP stats below accounting for both sides.
    let tcp_connections_established_events =
        events_with_id(&events, networking_metrics::TCP_CONNECTIONS_ESTABLISHED_TOTAL_METRIC_ID);
    assert_eq!(tcp_connections_established_events.len(), 1);
    assert_eq!(tcp_connections_established_events[0].count, 2);

    std::mem::drop(s1);
    std::mem::drop(s2);

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| anyhow::format_err!("queryerror: {:?}", e))?;

    assert_eq!(
        // https://github.com/rust-lang/rust/issues/57009
        2i64,
        events_with_id(&events, networking_metrics::SOCKETS_DESTROYED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .sum()
    );

    // TODO(43242): This is currently FIN-ACK, ACK but should be FIN-ACK, FIN-ACK.
    const EXPECTED_PACKET_COUNT: i64 = 2;

    // TODO(42092): make these sent/received expected values the same.
    // TCP payload size (12) + TCP headers (20)
    const EXPECTED_SENT_PACKET_SIZE: i64 = 32;
    // TCP payload size (12) + TCP headers (20) + IP minimum size (20)
    const EXPECTED_RECEIVED_PACKET_SIZE: i64 = 52;

    assert_eq!(
        EXPECTED_PACKET_COUNT,
        events_with_id(&events, networking_metrics::PACKETS_SENT_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .max()
            .expect("expected at least one event with PACKETS_SENT_METRIC_ID"),
        "packets sent. events:\n{}\n",
        display_events(&events),
    );
    assert_eq!(
        EXPECTED_PACKET_COUNT,
        events_with_id(&events, networking_metrics::PACKETS_RECEIVED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .max()
            .expect("expected at least one event with PACKETS_RECEIVED_METRIC_ID"),
        "packets received. events:\n{}\n",
        display_events(&events),
    );
    assert_eq!(
        EXPECTED_PACKET_COUNT * EXPECTED_SENT_PACKET_SIZE,
        events_with_id(&events, networking_metrics::BYTES_SENT_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .max()
            .expect("expected at least one event with BYTES_SENT_METRIC_ID"),
        "bytes sent. events:\n{}\n",
        display_events(&events),
    );
    assert_eq!(
        EXPECTED_PACKET_COUNT * EXPECTED_RECEIVED_PACKET_SIZE,
        events_with_id(&events, networking_metrics::BYTES_RECEIVED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .max()
            .expect("expected at least one event with BYTES_RECEIVED_METRIC_ID"),
        "bytes received. events:\n{}\n",
        display_events(&events),
    );

    assert_eq!(
        // https://github.com/rust-lang/rust/issues/57009
        2i64,
        events_with_id(&events, networking_metrics::SOCKETS_DESTROYED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .sum(),
        "sockets destroyed. events:\n{}\n",
        display_events(&events),
    );

    std::mem::drop(s3);

    let (events, _more) = logger_querier
        .watch_logs(
            networking_metrics::PROJECT_ID,
            fidl_fuchsia_cobalt_test::LogMethod::LogCobaltEvents,
        )
        .await
        .context("failed to call query_logger")?
        .map_err(|e| anyhow::format_err!("queryerror: {:?}", e))?;

    assert_eq!(
        // https://github.com/rust-lang/rust/issues/57009
        1i64,
        events_with_id(&events, networking_metrics::SOCKET_COUNT_MAX_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .sum(),
        "socket count max. events:\n{}\n",
        display_events(&events),
    );

    assert_eq!(
        // https://github.com/rust-lang/rust/issues/57009
        1i64,
        events_with_id(&events, networking_metrics::SOCKETS_DESTROYED_METRIC_ID)
            .iter()
            .map(|ev| ev.count)
            .sum(),
        "sockets destroyed. events:\n{}\n",
        display_events(&events)
    );
    // We expect only the server-side to be closed at this point.
    // The client-side TCP connection would not be closed as yet, because we do
    // not want to wait for TCP TIME-WAIT interval in this test.
    // TODO(gvisor.dev/issue/1400) There is currently no way the client can avoid
    // getting into time-wait on close.
    let tcp_connections_established_events =
        events_with_id(&events, networking_metrics::TCP_CONNECTIONS_ESTABLISHED_TOTAL_METRIC_ID);
    assert_eq!(tcp_connections_established_events.len(), 1);
    assert_eq!(tcp_connections_established_events[0].count, 1);
    let tcp_connections_closed_events =
        events_with_id(&events, networking_metrics::TCP_CONNECTIONS_CLOSED_METRIC_ID);
    assert_eq!(tcp_connections_closed_events.len(), 1);
    assert_eq!(tcp_connections_closed_events[0].count, 1);
    let tcp_connections_reset_events =
        events_with_id(&events, networking_metrics::TCP_CONNECTIONS_RESET_METRIC_ID);
    assert_eq!(tcp_connections_reset_events[0].count, 0);
    let tcp_connections_timed_out_events =
        events_with_id(&events, networking_metrics::TCP_CONNECTIONS_TIMED_OUT_METRIC_ID);
    assert_eq!(tcp_connections_timed_out_events[0].count, 0);

    Ok(())
}

fn display_events<'a, I>(events: I) -> String
where
    I: IntoIterator<Item = &'a fidl_fuchsia_cobalt::CobaltEvent>,
{
    itertools::join(events.into_iter().map(|ev| format!("{:?}", ev)), "\n")
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
