// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of `fuchsia.ui.shortcut2`.

use anyhow::{self, Context, Result};
use fidl_fuchsia_ui_focus::FocusChain;
use fidl_fuchsia_ui_input3::{KeyEvent, KeyEventType, KeyMeaning};
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_shortcut2 as fs2;
use fuchsia_async::{self as fasync, Task};
use fuchsia_scenic as scenic;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::{
    channel::mpsc::{self, Receiver, Sender},
    lock::Mutex,
    SinkExt, StreamExt, TryStreamExt,
};
use keymaps;
use std::{cell::RefCell, rc::Rc};
use std::{
    collections::{BTreeMap, BTreeSet},
    ops::Deref,
};
use tracing::{debug, error, warn};

/// The shortcut identifier for reporting to the consumers.
#[derive(Debug, Clone, PartialEq)]
struct ShortcutId(u32);

impl Deref for ShortcutId {
    type Target = u32;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// A consumer Session identifier.
#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
struct SessionId(zx::Koid);

impl Deref for SessionId {
    type Target = zx::Koid;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// A unique view identifier.
#[derive(Debug, Clone, Copy, Eq, PartialEq, Ord, PartialOrd)]
struct ViewId(zx::Koid);

impl Deref for ViewId {
    type Target = zx::Koid;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[derive(Debug, Clone)]
struct ShortcutSessionIdPair {
    session_id: SessionId,
    shortcut_id: ShortcutId,
}

#[derive(Debug, Clone)]
struct ViewSessionIdPair {
    view_id: ViewId,
    session_id: SessionId,
}

/// A single chord consists of multiple key meanings.
///
/// Used to identify keys that are pressed together.
///
/// The internal implementation of KeyChord is a newtype of `BTreeSet`. This is
/// since `BTreeSet` implements `Hash` and other traits that make it suitable
/// to be used as a map key, in contrast to `HashSet`. This simplistic
/// implementation is probably enough for the time being.
#[derive(Clone, Debug, Eq, Hash, PartialEq, Ord, PartialOrd)]
struct KeyChord(BTreeSet<KeyMeaning>);

impl From<&[KeyMeaning]> for KeyChord {
    fn from(meanings: &[KeyMeaning]) -> Self {
        KeyChord(meanings.iter().map(|key| *key).collect())
    }
}

impl KeyChord {
    fn new() -> Self {
        KeyChord(BTreeSet::new())
    }

    fn update(&mut self, key_meaning: KeyMeaning, event_type: KeyEventType) {
        match event_type {
            KeyEventType::Pressed | KeyEventType::Sync => self.0.insert(key_meaning),
            KeyEventType::Released | KeyEventType::Cancel => self.0.remove(&key_meaning),
        };
    }
}

/// A single connection by a single client to the `fuchsia.ui.shortcut2` API.
///
/// This state can not be copied around since its internals aren't really
/// copyable.
#[derive(Debug)]
pub struct Session {
    /// This session's task.  It waits for shortcuts to send out, and
    /// returns responses.  Cleans up the shortcut map on exit.
    _task: Task<Result<()>>,

    /// The channel input to _task, used to hand the shortcut ID to notify
    /// about.
    input: Sender<ShortcutId>,

    /// The output from _task, containing the result of the notification for
    /// the shortcut ID sent via `input`.
    output: Receiver<fs2::Handled>,
}

/// Stores a denormalized map of key chords to session ids.
#[derive(Debug)]
struct ShortcutMap(BTreeMap<KeyChord, BTreeMap<ViewId, ShortcutSessionIdPair>>);

impl ShortcutMap {
    /// Creates a new empty [ShortcutMap].
    fn new() -> Self {
        Self(BTreeMap::new())
    }

    /// Get the mapping of view IDs to session IDs for all Listeners interested in
    /// the [KeyChord] `key`.
    fn get(&self, key: &KeyChord) -> Option<&BTreeMap<ViewId, ShortcutSessionIdPair>> {
        self.0.get(key)
    }

    /// Adds all the session information into the shortcut map.
    fn add_session_id(
        &mut self,
        session_id: SessionId,
        shortcuts: Vec<fs2::Shortcut>,
        view_id: ViewId,
    ) {
        let shortcut_map = &mut self.0;
        for shortcut in shortcuts.iter() {
            let chord = KeyChord::from(&shortcut.key_meanings[..]);
            if !shortcut_map.contains_key(&chord) {
                shortcut_map.insert(chord.clone(), BTreeMap::new());
            }
            if let Some(id_map) = shortcut_map.get_mut(&chord) {
                id_map.insert(
                    view_id,
                    ShortcutSessionIdPair { shortcut_id: ShortcutId(shortcut.id), session_id },
                );
            }
        }
    }

