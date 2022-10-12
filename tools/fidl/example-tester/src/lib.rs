// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    component_events::{events::*, matcher::*, sequence::*},
    diagnostics_data::{Data, Logs},
    diagnostics_reader::ArchiveReader,
    fuchsia_component_test::{Capability, ChildOptions, ChildRef, RealmBuilder, Ref, Route},
    regex::Regex,
    std::future::Future,
    std::{thread, time},
};

/// Represents a component under test. The `name` is the test-local name assigned to the component,
/// whereas the path is the relative path to its component manifest (ex: "#meta/client.cm").
pub trait Component {
    fn get_name(&self) -> String;
    fn get_path(&self) -> String;
    fn get_regex_matcher(&self) -> String;
    fn matches_log(&self, raw_log: &Data<Logs>) -> bool;
}

/// Represents the client component under test.
pub struct Client<'a> {
    name: String,
    path: &'a str,
    regex: Regex,
    matcher: String,
}

impl<'a> Client<'a> {
    /// Create a new instance of a client component, passing in two string references: the name of
    /// the test that created this component, and the path to the `*.cm` file describing this
    /// component in the current package:
    ///
    ///   Client::new("my_test_case", "#meta/some_client.cm");
    ///
    pub fn new(test_prefix: &str, path: &'a str) -> Self {
        let name = format!("{}_client", test_prefix);
        let matcher = format!("{}$", name);
        Self { regex: Regex::new(matcher.as_str()).unwrap(), name, path, matcher }
    }
}

impl<'a> Component for Client<'a> {
    fn get_name(&self) -> String {
        self.name.clone()
    }
    fn get_path(&self) -> String {
        self.path.to_owned()
    }
    fn get_regex_matcher(&self) -> String {
        self.matcher.clone()
    }
    fn matches_log(&self, raw_log: &Data<Logs>) -> bool {
        self.regex.is_match(raw_log.moniker.as_str())
    }
}

/// Represents a proxy component under test.
pub struct Proxy<'a> {
    name: String,
    path: &'a str,
    regex: Regex,
    matcher: String,
}

impl<'a> Proxy<'a> {
    /// Create a new instance of a proxy component, passing in two string references: the name of
    /// the test that created this component, and the path to the `*.cm` file describing this
    /// component in the current package:
    ///
    ///   Proxy::new("my_test_case", "#meta/some_proxy.cm");
    ///
    pub fn new(test_prefix: &str, path: &'a str) -> Self {
        let name = format!("{}_proxy", test_prefix);
        let matcher = format!("{}$", name);
        Self { regex: Regex::new(matcher.as_str()).unwrap(), name, path, matcher }
    }
}

impl<'a> Component for Proxy<'a> {
    fn get_name(&self) -> String {
        self.name.clone()
    }
    fn get_path(&self) -> String {
        self.path.to_string()
    }
    fn get_regex_matcher(&self) -> String {
        self.matcher.clone()
    }
    fn matches_log(&self, raw_log: &Data<Logs>) -> bool {
        self.regex.is_match(raw_log.moniker.as_str())
    }
}

/// Represents a server component under test.
pub struct Server<'a> {
    name: String,
    path: &'a str,
    regex: Regex,
    matcher: String,
}

impl<'a> Server<'a> {
    /// Create a new instance of a server component, passing in two string references: the name of
    /// the test that created this component, and the path to the `*.cm` file describing this
    /// component in the current package:
    ///
    ///   Server::new("my_test_case", "#meta/some_server.cm");
    ///
    pub fn new(test_prefix: &str, path: &'a str) -> Self {
        let name = format!("{}_server", test_prefix);
        let matcher = format!("{}$", name);
        Self { regex: Regex::new(matcher.as_str()).unwrap(), name, path, matcher }
    }
}

impl<'a> Component for Server<'a> {
    fn get_name(&self) -> String {
        self.name.clone()
    }
    fn get_path(&self) -> String {
        self.path.to_string()
    }
    fn get_regex_matcher(&self) -> String {
        self.matcher.clone()
    }
    fn matches_log(&self, raw_log: &Data<Logs>) -> bool {
        self.regex.is_match(raw_log.moniker.as_str())
    }
}

/// This framework supports three kinds of tests:
///  - 3 components: client <-> proxy <-> server
///  - 2 components: client <-> server
///  - 1 component: standalone client
pub enum TestKind<'a> {
    StandaloneComponent { client: &'a Client<'a> },
    ClientAndServer { client: &'a Client<'a>, server: &'a Server<'a> },
    ClientProxyAndServer { client: &'a Client<'a>, proxy: &'a Proxy<'a>, server: &'a Server<'a> },
}

