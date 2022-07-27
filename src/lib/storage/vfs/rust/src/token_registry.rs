// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of [`TokenRegistry`].

use {
    crate::directory::entry_container::MutableDirectory,
    fidl::Handle,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::{AsHandleRef, Event, HandleBased, Koid, Rights, Status},
    pin_project::{pin_project, pinned_drop},
    std::{
        collections::hash_map::{Entry, HashMap},
        ops::{Deref, DerefMut},
        pin::Pin,
        sync::{Arc, Mutex},
    },
};

const DEFAULT_TOKEN_RIGHTS: Rights = Rights::BASIC;

pub struct TokenRegistry {
    inner: Mutex<Inner>,
}

struct Inner {
    /// Maps an owner to a handle used as a token for the owner.  Handles do not change their koid
    /// value while they are alive.  We will use the koid of a handle we receive later from the user
    /// of the API to find the owner that has this particular handle associated with it.
    ///
    /// Every entry in owner_to_token will have a reverse mapping in token_to_owner.
    ///
    /// Owners must be wrapped in Tokenizable which will ensure tokens are unregistered when
    /// Tokenizable is dropped.  They must be pinned since pointers are used.  They must also
    /// implement the TokenInterface trait which extracts the information that `get_owner` returns.
    owner_to_token: HashMap<*const (), Handle>,

    /// Maps a koid of an owner to the owner.
    token_to_owner: HashMap<Koid, *const dyn TokenInterface>,
}

unsafe impl Send for Inner {}

impl TokenRegistry {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(Inner {
                owner_to_token: HashMap::new(),
                token_to_owner: HashMap::new(),
            }),
        }
    }

    /// Returns a token for the owner, creating one if one doesn't already exist.  Tokens will be
    /// automatically removed when Tokenizable is dropped.
    pub fn get_token<T: TokenInterface>(owner: Pin<&Tokenizable<T>>) -> Result<Handle, Status> {
        let ptr = owner.get_ref() as *const _ as *const ();
        let mut this = owner.token_registry().inner.lock().unwrap();
        let Inner { owner_to_token, token_to_owner, .. } = &mut *this;
        match owner_to_token.entry(ptr) {
            Entry::Occupied(o) => o.into_mut(),
            Entry::Vacant(v) => {
                let handle = Event::create()?.into_handle();
                let koid = handle.get_koid()?;
                assert!(
                    token_to_owner.insert(koid, &owner.0 as &dyn TokenInterface).is_none(),
                    "koid is a duplicate"
                );
                v.insert(handle)
            }
        }
        .duplicate_handle(DEFAULT_TOKEN_RIGHTS)
    }

    /// Returns the information provided by get_node_and_flags for the given token.  Returns None if
    /// no such token exists (perhaps because the owner has been dropped).
    pub fn get_owner(
        &self,
        token: Handle,
    ) -> Result<Option<(Arc<dyn MutableDirectory>, fio::OpenFlags)>, Status> {
        let koid = token.get_koid()?;
        let this = self.inner.lock().unwrap();

        match this.token_to_owner.get(&koid) {
            Some(owner) => {
                // SAFETY: This is safe because Tokenizable's drop will ensure that unregister is
                // called to avoid any dangling pointers.
                Ok(Some(unsafe { (**owner).get_node_and_flags() }))
            }
            None => Ok(None),
        }
    }

    // Unregisters the token. This is done automatically by Tokenizable below.
    fn unregister<T: TokenInterface>(&self, owner: &Tokenizable<T>) {
        let ptr = owner as *const _ as *const ();
        let mut this = self.inner.lock().unwrap();

        if let Some(handle) = this.owner_to_token.remove(&ptr) {
            this.token_to_owner.remove(&handle.get_koid().unwrap()).unwrap();
        }
    }
}

pub trait TokenInterface: 'static {
    /// Returns the node and flags that correspond with this token.  This information is returned by
    /// the `get_owner` method.  For now this always returns Arc<dyn MutableDirectory> but it should
    /// be possible to change this so that files can be represented in future if and when the need
    /// arises.
    fn get_node_and_flags(&self) -> (Arc<dyn MutableDirectory>, fio::OpenFlags);

    /// Returns the token registry.
    fn token_registry(&self) -> &TokenRegistry;
}

/// Tokenizable is to be used to wrap anything that might need to have tokens generated.  It will
/// ensure that the token is unregistered when Tokenizable is dropped.
#[pin_project(!Unpin, PinnedDrop)]
pub struct Tokenizable<T: TokenInterface>(#[pin] T);

impl<T: TokenInterface> Tokenizable<T> {
    pub fn new(inner: T) -> Self {
        Self(inner)
    }

    pub fn as_mut(self: Pin<&mut Self>) -> Pin<&mut T> {
        self.project().0
    }
}

impl<T: TokenInterface> Deref for Tokenizable<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.0
    }
}

impl<T: TokenInterface> DerefMut for Tokenizable<T> {
    fn deref_mut(&mut self) -> &mut T {
        &mut self.0
    }
}

#[pinned_drop]
impl<T: TokenInterface> PinnedDrop for Tokenizable<T> {
    fn drop(self: Pin<&mut Self>) {
        self.0.token_registry().unregister(&self);
    }
}