    /// Removes the traces of [SessionId] from [ShortcutMap].
    ///
    /// Closes out all resources associated with the Session.
    fn remove_session_id(&mut self, session_id: &SessionId) {
        // A lot of in-place mutations, not sure this is doable with iterators.
        //
        // Remove all shortcuts associated with this session.  aka "massive mutability mess".
        // Iterate over keys only so that we could mutate what needs to be mutated.  This might be
        // optimizable if we keep track where each session ID is exactly.
        let shortcut_map = &mut self.0;
        let chords = shortcut_map.keys().cloned().collect::<Vec<_>>();
        for chord in chords.into_iter() {
            let mut remove_chord = false;
            if let Some(id_map) = shortcut_map.get_mut(&chord) {
                let mut removed_id_map_entry = false;
                let view_ids = id_map.keys().cloned().collect::<Vec<_>>();
                for view_id in view_ids.into_iter() {
                    let mut to_remove = false;
                    if let Some(id_pair) = id_map.get(&view_id) {
                        // Needs to be removed. Since we can't remove while iterating, removal is
                        // scheduled for after we exit the scope. Here and below too.
                        if id_pair.session_id == *session_id {
                            to_remove = true;
                        }
                    }
                    if to_remove {
                        id_map.remove(&view_id);
                        removed_id_map_entry = true;
                    }
                }
                if removed_id_map_entry {
                    // Check if it is now empty and remove.
                    remove_chord = id_map.is_empty();
                }
            }
            if remove_chord {
                shortcut_map.remove(&chord);
            }
        }
    }
}

#[derive(Debug)]
struct Inner {
    /// The mapping of key chords to interested sessions.
    shortcut_map: ShortcutMap,
    /// The map of currently active sessions.
    sessions: BTreeMap<SessionId, Session>,
    /// The currently valid focus chain view IDs. Last element is the ID of the
    /// focused view.
    focus_chain_view_ids: Vec<ViewId>,
}

/// The implementation of the FIDL API `fuchsia.ui.shortcut2`.
///
/// Create a new instance via [Shortcut2Impl::new].  The `handle_*` methods
/// each handle a specific asynchronous event that the handler supports.
///
/// New clients are registered via [handle_registry_stream].  New key events
/// are handled via [handle_key_event].  Focus changes are handled
/// via [handle_focus_change].
#[derive(Debug)]
pub struct Shortcut2Impl {
    /// The currently active key chord.
    /// It is currently not borrowed across await calls, so should be OK to
    /// borrow via RefCell only.
    keys: Rc<RefCell<KeyChord>>,

    /// The internal state. It gets mutated from concurrently executing calls,
    /// and must be held across awaits, so it is protected by a mutex.
    inner: Rc<Mutex<Inner>>,
}

impl Shortcut2Impl {
    /// Create a new Shortcut2Impl.
    pub fn new() -> Self {
        Self {
            keys: Rc::new(RefCell::new(KeyChord::new())),
            inner: Rc::new(Mutex::new(Inner {
                shortcut_map: ShortcutMap::new(),
                sessions: BTreeMap::new(),
                focus_chain_view_ids: vec![],
            })),
        }
    }

    /// Handles requests to `fuchsia.ui.shortcut.Manager` interface.
    pub async fn manager_server(
        &self,
        mut stream: ui_shortcut::ManagerRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await.context("error running manager server")? {
            match req {
                ui_shortcut::ManagerRequest::HandleKey3Event { event, responder } => {
                    let was_handled = match self.handle_key_event(event).await? {
                        fs2::Handled::Handled => true,
                        fs2::Handled::NotHandled | fs2::HandledUnknown!() => false,
                    };
                    responder
                        .send(was_handled)
                        .context("Error sending response for HandleKey3Event")?;
                }
                ui_shortcut::ManagerRequest::HandleFocusChange {
                    focus_chain, responder, ..
                } => {
                    self.handle_focus_change(&focus_chain).await;
                    responder.send()?;
                }
            }
        }
        Ok(())
    }

