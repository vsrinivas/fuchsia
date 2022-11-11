// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use appkit::*;
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3::KeyEventStatus;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_views as ui_views;
use futures::{channel::mpsc::UnboundedReceiver, Stream, StreamExt};
use indexmap::IndexMap;
use num::FromPrimitive;
use tracing::{debug, error, warn};

use crate::shortcuts::{all_shortcuts, ShortcutAction};

// Background color: Fuchsia Green 04 [Opacity 0.3].
const BACKGROUND_COLOR: ui_comp::ColorRgba =
    ui_comp::ColorRgba { red: 0.075, green: 0.698, blue: 0.58, alpha: 0.3 };

// Height of the app launcher used to subtract from fullscreen height for child views.
const APP_LAUNCHER_HEIGHT: u32 = 40;

/// Defines an enumeration of events used to implement the window manager.
#[derive(Debug)]
pub enum WMEvent {}

/// Defines a type for holding the state of the window manager.
///
/// The window manager holds child views from applications provided to it by GraphicalPresenter and
/// shell views from components launched by itself. It places them in a certain display order:
///
///              Bottom layer: -----window manager background-----
///
///              Middle layer: -----     child views         -----
///
///              Top layer:    -----     shell views         -----
///
pub(crate) struct WindowManager {
    // The event sender used to route events.
    event_sender: EventSender<WMEvent>,
    // The main application [Window] of the Window Manager.
    window: Window<WMEvent>,
    // The map of all launched [ChildView] views by their [ChildViewId].
    child_views: IndexMap<ChildViewId, ChildView<WMEvent>>,
    // The map of all incoming [ChildView]s views by their [ChildViewId] that are not assigned a
    // flatland transform yet.
    pending_child_views: IndexMap<ChildViewId, ChildView<WMEvent>>,
    // The flatland transforms that hold the [ChildView]'s content. The transforms always stay in
    // back-to-front order, so when child_views gets rearranged, the child-to-transform mapping
    // changes.
    child_view_transforms: Vec<ui_comp::TransformId>,
    // The flatland content id for background color on the root transform.
    background_content: ui_comp::ContentId,
    // The layer (parent transform) for holding all child views.
    child_views_layer: ui_comp::TransformId,
    // The layer (parent transform) for holding all shell component views.
    _shell_views_layer: ui_comp::TransformId,
    // The current width of the application window.
    width: u32,
    // The current height of the application window.
    height: u32,
    // The flag to indicate that WindowManager is ready after receiving the first resize event.
    is_ready: bool,
    // The view specs to present ChildViews that arrived before the main window is created.
    pending_child_view_present_specs: Vec<ViewSpecHolder>,
}

impl WindowManager {
    pub fn new(
        event_sender: EventSender<WMEvent>,
        view_creation_token: ui_views::ViewCreationToken,
        pending_child_view_present_specs: Vec<ViewSpecHolder>,
        protocol_connector: Box<dyn ProtocolConnector>,
    ) -> Result<WindowManager, Error> {
        let mut window = Window::new(event_sender.clone())
            .with_view_creation_token(view_creation_token)
            .with_protocol_connector(protocol_connector.box_clone());
        window.create_view()?;

        window.register_shortcuts(all_shortcuts());

        let flatland = window.get_flatland();
        let mut background = window.next_content_id();
        flatland.create_filled_rect(&mut background)?;
        let mut root_transform_id = window.get_root_transform_id();
        flatland.set_content(&mut root_transform_id, &mut background)?;

        let mut child_views_layer = window.next_transform_id();
        flatland.create_transform(&mut child_views_layer)?;
        flatland.add_child(&mut root_transform_id, &mut child_views_layer)?;

        let mut shell_views_layer = window.next_transform_id();
        flatland.create_transform(&mut shell_views_layer)?;
        flatland.add_child(&mut root_transform_id, &mut shell_views_layer)?;

        Ok(WindowManager {
            event_sender,
            window,
            child_views: IndexMap::new(),
            pending_child_views: IndexMap::new(),
            child_view_transforms: vec![],
            background_content: background,
            child_views_layer,
            _shell_views_layer: shell_views_layer,
            width: 0,
            height: 0,
            is_ready: false,
            pending_child_view_present_specs,
        })
    }