#[cfg(test)]
mod tests {
    use {
        self::mocks::{MockChannel, MockDirectory},
        super::{TokenRegistry, Tokenizable, DEFAULT_TOKEN_RIGHTS},
        fuchsia_zircon::{AsHandleRef, HandleBased, Rights},
        futures::pin_mut,
        std::sync::Arc,
    };

    #[test]
    fn client_register_same_token() {
        let registry = Arc::new(TokenRegistry::new());
        let client = Tokenizable(MockChannel(registry.clone(), MockDirectory::new()));
        pin_mut!(client);

        let token1 = TokenRegistry::get_token(client.as_ref()).unwrap();
        let token2 = TokenRegistry::get_token(client.as_ref()).unwrap();

        let koid1 = token1.get_koid().unwrap();
        let koid2 = token2.get_koid().unwrap();
        assert_eq!(koid1, koid2);
    }

    #[test]
    fn token_rights() {
        let registry = Arc::new(TokenRegistry::new());
        let client = Tokenizable(MockChannel(registry.clone(), MockDirectory::new()));
        pin_mut!(client);

        let token = TokenRegistry::get_token(client.as_ref()).unwrap();

        assert_eq!(token.basic_info().unwrap().rights, DEFAULT_TOKEN_RIGHTS);
    }

    #[test]
    fn client_unregister() {
        let registry = Arc::new(TokenRegistry::new());

        let token = {
            let client = Tokenizable(MockChannel(registry.clone(), MockDirectory::new()));
            pin_mut!(client);

            let token = TokenRegistry::get_token(client.as_ref()).unwrap();

            {
                let res = registry
                    .get_owner(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                    .unwrap()
                    .unwrap();
                // Note this ugly cast in place of `Arc::ptr_eq(&client, &res)` here is to ensure we
                // don't compare vtable pointers, which are not strictly guaranteed to be the same
                // across casts done in different code generation units at compilation time.
                assert_eq!(Arc::as_ptr(&client.1) as *const (), Arc::as_ptr(&res.0) as *const ());
            }

            token
        };

        assert!(
            registry
                .get_owner(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                .unwrap()
                .is_none(),
            "`registry.get_owner() is not `None` after an connection dropped."
        );
    }

    #[test]
    fn client_get_token_twice_unregister() {
        let registry = Arc::new(TokenRegistry::new());

        let token = {
            let client = Tokenizable(MockChannel(registry.clone(), MockDirectory::new()));
            pin_mut!(client);

            let token = TokenRegistry::get_token(client.as_ref()).unwrap();

            {
                let token2 = TokenRegistry::get_token(client.as_ref()).unwrap();

                let koid1 = token.get_koid().unwrap();
                let koid2 = token2.get_koid().unwrap();
                assert_eq!(koid1, koid2);
            }

            token
        };

        assert!(
            registry
                .get_owner(token.duplicate_handle(Rights::SAME_RIGHTS).unwrap())
                .unwrap()
                .is_none(),
            "`registry.get_owner() is not `None` after connection dropped."
        );
    }

    mod mocks {
        use crate::{
            directory::{
                dirents_sink,
                entry::{DirectoryEntry, EntryInfo},
                entry_container::{Directory, DirectoryWatcher, MutableDirectory},
                traversal_position::TraversalPosition,
            },
            execution_scope::ExecutionScope,
            path::Path,
            token_registry::{TokenInterface, TokenRegistry},
        };

        use {
            async_trait::async_trait, fidl::endpoints::ServerEnd, fidl_fuchsia_io as fio,
            fuchsia_zircon::Status, std::sync::Arc,
        };

        pub(super) struct MockChannel(pub Arc<TokenRegistry>, pub Arc<MockDirectory>);

        impl TokenInterface for MockChannel {
            fn get_node_and_flags(&self) -> (Arc<dyn MutableDirectory>, fio::OpenFlags) {
                (self.1.clone(), fio::OpenFlags::RIGHT_READABLE)
            }

            fn token_registry(&self) -> &TokenRegistry {
                &self.0
            }
        }

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
                _flags: fio::OpenFlags,
                _mode: u32,
                _path: Path,
                _server_end: ServerEnd<fio::NodeMarker>,
            ) {
            }

            fn entry_info(&self) -> EntryInfo {
                EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
            }
        }

        #[async_trait]
        impl Directory for MockDirectory {
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
                _mask: fio::WatchMask,
                _watcher: DirectoryWatcher,
            ) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            async fn get_attrs(&self) -> Result<fio::NodeAttributes, Status> {
                panic!("Not implemented!")
            }

            fn unregister_watcher(self: Arc<Self>, _key: usize) {
                panic!("Not implemented!")
            }

            fn close(&self) -> Result<(), Status> {
                panic!("Not implemented!");
            }
        }

        #[async_trait]
        impl MutableDirectory for MockDirectory {
            async fn unlink(
                self: Arc<Self>,
                _name: &str,
                _must_be_directory: bool,
            ) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            async fn set_attrs(
                &self,
                _flags: fio::NodeAttributeFlags,
                _attributes: fio::NodeAttributes,
            ) -> Result<(), Status> {
                panic!("Not implemented!")
            }

            async fn sync(&self) -> Result<(), Status> {
                panic!("Not implemented!");
            }

            async fn rename(
                self: Arc<Self>,
                _src_dir: Arc<dyn MutableDirectory>,
                _src_name: Path,
                _dst_name: Path,
            ) -> Result<(), Status> {
                panic!("Not implemented!");
            }
        }
    }
}
