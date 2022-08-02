// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::{error::HangingGetServerError, server as hanging_get},
    derivative::Derivative,
    fidl::endpoints::{ControlHandle, RequestStream},
    fidl_fuchsia_element as felement, fidl_fuchsia_mem as fmem, fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
    tracing::error,
};

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
struct Key {
    namespace: String,
    value: String,
}

impl Key {
    fn from_fidl_key(fidl_key: &felement::AnnotationKey) -> Key {
        Key { namespace: fidl_key.namespace.to_string(), value: fidl_key.value.to_string() }
    }

    fn as_fidl_key(&self) -> felement::AnnotationKey {
        felement::AnnotationKey {
            namespace: self.namespace.to_string(),
            value: self.value.to_string(),
        }
    }
}

#[derive(Debug, thiserror::Error, PartialEq)]
pub enum AnnotationError {
    #[error("failed to update annotations")]
    Update(felement::UpdateAnnotationsError),
    #[error("failed to get annotations")]
    Get(felement::GetAnnotationsError),
    #[error("failed to watch for annotation updates")]
    Watch(#[from] HangingGetServerError),
}

// TODO(fxbug.dev/93948): clarify error types/names
enum BufferIOError {
    BufferReadFailed,
}

// This only converts into the `AnnotationError::Get` variant and must be modified if another `AnnotationError` needs to be supported.
impl Into<AnnotationError> for BufferIOError {
    fn into(self) -> AnnotationError {
        match self {
            BufferIOError::BufferReadFailed => {
                AnnotationError::Get(felement::GetAnnotationsError::BufferReadFailed)
            }
        }
    }
}

impl Into<felement::WatchAnnotationsError> for BufferIOError {
    fn into(self) -> felement::WatchAnnotationsError {
        match self {
            BufferIOError::BufferReadFailed => felement::WatchAnnotationsError::BufferReadFailed,
        }
    }
}

// A `WatchResponder` is used to send clients of either the element controller or annotation
// controller a set of updated annotations.
pub enum WatchResponder {
    ElementController(felement::ControllerWatchAnnotationsResponder),
    AnnotationController(felement::AnnotationControllerWatchAnnotationsResponder),
}

impl WatchResponder {
    fn send(
        self,
        response: &mut Result<Vec<felement::Annotation>, felement::WatchAnnotationsError>,
    ) -> bool {
        // Ignore if the receiver half has dropped
        let _ = match self {
            WatchResponder::ElementController(e) => e.send(response),
            WatchResponder::AnnotationController(a) => a.send(response),
        };
        true
    }
}

// The function that notifies a `WatchResponder` the result of an annotation update.
type WatchResponderNotifyFn = Box<
    dyn Fn(
            &Result<Vec<felement::Annotation>, felement::WatchAnnotationsError>,
            WatchResponder,
        ) -> bool
        + Send,
>;
// Creates new subscribers and publishers to watch and notify for annotation updates.
type WatchHangingGet = hanging_get::HangingGet<
    Result<Vec<felement::Annotation>, felement::WatchAnnotationsError>,
    WatchResponder,
    WatchResponderNotifyFn,
>;

// Publishes updates to the annotations to hanging_get clients
type WatchPublisher = async_utils::hanging_get::server::Publisher<
    Result<Vec<felement::Annotation>, felement::WatchAnnotationsError>,
    WatchResponder,
    WatchResponderNotifyFn,
>;

// Subscribes to a notification for the next annotation update and hands the result to its
// registered `WatchResponder`. This wrapper is needed in order to provide an interface for
// `watch_annotations` to register a `WatchResponder`.
pub struct WatchSubscriber(
    hanging_get::Subscriber<
        Result<Vec<felement::Annotation>, felement::WatchAnnotationsError>,
        WatchResponder,
        WatchResponderNotifyFn,
    >,
);

impl WatchSubscriber {
    pub fn watch_annotations(&mut self, responder: WatchResponder) -> Result<(), AnnotationError> {
        self.0.register(responder).map_err(|e| AnnotationError::Watch(e))
    }
}

/// Maximum number of annotations that can be stored in [`AnnotationHolder`].
///
/// This is the same as the max number per element as each element should have
/// its own `AnnotationHolder`
pub const MAX_ANNOTATIONS: usize = felement::MAX_ANNOTATIONS_PER_ELEMENT as usize;

fn clone_annotation_value(
    value: &felement::AnnotationValue,
) -> Result<felement::AnnotationValue, BufferIOError> {
    Ok(match &*value {
        felement::AnnotationValue::Text(content) => {
            felement::AnnotationValue::Text(content.to_string())
        }
        felement::AnnotationValue::Buffer(content) => {
            let mut bytes = Vec::<u8>::with_capacity(content.size as usize);
            let vmo = fidl::Vmo::create(content.size).unwrap();
            content.vmo.read(&mut bytes[..], 0).map_err(|_| BufferIOError::BufferReadFailed)?;
            vmo.write(&bytes[..], 0).map_err(|_| BufferIOError::BufferReadFailed)?;
            felement::AnnotationValue::Buffer(fmem::Buffer { vmo, size: content.size })
        }
    })
}

/// Try to construct a vec of `Annotation`s from each key/value pair in the HashMap
fn annotations_to_vec(
    annotations: &HashMap<Key, felement::AnnotationValue>,
) -> Result<Vec<felement::Annotation>, BufferIOError> {
    let mut result = vec![];
    for (key, value) in annotations {
        let annotation =
            felement::Annotation { key: key.as_fidl_key(), value: clone_annotation_value(value)? };
        result.push(annotation);
    }
    Ok(result)
}

/// Helper for `AnnotationHolder::watch_annotations`, if you want to use it for something else,
/// verify that the error return types are suitable.
fn clone_annotation_vec(
    annotations: &Vec<felement::Annotation>,
) -> Result<Vec<felement::Annotation>, felement::WatchAnnotationsError> {
    let mut result = vec![];
    for felement::Annotation { key, value } in annotations {
        result.push(felement::Annotation {
            key: key.clone(),
            value: clone_annotation_value(&value).map_err(|e| e.into())?,
        });
    }
    Ok(result)
}

#[derive(Derivative)]
#[derivative(Debug)]
pub struct AnnotationHolder {
    annotations: HashMap<Key, felement::AnnotationValue>,
    #[derivative(Debug = "ignore")]
    watch_hanging_get: WatchHangingGet,
    #[derivative(Debug = "ignore")]
    watch_publisher: WatchPublisher,
}

impl AnnotationHolder {
    pub fn new() -> AnnotationHolder {
        let notify_fn: WatchResponderNotifyFn = Box::new(|result, responder| {
            // Create a copy of the result that can be sent to the responder.
            let mut result = match result {
                Ok(v) => clone_annotation_vec(&v),
                Err(e) => Err(*e),
            };
            responder.send(&mut result)
        });

        // Create a hanging get controller and initialize its copy of the annotations to an empty vector.
        let watch_hanging_get = WatchHangingGet::new(Ok(Vec::new()), notify_fn);
        let watch_publisher = watch_hanging_get.new_publisher();
        AnnotationHolder { annotations: HashMap::new(), watch_hanging_get, watch_publisher }
    }

