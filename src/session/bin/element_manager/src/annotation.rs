// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_element as felement, fidl_fuchsia_mem as fmem,
    futures::{lock::Mutex, TryStreamExt},
    std::collections::HashMap,
    std::{collections::HashSet, sync::Arc},
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

#[derive(Debug, thiserror::Error)]
pub enum AnnotationError {
    #[error("failed to update annotations")]
    Update(felement::UpdateAnnotationsError),
    #[error("failed to get annotations")]
    Get(felement::GetAnnotationsError),
}

#[derive(Debug)]
pub struct AnnotationHolder {
    annotations: HashMap<Key, felement::AnnotationValue>,
}

/// Maximum number of annotations that can be stored in [`AnnotationHolder`].
///
/// This is the same as the max number per element as each element should have
/// its own `AnnotationHolder`
pub const MAX_ANNOTATIONS: usize = felement::MAX_ANNOTATIONS_PER_ELEMENT as usize;

// Helper for AnnotationHolder::get_annotations().  If you want to use it for something else, verify
// that the error return types are suitable.
fn clone_annotation_value(
    value: &felement::AnnotationValue,
) -> Result<felement::AnnotationValue, AnnotationError> {
    Ok(match &*value {
        felement::AnnotationValue::Text(content) => {
            felement::AnnotationValue::Text(content.to_string())
        }
        felement::AnnotationValue::Buffer(content) => {
            let mut bytes = Vec::<u8>::with_capacity(content.size as usize);
            let vmo = fidl::Vmo::create(content.size).unwrap();
            content.vmo.read(&mut bytes[..], 0).map_err(|_| {
                AnnotationError::Get(felement::GetAnnotationsError::BufferReadFailed)
            })?;
            vmo.write(&bytes[..], 0).map_err(|_| {
                AnnotationError::Get(felement::GetAnnotationsError::BufferReadFailed)
            })?;
            felement::AnnotationValue::Buffer(fmem::Buffer { vmo, size: content.size })
        }
    })
}

impl AnnotationHolder {
    pub fn new() -> AnnotationHolder {
        AnnotationHolder { annotations: HashMap::new() }
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

        Ok(())
    }

    // Get the current set of annotations, as specified by fuchsia.element.AnnotationController.GetAnnotations()
    pub fn get_annotations(&self) -> Result<Vec<felement::Annotation>, AnnotationError> {
        let mut result = vec![];
        for (key, value) in &self.annotations {
            let annotation = felement::Annotation {
                key: key.as_fidl_key(),
                value: clone_annotation_value(value)?,
            };
            result.push(annotation);
        }
        Ok(result)
    }
}

// Convenient function to handle an AnnotationControllerRequestStream.
pub async fn handle_annotation_controller_stream(
    annotations: Arc<Mutex<AnnotationHolder>>,
    mut stream: felement::AnnotationControllerRequestStream,
) {
    while let Ok(Some(request)) = stream.try_next().await {
        handle_annotation_controller_request(&mut *annotations.lock().await, request);
    }
}

// Convenient function to handle an AnnotationControllerRequest.
fn handle_annotation_controller_request(
    annotations: &mut AnnotationHolder,
    request: felement::AnnotationControllerRequest,
) {
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
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::annotation::{
            handle_annotation_controller_request, AnnotationError, AnnotationHolder,
            MAX_ANNOTATIONS,
        },
        fidl::endpoints::spawn_stream_handler,
        fidl_fuchsia_element as felement,
        matches::assert_matches,
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

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_handle_annotation_controller_request() -> Result<(), anyhow::Error> {
        let holder = Arc::new(Mutex::new(AnnotationHolder::new()));

        const NAMESPACE: &str = "HARDCODED_NAMESPACE";
        const ID1: &str = "id1";
        const ID2: &str = "id2";

        let proxy: felement::AnnotationControllerProxy = spawn_stream_handler(move |req| {
            let holder = holder.clone();
            async move { handle_annotation_controller_request(&mut holder.lock().unwrap(), req) }
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