    /// Processes a [KeyEvent].
    ///
    /// Processing a key event includes triggering notifications for matching
    /// listeners. Listeners must have a matching ViewRef and shortcut.
    pub async fn handle_key_event(&self, event: KeyEvent) -> Result<fs2::Handled> {
        validate_event(&event)?;

        let key_event_type = event.type_.expect("validate_event");
        let key_meaning = to_lowercase(keymaps::get_key_meaning(
            &event.key,
            &event.key_meaning,
            &event.lock_state,
            &event.modifiers,
        ));
        self.keys.borrow_mut().update(key_meaning, key_event_type);

        // Only ever activate a shortcut on a key press. This avoids spurious
        // shortcut activation of Ctrl+A, if Ctrl+A+B is actuated, and B is then
        // released; or on any Sync or Cancel event types.
        if key_event_type == KeyEventType::Pressed {
            let shortcut_session_ids: Vec<ShortcutSessionIdPair> = {
                // Lock.
                let inner = self.inner.lock().await;
                if let Some(id_map) = inner.shortcut_map.get(&self.keys.borrow()) {
                    inner
                        .focus_chain_view_ids
                        .iter() // &ViewId
                        .map(|koid| id_map.get(koid)) // Option<&IdPair>
                        .flatten()
                        .map(|e| e.clone())
                        .collect()
                } else {
                    vec![]
                }
            };
            // Now go through the candidate sessions and try notifying them.
            // Valid ViewRefs should get notified.
            for ShortcutSessionIdPair { shortcut_id, session_id } in
                shortcut_session_ids.into_iter()
            {
                let mut inner = self.inner.lock().await;
                // Session could disappear between this lock and the previous one, so
                // skip notification if it did disappear.
                if let Some(session) = inner.sessions.get_mut(&session_id) {
                    session.input.send(shortcut_id).await.context("session.input.send")?;
                    let handled = session.output.next().await.context("handled signal")?;
                    // Stop with notifications once first willing and able
                    // Listener is found.
                    if handled == fs2::Handled::Handled {
                        return Ok(handled);
                    }
                }
            }
        }

        Ok(fs2::Handled::NotHandled)
    }

    /// Serves a single connection to `fuchsia.ui.shortcut2/Registry`.
    ///
    /// A prospective client must first contact the Registry and register itself
    /// and its desired shortcuts.  See the FIDL documentation for this API for
    /// usage details.
    pub async fn handle_registry_stream(
        &self,
        mut stream: fs2::RegistryRequestStream,
    ) -> Result<()> {
        // Any early calls to RegisterShortcut are saved here.  We don't limit
        // the maximum number of registrations, though in principle we could.
        let mut preregistered_shortcuts = vec![];

        // Set if a session has been established (SetView has been called).
        let mut view_session_id_pair: Option<ViewSessionIdPair> = None;
        while let Some(req) =
            stream.try_next().await.context("error serving fuchsia.ui.shortcut2/Registry)")?
        {
            match req {
                fs2::RegistryRequest::SetView { view_ref, listener, .. } => {
                    let session_id = SessionId(koid_of(&listener)?);
                    let view_id = ViewId(koid_of(&view_ref.reference)?);
                    view_session_id_pair = Some(ViewSessionIdPair { view_id, session_id });

                    let listener_proxy = listener.into_proxy()?;

                    // Task input side
                    let (in_sender_end, mut in_receiver_end) = mpsc::channel::<ShortcutId>(1);

                    // Task output side
                    let (mut out_sender_end, out_receiver_end) = mpsc::channel(1);

                    // Create a notification task.
                    // Hm, is this task really necessary if we have a mutex?
                    // The only reason I introduced it is to not have to copy
                    // ViewRef and Proxy, but that ship has sailed it seems.
                    let inner_clone = self.inner.clone();
                    let session_id_clone = session_id.clone();
                    let notify_closure = async move {
                        while let Some(id) = in_receiver_end.next().await {
                            match listener_proxy.on_shortcut(*id).await {
                                Ok(result) => out_sender_end
                                    .send(result)
                                    .await
                                    .expect("out_sender_end always sendable"),
                                Err(e) => {
                                    warn!("error on send, exiting: {:?}", &e);
                                    break;
                                }
                            }
                        }
                        // When the connection closes, clean up the session info.
                        let mut locked = inner_clone.lock().await;
                        locked.sessions.remove(&session_id_clone);
                        locked.shortcut_map.remove_session_id(&session_id_clone);
                        Ok(())
                    };
                    let session = Session {
                        _task: fasync::Task::local(notify_closure),
                        input: in_sender_end,
                        output: out_receiver_end,
                    };

                    let shortcuts = preregistered_shortcuts;
                    preregistered_shortcuts = vec![];
                    {
                        // Lock
                        let mut locked = self.inner.lock().await;
                        locked.sessions.insert(session_id, session);
                        locked.shortcut_map.add_session_id(session_id, shortcuts, view_id);
                    }
                }

                fs2::RegistryRequest::RegisterShortcut { shortcut, responder, .. } => {
                    if let Err(e) = validate_shortcut(&shortcut) {
                        warn!("shortcut failed validation: {:?}", &e);
                        if let Err(e) = responder
                            .send(&mut Err(fs2::Error::IllegalArgument))
                            .context("while sending RegisterShortcut Error response")
                        {
                            error!(?e);
                        }
                    } else {
                        // Is a session already registered? Then just add into it.
                        // If it isn't, then add preregistered.
                        if let Some(ref id_pair) = view_session_id_pair {
                            let mut locked = self.inner.lock().await;
                            locked.shortcut_map.add_session_id(
                                id_pair.session_id,
                                vec![shortcut],
                                id_pair.view_id,
                            );
                        } else {
                            preregistered_shortcuts.push(shortcut);
                        }
                        if let Err(e) = responder
                            .send(&mut Ok(()))
                            .context("while sending RegisterShortcut OK response")
                        {
                            error!(?e);
                        }
                    }
                }
            }
        }
        Ok(())
    }

