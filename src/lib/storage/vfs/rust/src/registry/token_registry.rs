// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple implementation of the [`TokenRegistry`] trait.

use super::{TokenRegistry, TokenRegistryClient, DEFAULT_TOKEN_RIGHTS};

use {
    fidl::Handle,
    fuchsia_zircon::{AsHandleRef, Event, HandleBased, Koid, Status},
    std::collections::hash_map::HashMap,
    std::sync::{Arc, Mutex, Weak},
};

pub struct Simple {
    inner: Mutex<Inner>,
}

struct Inner {
    /// Maps a container address to a handle used as a token for this container.  Handles do not
    /// change their koid value while they are alive.  We will use the koid of a handle we receive
    /// later from the user of the API to find the container that got this particular handle
    /// associated with it.
    ///
    /// Invariant:
    ///
    ///     container_to_token.get(container).map(|handle|
    ///       token_to_container.get(handle.get_koid()).map(|weak_container|
    ///         weak_container.upgrade().map(|container2| &container == &container2)
    ///       )
    ///     ).is_some()
    ///
    /// `usize` is actually `*const dyn InodeRegistryClient`, but as pointers are `!Send` and
    /// `!Sync`, we store it as `usize` - never need to dereference it.  See
    /// [https://internals.rust-lang.org/t/shouldnt-pointers-be-send-sync-or/8818] for discussion.
    /// Another alternative would be to add `unsafe impl Send for Inner {}`, but it seems less
    /// desirable, as there are other fields in this struct.
    ///
    /// We rely on the presence of the `Weak<dyn TokenRegistryClient>` in `token_to_container` to
    /// make sure that the pointer value (store as `usize`) does not change.  `Arc` and `Weak`
    /// allocate a box for the contained value, that is not moved or deallocated while there is a
    /// live `Arc` *or* `Weak`.
    ///
    /// `Weak` actually has `as_raw()` that would allow me to implement hashing for `Weak<dyn
    /// TokenRegistryClient>` based on the contained object address and equality based on the same,
    /// but it is behind a `weak_into_raw` flag.  See
    /// [https://github.com/rust-lang/rust/issues/60728].  When this method is stable,
    /// we should switch to something like
    ///
    ///     HashMap<WeakHasher<Weak<dyn TokenRegistryClient>>, Handle>
    ///
    /// where `WeakHasher` hashes the pointer address stored inside the `Weak` instance and
    /// implements equality based on the contained object address.
    container_to_token: HashMap<usize, Handle>,

    /// Maps a koid of a container to a weak pointer to it.  We actually expect these pointers to
    /// be always valid, but for robustness we do not store strong references here.  Debug build
    /// will assert if we ever see a weak reference, but the release build will just ignore it.
    token_to_container: HashMap<Koid, Weak<dyn TokenRegistryClient>>,
}

impl Simple {
    pub fn new() -> Arc<Self> {
        Arc::new(Simple {
            inner: Mutex::new(Inner {
                container_to_token: HashMap::new(),
                token_to_container: HashMap::new(),
            }),
        })
    }
}

impl TokenRegistry for Simple {
    fn get_token(&self, container: Arc<dyn TokenRegistryClient>) -> Result<Handle, Status> {
        let container_id =
            container.as_ref() as *const dyn TokenRegistryClient as *const usize as usize;

        let mut this = if let Ok(this) = self.inner.lock() {
            this
        } else {
            debug_assert!(false, "Another thread has panicked while holding the `inner` lock.\n");
            return Err(Status::INTERNAL);
        };

        match this.container_to_token.get(&container_id) {
            Some(handle) => handle.duplicate_handle(DEFAULT_TOKEN_RIGHTS),
            None => {
                let handle = Event::create()?.into_handle();
                let koid = handle.get_koid()?;

                let res = handle.duplicate_handle(DEFAULT_TOKEN_RIGHTS)?;

                this.container_to_token.insert(container_id, handle);
                let insert_res = this.token_to_container.insert(koid, Arc::downgrade(&container));
                debug_assert!(
                    insert_res.is_none(),
                    "`container_to_token` does not have an entry, while `token_to_container` does \
                     have one.  Either entry was not removed via an `unregister()` call or there \
                     is a more serious logic error."
                );

                Ok(res)
            }
        }
    }

    fn get_container(&self, token: Handle) -> Result<Option<Arc<dyn TokenRegistryClient>>, Status> {
        let koid = token.get_koid()?;

        let this = if let Ok(this) = self.inner.lock() {
            this
        } else {
            debug_assert!(false, "Another thread has panicked while holding the `inner` lock.\n");
            return Err(Status::INTERNAL);
        };

        let res = match this.token_to_container.get(&koid) {
            Some(container) => match container.upgrade() {
                Some(container) => Some(container),
                None => {
                    debug_assert!(
                        false,
                        "`token_to_container` has a weak reference to a dropped value.\n\
                         Most likely this means that `unregister()` was not called."
                    );
                    None
                }
            },
            None => None,
        };

        Ok(res)
    }