/// Runs a test of the specified protocol, using one of the `TestKind`s enumerated above. The
/// `input_setter` closure may be used to pass structured config values to the client, which is how
/// the test is meant to receive its inputs. The `logs_reader` closure provides the raw logs
/// collected from all child processes under test, allowing test authors to assert against the
/// logged values. Note that these are raw logs - most users will want to process the logs into
/// string form, which can be accomplished by passing the raw log vector to the `logs_to_str` helper
/// function.
pub async fn run_test<Fut>(
    protocol_name: &str,
    test_kind: TestKind<'_>,
    input_setter: impl Fn(RealmBuilder, ChildRef) -> Fut,
    logs_reader: impl Fn(Vec<Data<Logs>>),
) -> Result<(), Error>
where
    Fut: Future<Output = Result<(RealmBuilder, ChildRef), Error>>,
{
    // Subscribe to started events for child components.
    let event_stream = EventStream::open().await.unwrap();

    // Create a new empty test realm.
    let builder = RealmBuilder::new().await?;

    // Add the client to the realm, and make the client eager so that it starts automatically.
    let (client_name, client_path, client_regex_matcher) = match test_kind {
        TestKind::StandaloneComponent { client, .. }
        | TestKind::ClientAndServer { client, .. }
        | TestKind::ClientProxyAndServer { client, .. } => {
            (client.get_name(), client.get_path(), client.get_regex_matcher())
        }
    };
    let client =
        builder.add_child(client_name.clone(), client_path, ChildOptions::new().eager()).await?;

    // Apply the supplied configs to the client to allow the test runner to pass "arguments" in.
    let (builder, client) = input_setter(builder, client).await?;

    // Route the LogSink to all children so that all realm members are able to send us logs.
    let mut log_sink_route = Route::new()
        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
        .from(Ref::parent())
        .to(&client);

    // Take note of child names - we'll use these to setup logging filters further down the line.
    let mut child_names = vec![client_name];

    // Create event listeners waiting on client component startup.
    let mut start_event_matchers =
        vec![EventMatcher::ok().moniker_regex(client_regex_matcher.clone())];

    // Setup the test in each of the three supported configurations.
    if !std::matches!(test_kind, TestKind::StandaloneComponent { .. }) {
        // We have a server - add it to the realm.
        let (server_name, server_path, server_regex_matcher) = match test_kind {
            TestKind::ClientAndServer { server, .. }
            | TestKind::ClientProxyAndServer { server, .. } => {
                (server.get_name(), server.get_path(), server.get_regex_matcher())
            }
            _ => panic!("unreachable!"),
        };
        let server =
            builder.add_child(server_name.clone(), server_path, ChildOptions::new()).await?;
        child_names.push(server_name);

        // Setup logging.
        log_sink_route = log_sink_route.to(&server);

        // Add event matchers waiting on server component startup/shutdown.
        start_event_matchers.push(EventMatcher::ok().moniker_regex(server_regex_matcher));

        if std::matches!(test_kind, TestKind::ClientAndServer { .. }) {
            // If there is no proxy, connect the client to the server directly.
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(protocol_name))
                        .from(&server)
                        .to(&client),
                )
                .await?;
        } else {
            // We have a proxy - add it to the realm.
            let (proxy_name, proxy_path, proxy_regex_matcher) = match test_kind {
                TestKind::ClientProxyAndServer { proxy, .. } => {
                    (proxy.get_name(), proxy.get_path(), proxy.get_regex_matcher())
                }
                _ => panic!("unreachable!"),
            };
            let proxy =
                builder.add_child(proxy_name.clone(), proxy_path, ChildOptions::new()).await?;
            child_names.insert(1, proxy_name);

            // Setup logging.
            log_sink_route = log_sink_route.to(&proxy);

            // Add event matchers waiting on server component startup/shutdown. The proxy watcher needs to be
            // inserted prior to the server watcher, as the startup sequence for 3 process tests is
            // client then proxy then server.
            start_event_matchers.insert(1, EventMatcher::ok().moniker_regex(proxy_regex_matcher));

            // Route the capabilities from the server to the proxy.
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(protocol_name))
                        .from(&server)
                        .to(&proxy),
                )
                .await?;

            // Route the capabilities from the proxy to the client.
            builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(protocol_name))
                        .from(&proxy)
                        .to(&client),
                )
                .await?;
        }
    }

    // Route the LogSink to all children so that all realm members are able to send us logs.
    builder.add_route(log_sink_route.to_owned()).await?;

    // Create the realm instance.
    let realm_instance = builder.build().await?;

    // Verify that we get expected start and stop (clean) events.
    EventSequence::new()
        .has_subset(start_event_matchers, Ordering::Unordered)
        .has_subset(
            vec![EventMatcher::ok()
                .stop(Some(ExitStatusMatcher::Clean))
                .moniker_regex(client_regex_matcher)],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();

    // Setup the archivist link, but don't read the logs yet!
    let mut archivist_reader = ArchiveReader::new();
    child_names.iter().for_each(|child_name| {
        let moniker = format!("realm_builder:{}/{}", realm_instance.root.child_name(), child_name);
        archivist_reader.select_all_for_moniker(moniker.as_str());
    });

    // TODO(fxbug.dev/76579): We need to sleep here to make sure all child component logs get
    // drained. Once the referenced bug has been resolved, we can remove the sleep.
    thread::sleep(time::Duration::from_secs(2));

    // Clean up the realm instance, and close all open processes.
    realm_instance.destroy().await?;

    // Read all of the logs out to the test, and exit.
    logs_reader(archivist_reader.snapshot::<Logs>().await?);
    Ok(())
}