    /// Updates the current view of the focus chain for the shortcut handler.
    pub async fn handle_focus_change(&self, focus_chain: &FocusChain) {
        let view_refs = focus_chain.focus_chain.as_ref().map(clone_focus_chain).unwrap_or(vec![]);
        let view_ids = koids_of(&view_refs).into_iter().map(|k| ViewId(k)).collect();
        debug!(focus_chain = ?view_ids, "shortcut2/handle_focus_change");

        self.inner.lock().await.focus_chain_view_ids = view_ids;
    }
}

/// Turns `focus_chain` into a sequence of corresponding `zx::Koid`s.  Mainly useful
/// for debugging.
fn koids_of(focus_chain: &Vec<fidl_fuchsia_ui_views::ViewRef>) -> Vec<zx::Koid> {
    focus_chain
        .iter()
        .map(|v| v.reference.as_handle_ref().get_koid().expect("failed to get koid"))
        .collect()
}

/// Clones the supplied focus chain, since [ViewRef]s are not directly cloneable.
fn clone_focus_chain(
    focus_chain: &Vec<fidl_fuchsia_ui_views::ViewRef>,
) -> Vec<fidl_fuchsia_ui_views::ViewRef> {
    focus_chain
        .iter()
        .map(|v| scenic::duplicate_view_ref(v).expect("failed to clone a ViewRef"))
        .collect()
}

/// Code point for 'A'.
const UPPERCASE_A: u32 = 'A' as u32;
/// Code point for 'Z'.
const UPPERCASE_Z: u32 = 'Z' as u32;

/// The range of uppercase Latin letters.
const UPPERCASE_LATIN: std::ops::RangeInclusive<u32> = UPPERCASE_A..=UPPERCASE_Z;

/// The range of 7-bit ASCII.
const PRINTABLE_ASCII: std::ops::RangeInclusive<u32> = 0x20..=0x7f;

/// Converts the key meaning into one with lowercase code point if such a point
/// exists.  Else passes the code point unchanged.
fn to_lowercase(k: KeyMeaning) -> KeyMeaning {
    match k {
        // Admit code points from the Latin block, but not uppercase.
        KeyMeaning::Codepoint(c) if UPPERCASE_LATIN.contains(&c) => {
            let lower = char::from_u32(c).unwrap().to_lowercase().next().unwrap() as u32;
            KeyMeaning::Codepoint(lower)
        }
        k => k,
    }
}

/// Validates the event without crashing the service.
///
/// We must validate the input since while KeyEvent is a FIDL table, some of
/// its formally optional fields are actually required by protocol.
fn validate_event(event: &KeyEvent) -> Result<()> {
    if event.type_.is_none() || (event.key.is_none() && event.key_meaning.is_none()) {
        Err(anyhow::anyhow!("BUGBUGBUG: malformed KeyEvent: {:?}", &event))
    } else {
        Ok(())
    }
}

/// Validates the supplied shortcut at registration time.
fn validate_shortcut(shortcut: &fs2::Shortcut) -> Result<()> {
    shortcut
        .key_meanings
        .iter()
        .map(|k: &KeyMeaning| -> Result<()> {
            match k {
                KeyMeaning::Codepoint(c) => {
                    // For now, allow printable Latin shortcuts, but no uppercases.
                    // Nonprintable keys that have an ASCII code should be
                    // registered using NonPrintableKey.
                    if PRINTABLE_ASCII.contains(c) && !UPPERCASE_LATIN.contains(c) {
                        Ok(())
                    } else {
                        Err(anyhow::anyhow!("malformed KeyMeaning: {:?}", k))
                    }
                }
                KeyMeaning::NonPrintableKey(_) => Ok(()),
            }
        })
        .collect()
}
/// Returns a koid of the given handle
fn koid_of(h: &impl AsHandleRef) -> Result<zx::Koid> {
    h.as_handle_ref().get_koid().context("shortcut2/koid_of")
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints;
    use fidl_fuchsia_ui_focus::FocusChain;
    use fidl_fuchsia_ui_input3::{self as finput3, NonPrintableKey};
    use fidl_fuchsia_ui_views::ViewRef;
    use fuchsia_scenic as scenic;
    use futures::{channel::mpsc, TryStreamExt};
    use pretty_assertions::assert_eq;
    use test_case::test_case;

    const SHORTCUT_ID: ShortcutId = ShortcutId(42);

    const SHORTCUT_ID_2: ShortcutId = ShortcutId(84);

    /// Creates a new fake listener for shortcuts.
    ///
    /// `stream` is the notification stream to listen to. It is assumed that
    /// the listener has already been registered.  `response` is the response
    /// that the listener will keep sending when notified.
    ///
    /// Returns a listener task, and a receiver that will get the resulting notifications.
    /// Keep the task alive to ensure that listener events get processed.
    fn new_fake_listener(
        mut stream: fs2::ListenerRequestStream,
        response: fs2::Handled,
    ) -> (Task<Result<()>>, mpsc::UnboundedReceiver<ShortcutId>) {
        let (sender, receiver) = mpsc::unbounded();
        let task = fasync::Task::local(async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    fs2::ListenerRequest::OnShortcut { id, responder } => {
                        sender.unbounded_send(ShortcutId(id)).unwrap();
                        responder.send(response).unwrap();
                    }
                }
            }
            Ok(())
        });
        (task, receiver)
    }

