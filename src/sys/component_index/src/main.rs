// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `component_index` provides a fuzzy search for locally-known component URLs.

use anyhow::{Context as _, Error};
use fidl_fuchsia_sys_index::{
    ComponentIndexRequest, ComponentIndexRequestStream, FuzzySearchError,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use std::fs;
use std::sync::Arc;

const MAX_RESULT_ITEMS: usize = 20;

async fn run_fuzzy_search_server(
    mut stream: ComponentIndexRequestStream,
    index: Arc<Vec<String>>,
) -> Result<(), Error> {
    while let Some(ComponentIndexRequest::FuzzySearch { needle, responder }) =
        stream.try_next().await.context("Error serving fuzzy search")?
    {
        if !check_needle(&needle) {
            responder
                .send(&mut Err(FuzzySearchError::MalformedInput))
                .context("error sending response")?;
        } else {
            let res: Vec<String> = index
                .iter()
                .filter(|c| c.contains(&needle))
                .take(MAX_RESULT_ITEMS)
                .cloned()
                .collect();
            responder.send(&mut Ok(res)).context("error sending response")?;
        }
    }
    Ok(())
}

/// Needle only accepts [A-Z a-z 0-9 / _ - .].
fn check_needle(needle: &str) -> bool {
    return needle
        .chars()
        .all(|c| c.is_alphanumeric() || c == '/' || c == '_' || c == '-' || c == '.');
}

enum IncomingServices {
    ComponentIndex(ComponentIndexRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();

    let index_string = fs::read_to_string("/pkg/data/component_index.txt")
        .expect("Error reading component_index.txt");
    let index_vec: Vec<String> = index_string.lines().map(|l| l.to_string()).collect();
    let index = Arc::new(index_vec);

    fs.dir("svc").add_fidl_service(IncomingServices::ComponentIndex);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |IncomingServices::ComponentIndex(stream)| {
        run_fuzzy_search_server(stream, index.clone()).unwrap_or_else(|e| println!("{:?}", e))
    });
    fut.await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_check_needle {
        (
            $(
                $test_name:ident => {
                    needle = $fuzzy_needle:expr,
                    accept = $fuzzy_accept:expr,
                }
            )+
        ) => {
            $(
                mod $test_name {
                    use super::*;
                    #[test]
                    fn test_eq() {
                        assert_eq!(check_needle($fuzzy_needle), $fuzzy_accept);
                    }
                }
            )+
        }
    }

    test_check_needle! {
        test_parse_alphanumeric => {
            needle = "F00bar",
            accept = true,
        }
        test_parse_dashes => {
            needle = "foo_bar-baz",
            accept = true,
        }
        test_parse_forward_slash => {
            needle = "foo/bar",
            accept = true,
        }
        test_parse_period => {
            needle = "foo.cmx",
            accept = true,
        }
        test_parse_false => {
            needle = "foo bar",
            accept = false,
        }
        test_parse_url => {
            // colon is invalid character
            needle = "fuchsia-pkg://",
            accept = false,
        }
        test_parse_bang => {
            needle = "hello!",
            accept = false,
        }
        test_parse_shebang => {
            needle = "#!/etc/bin",
            accept = false,
        }
        test_parse_hash => {
            needle = "package#foo.cmx",
            accept = false,
        }
    }

    mod fuzzy_search_server {
        use super::*;
        use fidl::endpoints::create_proxy_and_stream;
        use fidl_fuchsia_sys_index::ComponentIndexMarker;
        use futures::pin_mut;
        use matches::assert_matches;

        #[fasync::run_until_stalled(test)]
        async fn reports_matching_component_to_client() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let search_result_fut = search_proxy.fuzzy_search("a");
            let server_exit_fut = run_fuzzy_search_server(
                query_stream,
                Arc::new(vec!["component_a".to_owned(), "component_b".to_owned()]),
            );
            pin_mut!(search_result_fut);
            pin_mut!(server_exit_fut);

            match futures::future::select(search_result_fut, server_exit_fut).await {
                future::Either::Left((search_result, _server_fut)) => {
                    assert_matches!(search_result, Ok(Ok(results)) if results.as_slice() == ["component_a"])
                }
                future::Either::Right((server_exit, _search_fut)) => {
                    panic!("server terminated with `{:?}`", server_exit)
                }
            };
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn informs_client_of_error_on_invalid_query() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let search_result_fut = search_proxy.fuzzy_search("bad:query");
            let server_exit_fut = run_fuzzy_search_server(
                query_stream,
                Arc::new(vec!["component_a".to_owned(), "component_b".to_owned()]),
            );
            pin_mut!(search_result_fut);
            pin_mut!(server_exit_fut);

            match futures::future::select(search_result_fut, server_exit_fut).await {
                future::Either::Left((search_result, _server_fut)) => {
                    assert_matches!(search_result, Ok(Err(_)))
                }
                future::Either::Right((server_exit, _search_fut)) => {
                    panic!("server terminated with `{:?}`", server_exit)
                }
            };
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn reports_empty_results_to_client_on_no_match() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let search_result_fut = search_proxy.fuzzy_search("a");
            let server_exit_fut =
                run_fuzzy_search_server(query_stream, Arc::new(vec!["component_b".to_owned()]));
            pin_mut!(search_result_fut);
            pin_mut!(server_exit_fut);

            match futures::future::select(search_result_fut, server_exit_fut).await {
                future::Either::Left((search_result, _server_fut)) => {
                    assert_matches!(search_result, Ok(Ok(results)) if results.len() == 0)
                }
                future::Either::Right((server_exit, _search_fut)) => {
                    panic!("server terminated with `{:?}`", server_exit)
                }
            };
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn upper_bounds_size_of_results_sent_to_client() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let search_result_fut = search_proxy.fuzzy_search("c");
            let component_list =
                (1..=MAX_RESULT_ITEMS * 2).map(|i| format!("component {}", i)).collect::<Vec<_>>();
            let server_exit_fut = run_fuzzy_search_server(query_stream, Arc::new(component_list));
            pin_mut!(search_result_fut);
            pin_mut!(server_exit_fut);

            match futures::future::select(search_result_fut, server_exit_fut).await {
                future::Either::Left((search_result, _server_fut)) => {
                    assert_matches!(search_result, Ok(Ok(results)) if results.len() <= MAX_RESULT_ITEMS)
                }
                future::Either::Right((server_exit, _search_fut)) => {
                    panic!("server terminated with `{:?}`", server_exit)
                }
            };
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn does_not_spuriously_drop_matches() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let search_result_fut = search_proxy.fuzzy_search("A");
            let component_list = (1..=MAX_RESULT_ITEMS)
                .map(|i| format!("component {}", i))
                .chain(std::iter::once("Component A".to_owned()))
                .collect::<Vec<_>>();
            let server_exit_fut = run_fuzzy_search_server(query_stream, Arc::new(component_list));
            pin_mut!(search_result_fut);
            pin_mut!(server_exit_fut);

            match futures::future::select(search_result_fut, server_exit_fut).await {
                future::Either::Left((search_result, _server_fut)) => {
                    assert_matches!(search_result, Ok(Ok(results)) if results.as_slice() == ["Component A"])
                }
                future::Either::Right((server_exit, _search_fut)) => {
                    panic!("server terminated with `{:?}`", server_exit)
                }
            };
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn terminates_with_error_on_failed_write() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let server_exit_fut =
                run_fuzzy_search_server(query_stream, Arc::new(vec!["A".to_owned()]));
            search_proxy.fuzzy_search("A"); // Note: request is sent synchronously
            std::mem::drop(search_proxy);
            assert_matches!(server_exit_fut.await, Err(_));
            Ok(())
        }

        #[fasync::run_until_stalled(test)]
        async fn terminates_with_ok_on_end_of_stream() -> Result<(), Error> {
            let (search_proxy, query_stream) = create_proxy_and_stream::<ComponentIndexMarker>()?;
            let server_exit_fut =
                run_fuzzy_search_server(query_stream, Arc::new(vec!["A".to_owned()]));
            std::mem::drop(search_proxy);
            assert_matches!(server_exit_fut.await, Ok(()));
            Ok(())
        }
    }
}
