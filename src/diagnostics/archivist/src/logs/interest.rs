// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    fidl_fuchsia_diagnostics::{Interest, StringSelector},
    fidl_fuchsia_logger::{LogInterestSelector, LogSinkControlHandle},
    fidl_fuchsia_sys_internal::SourceIdentity,
    log::warn,
    std::collections::HashMap,
    std::convert::TryFrom,
    std::sync::{Arc, Weak},
};

/// Type used to identify the intended component target specified by an
/// interest selector.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
struct Component {
    name: String,
}

impl<'s> TryFrom<&'s Arc<SourceIdentity>> for Component {
    type Error = &'static str;

    fn try_from(source: &'s Arc<SourceIdentity>) -> Result<Self, Self::Error> {
        match &source.component_name {
            Some(n) => Ok(Component { name: n.to_string() }),
            None => Err("Failed to convert SourceIdentity to Component - missing name."),
        }
    }
}

/// Interest dispatcher to handle the communication of `Interest` changes
/// to their intended `LogSink` client listeners.
#[derive(Debug, Default)]
pub struct InterestDispatcher {
    /// Map of LogSinkControlHandles associated with a given component name
    /// (possibly multiple instances). Any provided selector specification
    /// will apply universally to all matching instances.
    interest_listeners: HashMap<Component, Vec<Weak<LogSinkControlHandle>>>,
    /// List of LogInterestSelectors that couple the specified interest with
    /// the intended target component.
    selectors: Vec<LogInterestSelector>,
}

impl InterestDispatcher {
    /// Add a LogSinkControlHandle corresponding to the given component
    /// (source) as an interest listener. If one or more control handles
    /// are already associated with this component (i.e. in the case of
    /// multiple instances) this handle will be apended to the list.
    pub fn add_interest_listener(
        &mut self,
        source: &Arc<SourceIdentity>,
        handle: Weak<LogSinkControlHandle>,
    ) {
        let component = match Component::try_from(source) {
            Ok(c) => c,
            _ => {
                warn!("Failed to add interest listener - no component for source");
                return;
            }
        };

        let component_listeners =
            self.interest_listeners.entry(component.clone()).or_insert(vec![]);
        component_listeners.push(handle);

        // check to see if we have a selector specified for this component and
        // if so, send the interest notification.
        let mut interest: Option<Interest> = None;
        self.selectors.iter().for_each(|s| {
            if let Some(segments) = &s.selector.moniker_segments {
                segments.iter().for_each(|segment| {
                    match segment {
                        StringSelector::StringPattern(name) => {
                            // TODO(fxbug.dev/54198): Interest listener matching based
                            // on strict name comparison look at using moniker
                            // heuristics via selectors API.
                            if name == &component.name {
                                interest = Some(Interest { min_severity: s.interest.min_severity });
                            }
                        }
                        _ => warn!("Unexpected component selector moniker segment {:?}", segment),
                    };
                });
            };
        });
        if let Some(i) = interest {
            self.notify_listeners_for_component(component, |l| {
                let _ = l.send_on_register_interest(Interest { min_severity: i.min_severity });
            });
        }
    }

    /// Update the set of selectors that archivist uses to control the log
    /// levels associated with any active LogSink clients.
    pub async fn update_selectors<'a>(&mut self, selectors: Vec<LogInterestSelector>) {
        if !self.selectors.is_empty() {
            warn!("Overriding existing selectors: {:?} with {:?}", &self.selectors, &selectors);
        }
        selectors.iter().for_each(|s| {
            if let Some(segments) = &s.selector.moniker_segments {
                segments.iter().for_each(|segment| {
                    match segment {
                        // string_pattern results from selectors created with
                        // selectors::parse_component_selector. Potentially
                        // handle additional cases (exact_match?) if needs be.
                        StringSelector::StringPattern(name) => {
                            self.notify_listeners_for_component(
                                Component { name: name.to_string() },
                                |l| {
                                    let _ = l.send_on_register_interest(Interest {
                                        min_severity: s.interest.min_severity,
                                    });
                                },
                            );
                        }
                        _ => warn!("Unexpected component selector moniker segment {:?}", segment),
                    };
                });
            };
        });
        self.selectors = selectors;
    }

    fn notify_listeners_for_component<F>(&mut self, component: Component, mut f: F)
    where
        F: FnMut(&LogSinkControlHandle) -> (),
    {
        if let Some(component_listeners) = self.interest_listeners.get_mut(&component) {
            component_listeners.retain(|listener| match listener.upgrade() {
                Some(listener_) => {
                    f(&listener_);
                    true
                }
                None => false,
            });
        } else {
            warn!(
                "Failed to notify interest listener - unable to find LogSinkControlHandle for {:?}",
                component
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_request_stream, RequestStream};
    use fuchsia_async as fasync;
    use std::sync::Arc;

    #[fasync::run_singlethreaded(test)]

    async fn interest_listeners() {
        let mut source_id = SourceIdentity::empty();
        source_id.component_name = Some("foo.cmx".to_string());

        let source_arc = Arc::new(source_id);
        let component = Component::try_from(&source_arc);

        let mut dispatcher = InterestDispatcher::default();
        let (_logsink_client, log_request_stream) =
            create_request_stream::<fidl_fuchsia_logger::LogSinkMarker>()
                .expect("failed to create LogSink proxy");
        let handle = log_request_stream.control_handle();

        // add the interest listener (component_id + weak handle)
        dispatcher.add_interest_listener(&source_arc, Arc::downgrade(&Arc::new(handle)));

        // check listener addition successful
        assert_eq!(component.is_ok(), true);
        if let Ok(c) = component {
            let preclose_listeners = &dispatcher.interest_listeners.get(&c);
            assert_eq!(preclose_listeners.is_some(), true);
            if let Some(l) = preclose_listeners {
                assert_eq!(l.len(), 1);
                if let Some(handle) = l[0].upgrade() {
                    // close the channel
                    handle.shutdown();
                }
                // notify checks the channel/retains
                dispatcher.notify_listeners_for_component(c.clone(), |listener| {
                    let _ = listener.send_on_register_interest(Interest { min_severity: None });
                });

                // check that the listener is no longer active.
                let postclose_listeners = &dispatcher.interest_listeners.get(&c);
                assert_eq!(postclose_listeners.is_some(), true);
                match postclose_listeners {
                    Some(l) => assert_eq!(l.len(), 0),
                    None => {}
                }
            }
        }
    }
}