    fn new_view_ref() -> ViewRef {
        scenic::ViewRefPair::new().unwrap().view_ref
    }

    fn clone_view_ref(view_ref: &ViewRef) -> ViewRef {
        scenic::duplicate_view_ref(view_ref).unwrap()
    }

    fn into_focus_chain(focus_chain: Vec<ViewRef>) -> FocusChain {
        FocusChain { focus_chain: Some(focus_chain), ..FocusChain::EMPTY }
    }

    // A shorthand initializer for a key meaning based on a code point.
    fn cp_key(cp: char) -> finput3::KeyMeaning {
        finput3::KeyMeaning::Codepoint(cp as u32)
    }

    // A shorthand initializer for a key meaning based on a nonprintable key.
    fn np_key(key: NonPrintableKey) -> finput3::KeyMeaning {
        finput3::KeyMeaning::NonPrintableKey(key)
    }

    // Creates a shortcut of all supplied key meanings
    fn key_meanings_shortcut(id: ShortcutId, key_meanings: Vec<KeyMeaning>) -> fs2::Shortcut {
        fs2::Shortcut {
            id: *id,
            key_meanings: key_meanings.iter().map(|k| *k).collect(),
            options: fs2::Options { ..fs2::Options::EMPTY },
        }
    }

    // Creates a shortcut of all supplied code points.
    fn codepoint_shortcut(id: ShortcutId, code_points: Vec<char>) -> fs2::Shortcut {
        key_meanings_shortcut(id, code_points.into_iter().map(|c| cp_key(c)).collect::<Vec<_>>())
    }

    // A helper for passing a key meaning sequence through the given handler and
    // reporting results.
    async fn handle_key_sequence(
        handler: &Rc<Shortcut2Impl>,
        key_sequence: Vec<(finput3::KeyMeaning, finput3::KeyEventType)>,
    ) -> Vec<fs2::Handled> {
        let mut result = vec![];
        for (key_meaning, type_) in key_sequence {
            let ret = handler
                .handle_key_event(KeyEvent {
                    key_meaning: Some(key_meaning),
                    type_: Some(type_),
                    ..KeyEvent::EMPTY
                })
                .await
                .expect("no error on handle_key_event");
            result.push(ret);
        }
        result
    }