    pub fn new_watch_subscriber(&mut self) -> WatchSubscriber {
        let s = self.watch_hanging_get.new_subscriber();
        WatchSubscriber(s)
    }

    fn notify_watch_subscribers(&self) {
        self.watch_publisher.set(annotations_to_vec(&self.annotations).map_err(Into::into));
    }

    // Update (add/modify/delete) annotations as specified by fuchsia.element.AnnotationController.UpdateAnnotations()
    pub fn update_annotations(
        &mut self,
        mut annotations_to_set: Vec<felement::Annotation>,
        annotations_to_delete: Vec<felement::AnnotationKey>,
    ) -> Result<(), AnnotationError> {
        // Gather the keys that will be updated.
        let mut keys_to_set = HashSet::new();
        for annotation in annotations_to_set.iter() {
            if annotation.key.namespace.len() == 0 {
                // Empty namespace is illegal.
                return Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs));
            }
            keys_to_set.insert(Key::from_fidl_key(&annotation.key));
        }

        // Gather keys that will be deleted.
        let mut keys_to_delete = HashSet::new();
        for key in annotations_to_delete.iter() {
            if key.namespace.len() == 0 {
                // Empty namespace is illegal.
                return Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs));
            }
            keys_to_delete.insert(Key::from_fidl_key(&key));
        }

        // Detect illegality: setting multiple annotations with the same key.
        if keys_to_set.len() != annotations_to_set.len() {
            return Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs));
        }

        // Detect illegality: both setting and deleting annotations with the same key.
        if !keys_to_set.is_disjoint(&keys_to_delete) {
            return Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs));
        }

        // Detect illegality: maximum number of annotations would be exceeded.
        // To compute the number of annotations that will exists after applying this operation:
        //   - start with the existing number of annotations
        //   - add one for every annotation-to-set whose key doesn't already exist
        //   - subtract one for every annotation-to-delete whose key does already exist
        let mut count = self.annotations.len();
        for k in keys_to_set {
            if !self.annotations.contains_key(&k) {
                count += 1
            }
        }
        for k in keys_to_delete {
            if self.annotations.contains_key(&k) {
                count -= 1
            }
        }
        if count > MAX_ANNOTATIONS {
            return Err(AnnotationError::Update(
                felement::UpdateAnnotationsError::TooManyAnnotations,
            ));
        }

        // Everything looks legal, so we can go ahead and mutate our map.
        for annotation in annotations_to_set.drain(..) {
            let key = Key::from_fidl_key(&annotation.key);
            self.annotations.insert(key, annotation.value);
        }
        for key in annotations_to_delete.iter() {
            let key = Key::from_fidl_key(key);
            self.annotations.remove(&key);
        }

        self.notify_watch_subscribers();

        Ok(())
    }

    // Get the current set of annotations, as specified by fuchsia.element.AnnotationController.GetAnnotations()
    pub fn get_annotations(&self) -> Result<Vec<felement::Annotation>, AnnotationError> {
        annotations_to_vec(&self.annotations).map_err(|e| e.into())
    }
}