    fn unregister(&self, container: Arc<dyn TokenRegistryClient>) {
        let container_id =
            container.as_ref() as *const dyn TokenRegistryClient as *const usize as usize;

        let mut this = if let Ok(this) = self.inner.lock() {
            this
        } else {
            debug_assert!(false, "Another thread has panicked while holding the `inner` lock.\n");
            return;
        };

        match this.container_to_token.remove(&container_id) {
            Some(handle) => {
                let koid = match handle.get_koid() {
                    Ok(koid) => koid,
                    Err(status) => {
                        debug_assert!(false, "`get_koid()` failed.  Status: {}", status);
                        return;
                    }
                };

                match this.token_to_container.remove(&koid) {
                    Some(_container) => (),
                    None => {
                        debug_assert!(
                            false,
                            "`container_to_token` has an entry, while `token_to_container` does \
                             not.  This can cause hard to diagnose bugs, as `container_to_token`
                             relies on the pointers it uses as keys to be distinct. And this is
                             only guaranteed by the fact that a `Weak` instance wrapping the
                             pointed to region is still alive."
                        );
                    }
                }
            }
            None => {
                debug_assert!(false, "`unregister()` has been already called for this container");
                ()
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        self::mocks::MockDirectory,
        super::Simple,
        crate::registry::{TokenRegistry, TokenRegistryClient, DEFAULT_TOKEN_RIGHTS},
        fuchsia_zircon::{AsHandleRef, HandleBased, Rights},
        std::sync::Arc,
    };

    #[test]
    fn client_register_same_token() {
        let client = MockDirectory::new();
        let registry = Simple::new();

        let token1 = registry.get_token(client.clone()).unwrap();
        let token2 = registry.get_token(client.clone()).unwrap();

        let koid1 = token1.get_koid().unwrap();
        let koid2 = token2.get_koid().unwrap();
        assert_eq!(koid1, koid2);
    }

    #[test]
    fn token_rights() {
        let client = MockDirectory::new();
        let registry = Simple::new();

        let token = registry.get_token(client.clone()).unwrap();

        assert_eq!(token.basic_info().unwrap().rights, DEFAULT_TOKEN_RIGHTS);
    }

    #[test]
    fn client_unregister() {
        let client = MockDirectory::new();
        let registry = Simple::new();

        let token = registry.get_token(client.clone()).unwrap();

        {
            let res = registry
                .get_container(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                .unwrap()
                .unwrap();
            assert!(Arc::ptr_eq(&(client.clone() as Arc<dyn TokenRegistryClient>), &res));
        }

        registry.unregister(client.clone());

        {
            let res = registry
                .get_container(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                .unwrap();
            assert!(
                res.is_none(),
                "`registry.get_container() is not `None` after an `unregister()` call."
            );
        }
    }

    #[test]
    fn client_get_token_twice_unregister() {
        let client = MockDirectory::new();
        let registry = Simple::new();

        let token = registry.get_token(client.clone()).unwrap();

        {
            let token2 = registry.get_token(client.clone()).unwrap();

            let koid1 = token.get_koid().unwrap();
            let koid2 = token2.get_koid().unwrap();
            assert_eq!(koid1, koid2);
        }

        registry.unregister(client.clone());

        {
            let res = registry
                .get_container(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                .unwrap();
            assert!(
                res.is_none(),
                "`registry.get_container() is not `None` after an `unregister()` call."
            );
        }
    }

    mod mocks {
        use crate::{
            directory::{
                dirents_sink,
                entry::{DirectoryEntry, EntryInfo},
                entry_container::{AsyncGetEntry, Directory, MutableDirectory},
                traversal_position::TraversalPosition,
            },
            execution_scope::ExecutionScope,
            filesystem::Filesystem,
            path::Path,
        };

        use {
            async_trait::async_trait,
            fidl::endpoints::ServerEnd,
            fidl_fuchsia_io::{NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN},
            fuchsia_async::Channel,
            fuchsia_zircon::Status,
            std::{any::Any, sync::Arc},
        };

        pub(super) struct MockDirectory {}

        impl MockDirectory {
            pub(super) fn new() -> Arc<Self> {
                Arc::new(Self {})
            }
        }

        impl DirectoryEntry for MockDirectory {
            fn open(
                self: Arc<Self>,
                _scope: ExecutionScope,
                _flags: u32,
                _mode: u32,
                _path: Path,
                _server_end: ServerEnd<NodeMarker>,
            ) {
            }

            fn entry_info(&self) -> EntryInfo {
                EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
            }

            fn can_hardlink(&self) -> bool {
                false
            }
        }

        #[async_trait]
        impl Directory for MockDirectory {
            fn get_entry(self: Arc<Self>, _name: String) -> AsyncGetEntry {
                panic!("Not implemented!")
            }

            async fn read_dirents<'a>(
                &'a self,
                _pos: &'a TraversalPosition,
                _sink: Box<dyn dirents_sink::Sink>,
            ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
                panic!("Not implemented!")
            }

            fn register_watcher(
                self: Arc<Self>,
                _scope: ExecutionScope,
                _mask: u32,
                _channel: Channel,
            ) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            fn get_attrs(&self) -> Result<NodeAttributes, Status> {
                panic!("Not implemented!")
            }

            fn unregister_watcher(self: Arc<Self>, _key: usize) {
                panic!("Not implemented!")
            }

            fn close(&self) -> Result<(), Status> {
                panic!("Not implemented!");
            }
        }

        impl MutableDirectory for MockDirectory {
            fn link(&self, _name: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            fn unlink(&self, _name: Path) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            fn set_attrs(&self, _flags: u32, _attributes: NodeAttributes) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            fn get_filesystem(&self) -> &dyn Filesystem {
                panic!("Not implemented!")
            }

            fn into_any(self: Arc<Self>) -> Arc<Any + Send + Sync> {
                panic!("Not implemented!");
            }

            fn sync(&self) -> Result<(), Status> {
                panic!("Not implemented!");
            }
        }
    }
}