/// Takes a vector of raw logs, and returns an iterator over the string representations of said
/// logs. The second argument allows for optional filtering by component. For example, if one only
/// wants to see server logs, the invocation may look like:
///
///   logs_to_str(&raw_logs, Some(vec![&server_component_definition]));
///
pub fn logs_to_str<'a>(
    raw_logs: &'a Vec<Data<Logs>>,
    maybe_filter_by_process: Option<Vec<&'a dyn Component>>,
) -> impl Iterator<Item = &'a str> + 'a {
    logs_to_str_filtered(raw_logs, maybe_filter_by_process, |_raw_log| true)
}

/// Same as |logs_to_str|, except an additional filtering function may be used to trim arbitrary
/// logs. This is particularly useful if one or more languages produces logs that we don't want to
/// include in the final, common output to be compared across language implementations.
pub fn logs_to_str_filtered<'a>(
    raw_logs: &'a Vec<Data<Logs>>,
    maybe_filter_by_process: Option<Vec<&'a dyn Component>>,
    filter_by_log: impl FnMut(&&Data<Logs>) -> bool + 'a,
) -> impl Iterator<Item = &'a str> + 'a {
    raw_logs
        .iter()
        .filter(move |raw_log| match maybe_filter_by_process {
            Some(ref process_list) => {
                for process in process_list.iter() {
                    if process.matches_log(*raw_log) {
                        return true;
                    }
                }
                return false;
            }
            None => true,
        })
        .filter(filter_by_log)
        .map(|raw_log| {
            raw_log.payload_message().expect("payload not found").properties[0]
                .string()
                .expect("message is not string")
        })
}

/// Takes the logs for a single component and compares them to the appropriate golden file. The path
/// of the file is expected to match the template `/pkg/data/goldens/{COMPONENT_NAME}.log.golden`.
/// The {COMPONENT_NAME} is itself generally a template of the form `{TEST_NAME}_{COMPONENT_ROLE}`.
/// Thus, for the three-component `test_foo_bar`, we expect the following golden logs to exist:
///
///   /pkg/data/goldens/test_foo_bar_client.log.golden
///   /pkg/data/goldens/test_foo_bar_proxy.log.golden
///   /pkg/data/goldens/test_foo_bar_server.log.golden
///
pub fn assert_logs_eq_to_golden<'a>(raw_logs: &'a Vec<Data<Logs>>, comp: &'a dyn Component) {
    assert_filtered_logs_eq_to_golden(raw_logs, comp, |_raw_log| true)
}

/// Same as |assert_logs_eq_to_golden|, except an additional filtering function may be used to trim
/// arbitrary logs. This is particularly useful if one or more languages produces logs that we don't
/// want to include in the final, common output to be compared across language implementations.
pub fn assert_filtered_logs_eq_to_golden<'a>(
    raw_logs: &'a Vec<Data<Logs>>,
    comp: &'a dyn Component,
    filter_by_log: impl FnMut(&&Data<Logs>) -> bool + 'a,
) {
    // Extract the golden log data.
    let golden_path = format!("/pkg/data/goldens/{}.log.golden", comp.get_name());
    let golden_file = std::fs::read_to_string(golden_path.clone())
        .with_context(|| format!("Failed to load {golden_path}"))
        .unwrap();
    let golden_logs = golden_file.as_str().trim();

    // Compare it to the actual components actual logs, asserting if there is a mismatch.
    let logs = logs_to_str_filtered(&raw_logs, Some(vec![comp]), filter_by_log)
        .collect::<Vec<&str>>()
        .join("\n");
    if logs != golden_logs.trim() {
        print!(
            "

Logs golden mismatch in '{}' ({})
Please copy the output between the '===' bounds into the golden file at {} in the fuchsia.git tree
====================================================================================================
{}
====================================================================================================


",
            comp.get_name(),
            comp.get_path(),
            golden_path,
            logs
        );
    }
    assert_eq!(logs, golden_logs)
}