    // Release flatland resources.
    async fn release(&mut self) -> Result<(), Error> {
        let flatland = self.window.get_flatland();

        // Drop child_views and the associated child_view_transforms.
        while let Some((child_view_id, _)) = self.child_views.first() {
            self.remove_child_view(*child_view_id)?;
        }

        let mut root_transform_id = self.window.get_root_transform_id();

        flatland.remove_child(&mut root_transform_id, &mut self._shell_views_layer)?;
        flatland.release_transform(&mut self._shell_views_layer)?;

        flatland.remove_child(&mut root_transform_id, &mut self.child_views_layer)?;
        flatland.release_transform(&mut self.child_views_layer)?;

        flatland.release_filled_rect(&mut self.background_content)?;
        flatland.clear()?;

        self.window.redraw();

        self.window.close()?;

        Ok(())
    }

    pub async fn run(
        &mut self,
        mut receiver: impl Stream<Item = Event<WMEvent>> + std::marker::Unpin,
    ) -> Result<(), Error> {
        while let Some(event) = receiver.next().await {
            debug!("{:?}", event);
            match event {
                Event::SystemEvent { event } => match event {
                    // Ignore subsequent ViewProvider::CreateView requests. We only allow one
                    // instance of WindowManager to exist.
                    SystemEvent::ViewCreationToken { .. } => {
                        warn!("Received another ViewProvider::CreateView2 request. Ignoring.");
                    }

                    // Create a [ChildView] from GraphicalPresenter's ViewSpec.
                    SystemEvent::PresentViewSpec { view_spec_holder } => {
                        if self.is_ready {
                            let child_view = self.window.create_child_view(
                                view_spec_holder,
                                self.width,
                                self.height - APP_LAUNCHER_HEIGHT,
                                self.event_sender.clone(),
                            )?;
                            self.window.redraw();

                            // Save child_view to pending_child_views until it's component has
                            // finished loading and rendered a frame. The child view can then be
                            // attached to the display tree in [ChildViewEvent::Available].
                            self.pending_child_views.insert(child_view.id(), child_view);
                        } else {
                            self.pending_child_view_present_specs.push(view_spec_holder);
                        }
                    }
                },

                Event::WindowEvent { event: window_event, .. } => {
                    match window_event {
                        WindowEvent::Resized { width, height, .. } => {
                            self.resize(width, height)?;
                            self.window.redraw();

                            self.is_ready = true;
                            // Handle any pending childview specs.
                            for view_spec_holder in self.pending_child_view_present_specs.drain(..)
                            {
                                self.event_sender
                                    .send(Event::SystemEvent {
                                        event: SystemEvent::PresentViewSpec { view_spec_holder },
                                    })
                                    .expect("failed to send SystemEvent::PresentViewSpec");
                            }
                        }

                        WindowEvent::Focused { focused } => {
                            if focused {
                                self.refocus();
                            }
                        }

                        WindowEvent::Keyboard { responder, .. } => {
                            // Keep keyboard channel happy by always responding.
                            responder.send(KeyEventStatus::NotHandled)?;
                        }

                        WindowEvent::Shortcut { id, responder } => {
                            // Active window is always on top and last in display list. Next
                            // window will be at index 0, whereas previous window will be the
                            // second to last index. Switching to those window will make them
                            // the top window of the display list.
                            if let Some(action) = ShortcutAction::from_u32(id) {
                                match action {
                                    ShortcutAction::FocusNext => {
                                        self.focus_next()?;
                                        self.layout()?;
                                        self.refocus();
                                    }

                                    ShortcutAction::FocusPrev => {
                                        self.focus_previous()?;
                                        self.layout()?;
                                        self.refocus();
                                    }

                                    ShortcutAction::Close => {
                                        if let Some((child_view_id, _)) = self.child_views.last() {
                                            self.remove_child_view(*child_view_id)?;
                                            self.layout()?;
                                            self.refocus();
                                        }
                                    }
                                }

                                self.window.redraw();
                            } else {
                                error!("Received unknown shortcut invocation: {:?}", id);
                            }

                            responder.send(ui_shortcut2::Handled::Handled)?;
                        }
                        WindowEvent::NeedsRedraw { .. }
                        | WindowEvent::Closed { .. }
                        | WindowEvent::Pointer { .. } => {}
                    }
                }

                Event::ChildViewEvent { child_view_id, event, .. } => match event {
                    ChildViewEvent::Available => {
                        self.add_child_view(child_view_id)?;
                        self.window.redraw();
                    }

                    ChildViewEvent::Attached { view_ref } => {
                        if let Some(child_view) = self.child_views.get_mut(&child_view_id) {
                            child_view.set_view_ref(view_ref);
                            self.window.request_focus(
                                child_view.get_view_ref().expect("Failed to get child view ref"),
                            );
                        }
                    }

                    ChildViewEvent::Detached | ChildViewEvent::Dismissed => {
                        self.remove_child_view(child_view_id)?;
                        self.layout()?;
                        self.refocus();
                        self.window.redraw();
                    }
                },

                Event::Exit => break,

                Event::Init | Event::DeviceEvent | Event::UserEvent(..) => {}
            }
        }

        self.release().await?;

        Ok(())
    }