// Convenient function to handle an AnnotationControllerRequestStream.
pub async fn handle_annotation_controller_stream(
    annotations: Arc<Mutex<AnnotationHolder>>,
    mut stream: felement::AnnotationControllerRequestStream,
) {
    let mut watch_subscriber = annotations.lock().await.new_watch_subscriber();
    while let Ok(Some(request)) = stream.try_next().await {
        if let Err(e) = handle_annotation_controller_request(
            &mut *annotations.lock().await,
            &mut watch_subscriber,
            request,
        ) {
            error!("AnnotationControllerRequest error: {}. Dropping connection", e);
            stream.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
            return;
        }
    }
}

// Convenient function to handle an AnnotationControllerRequest.
fn handle_annotation_controller_request(
    annotations: &mut AnnotationHolder,
    watch_subscriber: &mut WatchSubscriber,
    request: felement::AnnotationControllerRequest,
) -> Result<(), AnnotationError> {
    match request {
        felement::AnnotationControllerRequest::UpdateAnnotations {
            annotations_to_set,
            annotations_to_delete,
            responder,
        } => {
            let result = annotations.update_annotations(annotations_to_set, annotations_to_delete);
            match result {
                Ok(()) => responder.send(&mut Ok(())),
                Err(AnnotationError::Update(e)) => responder.send(&mut Err(e)),
                Err(_) => unreachable!(),
            }
            .ok();
        }
        felement::AnnotationControllerRequest::GetAnnotations { responder } => {
            let result = annotations.get_annotations();
            match result {
                Ok(annotation_vec) => responder.send(&mut Ok(annotation_vec)),
                Err(AnnotationError::Get(e)) => responder.send(&mut Err(e)),
                Err(_) => unreachable!(),
            }
            .ok();
        }
        felement::AnnotationControllerRequest::WatchAnnotations { responder } => {
            // An error is returned if there is already a `WatchAnnotations` request pending for the client. Since the responder gets dropped (TODO(fxbug.dev/94602)), the connection will be closed to indicate unexpected client behavior.
            watch_subscriber.watch_annotations(WatchResponder::AnnotationController(responder))?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        crate::annotation::{
            handle_annotation_controller_request, AnnotationError, AnnotationHolder,
            WatchSubscriber, MAX_ANNOTATIONS,
        },
        assert_matches::assert_matches,
        async_utils::hanging_get::error::HangingGetServerError,
        fidl::endpoints::{
            create_proxy_and_stream, spawn_stream_handler, ControlHandle, RequestStream,
        },
        fidl_fuchsia_element as felement, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{stream::FusedStream, StreamExt, TryStreamExt},
        std::{cmp::Ordering, sync::Arc, sync::Mutex},
    };

    fn make_annotation_key(namespace: &str, value: &str) -> felement::AnnotationKey {
        felement::AnnotationKey { namespace: namespace.to_string(), value: value.to_string() }
    }

    fn make_annotation_text(text: &str) -> felement::AnnotationValue {
        felement::AnnotationValue::Text(text.to_string())
    }

    fn compare_annotation_keys(
        a: &felement::AnnotationKey,
        b: &felement::AnnotationKey,
    ) -> Ordering {
        match a.namespace.cmp(&b.namespace) {
            Ordering::Less => Ordering::Less,
            Ordering::Greater => Ordering::Greater,
            Ordering::Equal => a.value.cmp(&b.value),
        }
    }

    fn assert_text_annotation_matches(
        annotation: &felement::Annotation,
        namespace: &str,
        key: &str,
        text: &str,
    ) {
        assert_eq!(annotation.key.namespace, namespace);
        assert_eq!(annotation.key.value, key);
        if let felement::AnnotationValue::Text(content) = &annotation.value {
            assert_eq!(content, text);
        } else {
            panic!("annotation value is not Text");
        }
    }

    // A helper so that the stream and request result can be tested by the caller.
    async fn handle_one_request(
        holder: Arc<Mutex<AnnotationHolder>>,
        watch_subscriber: &mut WatchSubscriber,
        stream: &mut felement::AnnotationControllerRequestStream,
    ) -> Result<(), AnnotationError> {
        if let Some(request) = stream.try_next().await.unwrap() {
            let result = handle_annotation_controller_request(
                &mut holder.lock().unwrap(),
                watch_subscriber,
                request,
            );
            if result.is_err() {
                stream.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
            }
            return result;
        }
        Ok(())
    }

    // An alternative to `spawn_stream_handler` that uses the same `watch_subscriber` for every request.
    fn spawn_annotation_controller_handler(
        holder: Arc<Mutex<AnnotationHolder>>,
    ) -> felement::AnnotationControllerProxy {
        let mut watch_subscriber = holder.lock().unwrap().new_watch_subscriber();
        let (proxy, mut stream) =
            create_proxy_and_stream::<felement::AnnotationControllerMarker>().unwrap();
        fasync::Task::spawn(async move {
            loop {
                let _ =
                    handle_one_request(holder.clone(), &mut watch_subscriber, &mut stream).await;
            }
        })
        .detach();
        proxy
    }

    #[test]
    fn get_empty_annotations() -> Result<(), anyhow::Error> {
        let holder = AnnotationHolder::new();
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 0);
        Ok(())
    }

    #[test]
    // Adds and retrieves a single annotation, verifying that namspace/key/value all match.
    fn get_single_annotation() -> Result<(), anyhow::Error> {
        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";

        let mut holder = AnnotationHolder::new();
        holder.update_annotations(
            vec![felement::Annotation {
                key: make_annotation_key(NAMESPACE, ID1),
                value: make_annotation_text("annotation value"),
            }],
            vec![],
        )?;

        let annotations = holder.get_annotations()?;
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID1, "annotation value");

        Ok(())
    }

    #[test]
    // Adds 3 annotations, then in a second call to update_annotations():
    // - removes one
    // - modifies one
    // - adds a new one
    fn update_some_annotations() -> Result<(), anyhow::Error> {
        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";
        const ID2: &str = "id2";
        const ID3: &str = "id3";
        const ID4: &str = "id4";

        let mut holder = AnnotationHolder::new();

        // Add initial annotations.
        holder.update_annotations(
            vec![
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID1),
                    value: make_annotation_text("initial value 1"),
                },
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID2),
                    value: make_annotation_text("initial value 2"),
                },
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID3),
                    value: make_annotation_text("initial value 3"),
                },
            ],
            vec![],
        )?;

        // Delete one annotation, modify another, add a third.
        holder.update_annotations(
            vec![
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID3),
                    value: make_annotation_text("updated value 3"),
                },
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID4),
                    value: make_annotation_text("new value 4"),
                },
            ],
            vec![make_annotation_key(NAMESPACE, ID2)],
        )?;

        let mut annotations = holder.get_annotations()?;
        annotations.sort_by(|a, b| compare_annotation_keys(&a.key, &b.key));
        assert_eq!(annotations.len(), 3);
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID1, "initial value 1");
        assert_text_annotation_matches(&annotations[1], NAMESPACE, ID3, "updated value 3");
        assert_text_annotation_matches(&annotations[2], NAMESPACE, ID4, "new value 4");

        Ok(())
    }

    #[fuchsia::test]
    // Test `watch_annotations` returns empty annotations if they have not been set yet.
    async fn watch_annotations_no_annotations() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));
        let proxy = spawn_annotation_controller_handler(holder.clone());
        let annotations = proxy.watch_annotations().await?.unwrap();
        assert_eq!(annotations.len(), 0);
        Ok(())
    }

    #[fuchsia::test]
    // Test `watch_annotations` returns current annotations on client's first request.
    async fn watch_annotations_immediate_return() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));
        let proxy = spawn_annotation_controller_handler(holder.clone());
        let _ = proxy.update_annotations(
            &mut vec![felement::Annotation {
                key: make_annotation_key("NAMESPACE", "id1"),
                value: make_annotation_text("original1"),
            }]
            .iter_mut(),
            &mut vec![].iter_mut(),
        );
        let annotations = proxy.watch_annotations().await?.unwrap();
        assert_eq!(annotations.len(), 1);
        Ok(())
    }

    #[fuchsia::test]
    // Test `watch_annotations` waits for an update to annotations before returning to the client.
    async fn watch_annotations_wait_for_update() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));
        let proxy = spawn_annotation_controller_handler(holder.clone());

        // Make the initial request, so that the next one will hang.
        let annotations = proxy.watch_annotations().await?.unwrap();
        assert_eq!(annotations.len(), 0);

        let annotations = proxy.watch_annotations();
        let update = fasync::Task::spawn(async move {
            let _ = proxy.update_annotations(
                &mut vec![felement::Annotation {
                    key: make_annotation_key("NAMESPACE", "id1"),
                    value: make_annotation_text("original1"),
                }]
                .iter_mut(),
                &mut vec![].iter_mut(),
            );
        });

        let (annotations, _) = futures::join!(annotations, update);
        assert_eq!(annotations?.unwrap().len(), 1);
        Ok(())
    }

    // #[fuchsia::test]
    // Test `watch_annotations` will notify multiple hanging clients of an update
    #[fuchsia::test]
    async fn watch_annotations_multiple_clients() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));
        let client1_proxy = spawn_annotation_controller_handler(holder.clone());
        let client2_proxy = spawn_annotation_controller_handler(holder.clone());

        // Make initial requests, so that the next ones will hang.
        let client1_annotations = client1_proxy.watch_annotations().await?.unwrap();
        assert_eq!(client1_annotations.len(), 0);
        let client2_annotations = client2_proxy.watch_annotations().await?.unwrap();
        assert_eq!(client2_annotations.len(), 0);

        let client1_annotations = client1_proxy.watch_annotations();
        let client2_annotations = client2_proxy.watch_annotations();
        let update = fasync::Task::spawn(async move {
            let _ = client1_proxy.update_annotations(
                &mut vec![felement::Annotation {
                    key: make_annotation_key("NAMESPACE", "id1"),
                    value: make_annotation_text("original1"),
                }]
                .iter_mut(),
                &mut vec![].iter_mut(),
            );
        });

        let (client1_annotations, client2_annotations, _) =
            futures::join!(client1_annotations, client2_annotations, update);
        assert_eq!(client1_annotations?.unwrap().len(), 1);
        assert_eq!(client2_annotations?.unwrap().len(), 1);
        Ok(())
    }

    #[fuchsia::test]
    //  Test `watch_annotations` will shutdown the client connection if a second
    //  `watch_annotations` request is made before the previous request completes for the same client.
    async fn watch_annotations_duplicate_requests() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));
        let mut watch_subscriber = holder.lock().unwrap().new_watch_subscriber();
        let (proxy, mut stream) =
            create_proxy_and_stream::<felement::AnnotationControllerMarker>().unwrap();

        // Make the initial request, so that the next one will hang.
        let annotations = proxy.watch_annotations();
        let result = handle_one_request(holder.clone(), &mut watch_subscriber, &mut stream);
        let (_, result) = futures::join!(annotations, result);
        assert!(result.is_ok());

        // Make a request to wait for updates
        let proxy_clone = proxy.clone();
        fasync::Task::spawn(async move {
            let _ = proxy_clone.watch_annotations().await;
        })
        .detach();
        let result = handle_one_request(holder.clone(), &mut watch_subscriber, &mut stream).await;
        assert!(result.is_ok());

        // A second request made before the previous completes errors and the connection gets shutdown
        let proxy_clone = proxy.clone();
        fasync::Task::spawn(async move {
            let _ = proxy_clone.watch_annotations().await;
        })
        .detach();
        let result = handle_one_request(holder.clone(), &mut watch_subscriber, &mut stream).await;
        assert_eq!(result, Err(AnnotationError::Watch(HangingGetServerError::MultipleObservers)));
        assert!(stream.next().await.is_none());
        assert!(stream.is_terminated());
        Ok(())
    }

    #[test]
    // Verify that there is no collision between annotations with the same key, but different namespaces.
    fn no_namespace_collisions() -> Result<(), anyhow::Error> {
        let mut holder = AnnotationHolder::new();

        // Add 2 annotations in different namespaces, with the same key.
        holder.update_annotations(
            vec![
                felement::Annotation {
                    key: make_annotation_key("namespace1", "id"),
                    value: make_annotation_text("_"),
                },
                felement::Annotation {
                    key: make_annotation_key("namespace2", "id"),
                    value: make_annotation_text("_"),
                },
            ],
            vec![],
        )?;
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 2);

        // Remove one, assert that the proper one is removed.
        holder.update_annotations(vec![], vec![make_annotation_key("namespace1", "id")])?;
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);
        assert_text_annotation_matches(&annotations[0], "namespace2", "id", "_");

        Ok(())
    }

    #[test]
    fn illegal_empty_namespaces() -> Result<(), anyhow::Error> {
        let mut holder = AnnotationHolder::new();

        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";
        const ID2: &str = "id2";

        // Pre-populate annotations, so we can verify that illegal actions cause no changes.
        holder.update_annotations(
            vec![felement::Annotation {
                key: make_annotation_key(NAMESPACE, ID1),
                value: make_annotation_text("original"),
            }],
            vec![],
        )?;
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);

        // ID1 won't be removed due to illegal delete.
        // Verify the result was an error, and the annotations are unmodified.
        assert_matches!(
            holder.update_annotations(
                vec![],
                vec![make_annotation_key(NAMESPACE, ID1), make_annotation_key("", ID1)],
            ),
            Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs))
        );
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID1, "original");

        // Nothing will be changed due to illegal add.
        assert_matches!(
            holder.update_annotations(
                vec![
                    felement::Annotation {
                        key: make_annotation_key(NAMESPACE, ID1),
                        value: make_annotation_text("updated"),
                    },
                    felement::Annotation {
                        key: make_annotation_key(NAMESPACE, ID2),
                        value: make_annotation_text("updated"),
                    },
                    felement::Annotation {
                        key: make_annotation_key("", ID1),
                        value: make_annotation_text("updated"),
                    },
                ],
                vec![],
            ),
            Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs))
        );
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID1, "original");

        Ok(())
    }

    #[test]
    fn too_many_annotations() -> Result<(), anyhow::Error> {
        let mut holder = AnnotationHolder::new();

        const NAMESPACE: &str = "HARDCODED_NAMESPACE";

        // Add annotations, which will succeed because it is exactly the max num allowed.
        let initial_annotations = (1..=MAX_ANNOTATIONS)
            .map(|i| felement::Annotation {
                key: felement::AnnotationKey {
                    namespace: NAMESPACE.to_string(),
                    value: format!("id{}", i),
                },
                value: felement::AnnotationValue::Text(format!("value{}", i)),
            })
            .collect();
        holder.update_annotations(initial_annotations, vec![])?;
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), MAX_ANNOTATIONS);

        // Try to add one more, which will fail because that's more than the max.
        assert_matches!(
            holder.update_annotations(
                vec![felement::Annotation {
                    key: make_annotation_key(NAMESPACE, "foo"),
                    value: make_annotation_text("should not be added"),
                }],
                vec![],
            ),
            Err(AnnotationError::Update(felement::UpdateAnnotationsError::TooManyAnnotations))
        );
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), MAX_ANNOTATIONS);

        Ok(())
    }

    #[test]
    fn set_delete_same_key() -> Result<(), anyhow::Error> {
        let mut holder = AnnotationHolder::new();

        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";

        // Pre-populate annotations, so we can verify that illegal actions cause no changes.
        holder.update_annotations(
            vec![felement::Annotation {
                key: make_annotation_key(NAMESPACE, ID1),
                value: make_annotation_text("original"),
            }],
            vec![],
        )?;
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);

        // Setting and deleting an annotation in the same operation with the same key is illegal.
        // Verify the result was an error, and the annotations are unmodified.
        assert_matches!(
            holder.update_annotations(
                vec![felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID1),
                    value: make_annotation_text("should not be updated"),
                },],
                vec![make_annotation_key(NAMESPACE, ID1)],
            ),
            Err(AnnotationError::Update(felement::UpdateAnnotationsError::InvalidArgs))
        );
        let annotations = holder.get_annotations()?;
        assert_eq!(annotations.len(), 1);
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID1, "original");

        Ok(())
    }

    #[fuchsia::test]
    async fn test_handle_annotation_controller_request() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));

        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";
        const ID2: &str = "id2";

        let proxy: felement::AnnotationControllerProxy = spawn_stream_handler(move |req| {
            let holder = holder.clone();
            let mut watch_subscriber = holder.lock().unwrap().new_watch_subscriber();
            async move {
                handle_annotation_controller_request(
                    &mut holder.lock().unwrap(),
                    &mut watch_subscriber,
                    req,
                )
                .unwrap()
            }
        })
        .unwrap();

        let _ = proxy.update_annotations(
            &mut vec![
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID1),
                    value: make_annotation_text("original1"),
                },
                felement::Annotation {
                    key: make_annotation_key(NAMESPACE, ID2),
                    value: make_annotation_text("original2"),
                },
            ]
            .iter_mut(),
            &mut vec![].iter_mut(),
        );

        let annotations = proxy.get_annotations().await?.unwrap();
        assert_eq!(annotations.len(), 2);

        let _ = proxy.update_annotations(
            &mut vec![].iter_mut(),
            &mut vec![make_annotation_key(NAMESPACE, ID1)].iter_mut(),
        );

        let annotations = proxy.get_annotations().await?.unwrap();
        assert_eq!(annotations.len(), 1);
        assert_text_annotation_matches(&annotations[0], NAMESPACE, ID2, "original2");

        Ok(())
    }
}
