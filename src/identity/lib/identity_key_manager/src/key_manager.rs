// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::KeyManagerContext;
use fidl_fuchsia_identity_keys::{Error as ApiError, KeyManagerRequest, KeyManagerRequestStream};
use futures::prelude::*;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};

/// A client handler that serves requests on the
/// `fuchsia.identity.keys.KeyManager` FIDL protocol.
pub struct KeyManager {
    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,
}

impl KeyManager {
    /// Creates a new `KeyManager`.
    pub fn new(task_group: TaskGroup) -> Self {
        KeyManager { task_group }
    }

    /// Returns a task group which can be used to spawn and cancel tasks that
    /// use this instance.
    pub fn task_group(&self) -> &TaskGroup {
        &self.task_group
    }

    /// Serves requests received through the provided `request_stream` for the
    /// application specified by `context`.
    pub async fn handle_requests_from_stream(
        &self,
        context: &KeyManagerContext,
        mut request_stream: KeyManagerRequestStream,
        cancel: TaskGroupCancel,
    ) -> Result<(), anyhow::Error> {
        while let Some(result) = cancel_or(&cancel, request_stream.try_next()).await {
            match result? {
                Some(request) => self.handle_fidl_request(request, context)?,
                None => break,
            }
        }
        Ok(())
    }

    fn handle_fidl_request(
        &self,
        request: KeyManagerRequest,
        _context: &KeyManagerContext,
    ) -> Result<(), fidl::Error> {
        match request {
            KeyManagerRequest::WatchOrCreateKeySingleton { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::WatchKeySingleton { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::DeleteKeySingleton { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::GetOrCreateKeySet { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::GetKeySet { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::FreezeKeySet { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
            KeyManagerRequest::DeleteKeySet { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_identity_keys::{
        KeyManagerMarker, KeyManagerProxy, KeySetMarker, KeySetProperties, KeySingletonProperties,
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::channel::oneshot;
    use futures::future::join;
    use test_util::assert_matches;

    const TEST_KEY_SINGLETON_NAME: &str = "test-key-singleton";
    const TEST_KEY_SET_NAME: &str = "test-key-set";

    fn run_key_manager_test<F, Fut>(test_fn: F)
    where
        F: FnOnce(KeyManagerProxy) -> Fut,
        Fut: Future<Output = Result<(), anyhow::Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (key_manager_proxy, manager_request_stream) =
            create_proxy_and_stream::<KeyManagerMarker>()
                .expect("Failed to create proxy and stream");

        let key_manager = KeyManager::new(TaskGroup::new());
        let (_sender, reciever) = oneshot::channel();
        let key_manager_context = KeyManagerContext::new("test-application-url".to_string());
        let server_fut = async move {
            key_manager
                .handle_requests_from_stream(
                    &key_manager_context,
                    manager_request_stream,
                    reciever.shared(),
                )
                .await
        };

        let joined_fut = join(test_fn(key_manager_proxy), server_fut);
        fasync::pin_mut!(joined_fut);
        let (test_result, server_result) = match executor.run_until_stalled(&mut joined_fut) {
            core::task::Poll::Ready((test_result, server_result)) => (test_result, server_result),
            _ => panic!("Executor stalled!"),
        };
        assert!(server_result.is_ok());
        assert!(test_result.is_ok());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_cancel() {
        let (km_proxy, manager_request_stream) = create_proxy_and_stream::<KeyManagerMarker>()
            .expect("Failed to create proxy and stream");
        let key_manager = KeyManager::new(TaskGroup::new());
        let (cancel, reciever) = oneshot::channel();
        let key_manager_context = KeyManagerContext::new("test-application-url".to_string());
        let server_fut = async move {
            key_manager
                .handle_requests_from_stream(
                    &key_manager_context,
                    manager_request_stream,
                    reciever.shared(),
                )
                .await
        };

        let test_fut = async move {
            // Before cancellation requests should be served
            assert_eq!(
                km_proxy.delete_key_singleton("key-singleton").await?,
                Err(ApiError::UnsupportedOperation)
            );

            cancel.send(()).unwrap_or_else(|_| panic!("Failed to cancel"));

            // After cancellation is sent stream should be closed
            assert_matches!(
                km_proxy.delete_key_singleton("key-singleton").await,
                Err(fidl::Error::ClientChannelClosed(zx::Status::PEER_CLOSED))
            );

            Result::<(), anyhow::Error>::Ok(())
        };

        let (test_res, server_res) = join(test_fut, server_fut).await;
        assert!(test_res.is_ok());
        assert!(server_res.is_ok());
    }

    #[test]
    fn test_watch_or_create_key_singleton() {
        run_key_manager_test(|km_proxy| async move {
            assert_eq!(
                km_proxy
                    .watch_or_create_key_singleton(KeySingletonProperties {
                        name: None,
                        uid: None,
                        metadata: None,
                        key_length: None
                    })
                    .await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }

    #[test]
    fn test_watch_key_singleton() {
        run_key_manager_test(|km_proxy| async move {
            assert_eq!(
                km_proxy.watch_key_singleton(TEST_KEY_SINGLETON_NAME).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }

    #[test]
    fn test_delete_key_singleton() {
        run_key_manager_test(|km_proxy| async move {
            assert_eq!(
                km_proxy.delete_key_singleton(TEST_KEY_SINGLETON_NAME).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }

    #[test]
    fn test_get_or_create_key_set() {
        run_key_manager_test(|km_proxy| async move {
            let key_set_properties = KeySetProperties {
                name: None,
                uid: None,
                metadata: None,
                key_length: None,
                max_keys: None,
                automatic_rotation: None,
                manual_rotation: None,
            };
            let (_key_set_proxy, key_set_server) = create_proxy::<KeySetMarker>()?;
            assert_eq!(
                km_proxy.get_or_create_key_set(key_set_properties, None, key_set_server).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }

    #[test]
    fn test_get_key_set() {
        run_key_manager_test(|km_proxy| async move {
            let (_key_set_proxy, key_set_server) = create_proxy::<KeySetMarker>()?;
            assert_eq!(
                km_proxy.get_key_set(TEST_KEY_SET_NAME, key_set_server).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }

    #[test]
    fn test_freeze_key_set() {
        run_key_manager_test(|km_proxy| async move {
            assert_eq!(
                km_proxy.freeze_key_set(TEST_KEY_SET_NAME).await?,
                Err(ApiError::UnsupportedOperation)
            );

            Ok(())
        });
    }

    #[test]
    fn test_delete_key_set() {
        run_key_manager_test(|km_proxy| async move {
            assert_eq!(
                km_proxy.delete_key_set(TEST_KEY_SET_NAME).await?,
                Err(ApiError::UnsupportedOperation)
            );
            Ok(())
        });
    }
}