    // Resizes the background color and child view contents.
    fn resize(&mut self, width: u32, height: u32) -> Result<(), Error> {
        self.width = width;
        self.height = height;

        // Resize the background.
        let flatland = self.window.get_flatland();
        flatland.set_solid_fill(
            &mut self.background_content,
            &mut BACKGROUND_COLOR.clone(),
            // TODO(fxbug.dev/110653): Mysteriously, Scenic blows up when
            // you make a rectangle the size of the viewport, under very
            // specific circumstances. When that bug is fixed, change this
            // to just width and height.
            &mut fmath::SizeU {
                width: width.saturating_sub(1).clamp(1, u32::MAX),
                height: height.saturating_sub(1).clamp(1, u32::MAX),
            },
        )?;

        // Resize child views.
        for child_view in self.child_views.values_mut() {
            child_view.set_size(width, height - APP_LAUNCHER_HEIGHT)?;
        }

        Ok(())
    }

    // Layout all child view transforms and set their content to a child view.
    //
    // The application holds a list of flatland transforms equal to the number of child views. Since
    // Flatland does not provide an API to change the order of transforms, we instead set their
    // contents in a display order, back-to-front to a child_view in [child_views].
    fn layout(&mut self) -> Result<(), Error> {
        assert_eq!(self.child_views.len(), self.child_view_transforms.len());

        for ((_, child_view), child_view_transform) in
            self.child_views.iter().zip(self.child_view_transforms.iter())
        {
            self.window.set_content(child_view_transform.clone(), child_view.get_content_id());
        }

        Ok(())
    }

    // Requests focus to the top-most child view.
    fn refocus(&mut self) {
        if let Some((_, child_view)) = self.child_views.last() {
            if let Some(view_ref) = child_view.get_view_ref() {
                self.window.request_focus(view_ref);
            }
        }
    }

    // Move the child_view from pending_child_views to child_views and create a transform to hold
    // it's content.
    fn add_child_view(&mut self, child_view_id: ChildViewId) -> Result<(), Error> {
        if let Some(child_view) = self.pending_child_views.remove(&child_view_id) {
            let content_id = child_view.get_content_id();
            let child_view_transform = self.create_child_view_transform(content_id)?;
            self.child_views.insert(child_view_id, child_view);
            self.child_view_transforms.push(child_view_transform);
        } else {
            warn!("Cannot find a child_view in pending_child_views.");
        }

        Ok(())
    }