    // Tests the basic registration flow: start the handler up, call a few of
    // its methods. The first test cases are more heavily commented as new
    // test approaches are introduced. Later tests will not have that much
    // explanation.
    #[fasync::run_singlethreaded(test)]
    async fn basic_registration() {
        // Unit under test.
        let handler = Rc::new(Shortcut2Impl::new());

        // The proxy/stream pair used for serving `Registry.{set_view,register_shortcut}` calls.
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();

        // Continuously handle registry calls, concurrently with this test.
        // Note, we're still running using a single thread.
        //
        // Handling the registry stream is the only continuously running task
        // that we must have running concurrent to the test. It should
        // therefore be enough to have this one running task only.
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        // We can make new ViewRefs at will in tests.
        let view_ref = new_view_ref();

        // Test will need to implement Listener so that it can receive shortcut
        // notifications.
        let (listener_client_end, _listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        // set_view is a fire-and-forget call. The expect() will report FIDL errors
        // only. It may not report anything until the next await.
        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        // Register two shortcuts: A, and A+B. No notifications yet, just trying out.
        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("registration went just fine");
        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID_2, vec!['a', 'b']))
            .await
            .expect("no FIDL errors")
            .expect("registration went just fine");

        // Just call some method so everything is exercised.
        let focus_chain = into_focus_chain(vec![view_ref]);
        handler.handle_focus_change(&focus_chain).await;
    }

    #[test_case(
        vec![cp_key('a')],
        vec![(cp_key('a'), KeyEventType::Pressed), (cp_key('a'), KeyEventType::Released)],
        vec![fs2::Handled::Handled, fs2::Handled::NotHandled]; "lowercase 'a'")]
    #[test_case(
        vec![cp_key('a')],
        vec![(cp_key('A'), KeyEventType::Pressed), (cp_key('A'), KeyEventType::Released)],
        vec![fs2::Handled::Handled, fs2::Handled::NotHandled]; "uppercase 'A'")]
    #[test_case(
        vec![np_key(NonPrintableKey::Tab)],
        vec![(np_key(NonPrintableKey::Tab), KeyEventType::Pressed), (np_key(NonPrintableKey::Tab), KeyEventType::Released)],
        vec![fs2::Handled::Handled, fs2::Handled::NotHandled]; "Tab")]
    #[fasync::run_singlethreaded(test)]
    async fn basic_notification(
        shortcut: Vec<KeyMeaning>,
        key_sequence: Vec<(KeyMeaning, finput3::KeyEventType)>,
        expected: Vec<fs2::Handled>,
    ) {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let view_ref = new_view_ref();

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut key_meanings_shortcut(SHORTCUT_ID, shortcut))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        // Just call some method so everything is exercised.
        let focus_chain = into_focus_chain(vec![view_ref]);
        handler.handle_focus_change(&focus_chain).await;

        // Run a task that listens to notifications for `listener_server_end`.
        // Anything that gets received will be put into a queue that is read
        // via `receiver`.
        let (_listener_task, mut receiver) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        let result = handle_key_sequence(&handler, key_sequence).await;
        pretty_assertions::assert_eq!(expected, result);
        pretty_assertions::assert_eq!(SHORTCUT_ID, receiver.next().await.unwrap());
    }

    // Verify that a shortcut registered out of order still gets triggered.
    #[fasync::run_singlethreaded(test)]
    async fn out_of_order_registration() {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let view_ref = new_view_ref();

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        // This shortcut is registered out of order.
        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['b']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        // Just call some method so everything is exercised.
        let focus_chain = into_focus_chain(vec![view_ref]);
        handler.handle_focus_change(&focus_chain).await;

        let (_listener_task, mut receiver) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        let result = handle_key_sequence(
            &handler,
            vec![(cp_key('a'), KeyEventType::Pressed), (cp_key('a'), KeyEventType::Released)],
        )
        .await;
        pretty_assertions::assert_eq!(
            vec![fs2::Handled::Handled, fs2::Handled::NotHandled],
            result
        );
        pretty_assertions::assert_eq!(SHORTCUT_ID, receiver.next().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn no_focus_no_notifications() {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let view_ref = new_view_ref();

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        // Empty focus chain - no clients should be notified.
        let focus_chain = into_focus_chain(vec![]);
        handler.handle_focus_change(&focus_chain).await;

        // Run a task that listens to notifications for `listener_server_end`.
        // Anything that gets received will be put into a queue that is read
        // via `receiver`.
        let (_listener_task, _) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        let result =
            handle_key_sequence(&handler, vec![(cp_key('a'), finput3::KeyEventType::Pressed)])
                .await;
        assert_eq!(vec![fs2::Handled::NotHandled], result);
    }

    // The following sequence does not activate the shortcut 'a'+'b':
    //
    // a  ____/"""""\_______________
    // b  _____________/"""""\______
    #[fasync::run_singlethreaded(test)]
    async fn key_chord_no_notification_for_inadequate_activation() {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let view_ref = new_view_ref();

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a', 'b']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        let focus_chain = into_focus_chain(vec![view_ref]);
        handler.handle_focus_change(&focus_chain).await;

        let (_listener_task, _) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('a'), finput3::KeyEventType::Pressed),
                (cp_key('a'), finput3::KeyEventType::Released),
                (cp_key('b'), finput3::KeyEventType::Pressed),
                (cp_key('b'), finput3::KeyEventType::Released),
            ],
        )
        .await;
        assert_eq!(
            vec![
                fs2::Handled::NotHandled,
                fs2::Handled::NotHandled,
                fs2::Handled::NotHandled,
                fs2::Handled::NotHandled,
            ],
            result
        );
    }

    // The following sequence activates the shortcut 'a'+'b':
    //
    // a  ___/"""""""\______
    // b  _______/"""""""\__
    //
    #[fasync::run_singlethreaded(test)]
    async fn key_chord_activation_a_plus_b() {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let view_ref = new_view_ref();

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        registry_proxy.set_view(&mut clone_view_ref(&view_ref), listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a', 'b']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        let focus_chain = into_focus_chain(vec![view_ref]);
        handler.handle_focus_change(&focus_chain).await;

        let (_listener_task, mut receiver) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('a'), finput3::KeyEventType::Pressed),
                (cp_key('b'), finput3::KeyEventType::Pressed),
                (cp_key('a'), finput3::KeyEventType::Released),
                (cp_key('b'), finput3::KeyEventType::Released),
            ],
        )
        .await;
        assert_eq!(
            vec![
                fs2::Handled::NotHandled,
                fs2::Handled::Handled,
                fs2::Handled::NotHandled,
                fs2::Handled::NotHandled,
            ],
            result
        );
        assert_eq!(SHORTCUT_ID, receiver.next().await.unwrap());
    }

    // Don't activate the shortcut 'a' if another key is released.
    //
    // a  """"""""""\___
    // b  """"""\_______
    //
    #[fasync::run_singlethreaded(test)]
    async fn key_chord_no_activation_on_release() {
        let handler = Rc::new(Shortcut2Impl::new());
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let handler_clone = handler.clone();
        let _handler_task = fasync::Task::local(async move {
            handler_clone.handle_registry_stream(registry_stream).await
        });

        let mut view_ref = new_view_ref();

        let focus_chain = into_focus_chain(vec![clone_view_ref(&view_ref)]);
        handler.handle_focus_change(&focus_chain).await;

        // Actuate shortcut keys before registering the shortcut.
        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('a'), finput3::KeyEventType::Pressed),
                (cp_key('b'), finput3::KeyEventType::Pressed),
            ],
        )
        .await;
        assert_eq!(vec![fs2::Handled::NotHandled, fs2::Handled::NotHandled,], result);

        let (listener_client_end, listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();

        registry_proxy.set_view(&mut view_ref, listener_client_end).unwrap();

        // Now that the shortcut is registered, no shortcut gets reported until
        // the next time around when the key is pressed.
        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("registration was a success");

        let (_listener_task, _receiver) =
            new_fake_listener(listener_server_end.into_stream().unwrap(), fs2::Handled::Handled);

        // While we released 'b' and only 'a' is still pressed, this is not a good enough reason
        // to activate the shortcut.
        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('b'), finput3::KeyEventType::Released),
                (cp_key('a'), finput3::KeyEventType::Released),
            ],
        )
        .await;
        assert_eq!(vec![fs2::Handled::NotHandled, fs2::Handled::NotHandled,], result);
    }

    // If a parent and a child are registered for the same shortcut, only the
    // parent will get it.
    #[fasync::run_singlethreaded(test)]
    async fn parent_catches_shortcut() {
        let handler = Rc::new(Shortcut2Impl::new());

        // Set up the shortcut registry connection.
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let parent_handler_clone = handler.clone();
        let _parent_handler_task = fasync::Task::local(async move {
            parent_handler_clone.handle_registry_stream(registry_stream).await
        });

        // Set up the parent-child focus chain.
        let mut parent_view_ref = new_view_ref();
        let mut child_view_ref = new_view_ref();
        let focus_chain = into_focus_chain(vec![
            clone_view_ref(&parent_view_ref),
            clone_view_ref(&child_view_ref),
        ]);
        handler.handle_focus_change(&focus_chain).await;

        let (parent_listener_client_end, parent_listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();
        registry_proxy.set_view(&mut parent_view_ref, parent_listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("parent registration was a success");

        // Register the child for the same shortcut as the parent.
        let (child_listener_client_end, child_listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();
        registry_proxy.set_view(&mut child_view_ref, child_listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("child registration was a success");

        // Start fake listeners for both the parent and the child.
        let (_parent_listener_task, mut parent_receiver) = new_fake_listener(
            parent_listener_server_end.into_stream().unwrap(),
            fs2::Handled::Handled,
        );

        let (_child_listener_task, _child_receiver) = new_fake_listener(
            child_listener_server_end.into_stream().unwrap(),
            fs2::Handled::Handled,
        );

        // Send the shortcut trigger.
        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('a'), finput3::KeyEventType::Pressed),
                (cp_key('a'), finput3::KeyEventType::Released),
            ],
        )
        .await;
        // The shortcut has been handled; but only the parent handled it.
        assert_eq!(vec![fs2::Handled::Handled, fs2::Handled::NotHandled], result);
        assert_eq!(SHORTCUT_ID, parent_receiver.next().await.unwrap());
    }

    // If a parent and a child are registered for the same shortcut, but we set
    // the parent to ignore the shortcut, then the child will get notified.
    #[fasync::run_singlethreaded(test)]
    async fn parent_passes_shorcut_child_catches() {
        let handler = Rc::new(Shortcut2Impl::new());

        // Set up the shortcut registry connection.
        let (registry_proxy, registry_stream) =
            endpoints::create_proxy_and_stream::<fs2::RegistryMarker>().unwrap();
        let parent_handler_clone = handler.clone();
        let _parent_handler_task = fasync::Task::local(async move {
            parent_handler_clone.handle_registry_stream(registry_stream).await
        });

        // Set up the parent-child focus chain.
        let mut parent_view_ref = new_view_ref();
        let mut child_view_ref = new_view_ref();
        let focus_chain = into_focus_chain(vec![
            clone_view_ref(&parent_view_ref),
            clone_view_ref(&child_view_ref),
        ]);
        handler.handle_focus_change(&focus_chain).await;

        let (parent_listener_client_end, parent_listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();
        registry_proxy.set_view(&mut parent_view_ref, parent_listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("parent registration was a success");

        // Register the child for the same shortcut as the parent.
        let (child_listener_client_end, child_listener_server_end) =
            endpoints::create_endpoints::<fs2::ListenerMarker>().unwrap();
        registry_proxy.set_view(&mut child_view_ref, child_listener_client_end).unwrap();

        registry_proxy
            .register_shortcut(&mut codepoint_shortcut(SHORTCUT_ID, vec!['a']))
            .await
            .expect("no FIDL errors")
            .expect("child registration was a success");

        // Instruct the parent to return NotHandled. This should cause the
        // child to get the shortcut.
        let (_parent_listener_task, _parent_receiver) = new_fake_listener(
            parent_listener_server_end.into_stream().unwrap(),
            fs2::Handled::NotHandled,
        );

        let (_child_listener_task, mut child_receiver) = new_fake_listener(
            child_listener_server_end.into_stream().unwrap(),
            fs2::Handled::Handled,
        );

        // Send the shortcut trigger.
        let result = handle_key_sequence(
            &handler,
            vec![
                (cp_key('a'), finput3::KeyEventType::Pressed),
                (cp_key('a'), finput3::KeyEventType::Released),
            ],
        )
        .await;
        // The shortcut has been handled; but only the parent handled it.
        assert_eq!(vec![fs2::Handled::Handled, fs2::Handled::NotHandled], result);
        assert_eq!(SHORTCUT_ID, child_receiver.next().await.unwrap());
    }

    #[test_case(vec![KeyMeaning::Codepoint('\t' as u32)]; "Tab") ]
    #[test_case(vec![KeyMeaning::Codepoint('\n' as u32)]; "Newline") ]
    #[test_case(vec![KeyMeaning::Codepoint(0x1f)]; "Last") ]
    fn test_invalid_shortcuts(keys: Vec<KeyMeaning>) {
        let shortcut = fs2::Shortcut { id: 42, key_meanings: keys, options: fs2::Options::EMPTY };
        let result = validate_shortcut(&shortcut);
        assert!(result.is_err(), "result: {:?}", &result);
    }
}