    // Removes a child_view from [child_views] and a transform from [child_view_transforms] to keep
    // their 1-1 association.
    fn remove_child_view(&mut self, child_view_id: ChildViewId) -> Result<(), Error> {
        if self.child_views.contains_key(&child_view_id) {
            assert!(self.child_views.len() == self.child_view_transforms.len());
            // We pop a transform [child_view_transforms] to keep them in sync with [child_views].
            // It does not matter where we remove it from. We re-associate child views to transform
            // in [layout].
            let mut child_view_transform =
                self.child_view_transforms.pop().expect("Failed to find child_view_transform");

            let flatland = self.window.get_flatland();
            flatland.remove_child(&mut self.child_views_layer, &mut child_view_transform)?;
            flatland.release_transform(&mut child_view_transform)?;

            self.child_views.remove(&child_view_id);
        } else if self.pending_child_views.contains_key(&child_view_id) {
            self.pending_child_views.remove(&child_view_id);
        }

        Ok(())
    }

    // Creates a transform to hold the child view's content. We maintain a 1-1 association of a
    // child view's content to a transform. The transform is added to the [child_views_layer].
    fn create_child_view_transform(
        &mut self,
        mut content_id: ui_comp::ContentId,
    ) -> Result<ui_comp::TransformId, Error> {
        let flatland = self.window.get_flatland();
        let mut child_view_transform = self.window.next_transform_id();
        flatland.create_transform(&mut child_view_transform)?;
        flatland.add_child(&mut self.child_views_layer, &mut child_view_transform)?;
        flatland.set_content(&mut child_view_transform, &mut content_id)?;

        Ok(child_view_transform)
    }

    // Moves a child view to the top of the display list. This can also be used to implement
    // "Focus Next", where the next view to a top view is the view at index 0.
    fn bring_to_top(&mut self, index: usize) -> Result<(), Error> {
        if self.child_view_transforms.len() <= 1 || index >= self.child_view_transforms.len() {
            return Ok(());
        }

        if let Some((child_view_id, child_view)) = self.child_views.shift_remove_index(index) {
            self.child_views.insert(child_view_id, child_view);
        }

        self.refocus();

        Ok(())
    }

    /// Focus to the next-from-last child view, which is at index 0, in the display list.
    fn focus_next(&mut self) -> Result<(), Error> {
        self.bring_to_top(0)
    }

    /// Focus to the second-to-last child view in the display list.
    fn focus_previous(&mut self) -> Result<(), Error> {
        if self.child_view_transforms.len() <= 1 {
            return Ok(());
        }

        // We drain all elements except the last, and then re-add them back.
        let rest: Vec<_> = self.child_views.drain(..self.child_views.len() - 1).collect();
        for (child_view_id, child_view) in rest {
            self.child_views.insert(child_view_id, child_view);
        }

        self.refocus();

        Ok(())
    }
}

/// Returns the first ViewCreationToken received by ViewProvider service and all requests to
/// present a view to the GraphicalPresenter service, while consuming all other events.
pub async fn get_first_view_creation_token(
    receiver: &mut UnboundedReceiver<Event<WMEvent>>,
) -> (ui_views::ViewCreationToken, Vec<ViewSpecHolder>) {
    let mut view_spec_holders = vec![];
    while let Some(event) = receiver.next().await {
        match event {
            Event::Init => continue,
            Event::SystemEvent { event } => match event {
                SystemEvent::ViewCreationToken { token } => {
                    return (token, view_spec_holders);
                }
                SystemEvent::PresentViewSpec { view_spec_holder } => {
                    view_spec_holders.push(view_spec_holder);
                }
            },
            _ => {
                warn!("Ignoring {:?} while waiting for ViewProvider::CreateView2 request", event)
            }
        }
    }
    unreachable!()
}
