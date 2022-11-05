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
use futures::{channel::mpsc::UnboundedReceiver, StreamExt};
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
pub(crate) enum WMEvent {}

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
}

impl WindowManager {
    fn new(
        event_sender: EventSender<WMEvent>,
        token: ui_views::ViewCreationToken,
    ) -> Result<WindowManager, Error> {
        let mut window = Window::new(event_sender.clone()).with_view_creation_token(token);
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
            window,
            child_views: IndexMap::new(),
            pending_child_views: IndexMap::new(),
            child_view_transforms: vec![],
            background_content: background,
            child_views_layer,
            _shell_views_layer: shell_views_layer,
            width: 0,
            height: 0,
        })
    }

    // Release flatland resources.
    async fn release(&mut self) -> Result<(), Error> {
        let flatland = self.window.get_flatland();

        // Drop child_views and the associated child_view_transforms.
        while let Some((child_view_id, child_view)) = self.child_views.first() {
            let mut child_view_content_id = child_view.get_content_id();
            self.remove_child_view(*child_view_id)?;
            flatland.release_viewport(&mut child_view_content_id).await?;
        }

        let mut root_transform_id = self.window.get_root_transform_id();

        flatland.remove_child(&mut root_transform_id, &mut self._shell_views_layer)?;
        flatland.release_transform(&mut self._shell_views_layer)?;

        flatland.remove_child(&mut root_transform_id, &mut self.child_views_layer)?;
        flatland.release_transform(&mut self.child_views_layer)?;

        flatland.release_filled_rect(&mut self.background_content)?;
        self.window.redraw();

        Ok(())
    }

    pub async fn run(
        event_sender: EventSender<WMEvent>,
        mut receiver: UnboundedReceiver<Event<WMEvent>>,
    ) -> Result<(), Error> {
        // This is THE window manager instance. It is created after receiving a ViewCreationToken
        // from a ViewProvider request stream through [SystemEvent::ViewCreationToken].
        let mut wm: Option<WindowManager> = None;

        while let Some(event) = receiver.next().await {
            debug!("{:?}", event);
            match event {
                Event::SystemEvent { event } => match event {
                    // Create the application from ViewProvider's ViewCreationToken.
                    SystemEvent::ViewCreationToken { token } => {
                        wm = Some(WindowManager::new(event_sender.clone(), token)?);
                    }

                    // Create a [ChildView] from GraphicalPresenter's ViewSpec.
                    SystemEvent::PresentViewSpec { view_spec_holder } => {
                        if let Some(wm) = wm.as_mut() {
                            let child_view = wm.window.create_child_view(
                                view_spec_holder,
                                wm.width,
                                wm.height - APP_LAUNCHER_HEIGHT,
                                event_sender.clone(),
                            )?;
                            // Save child_view to pending_child_views until it's component has
                            // finished loading and rendered a frame. The child view is available
                            // to be attached to the display tree in [ChildViewEvent::Available].
                            wm.pending_child_views.insert(child_view.id(), child_view);
                        } else {
                            // TODO(https://fxbug.dev/113709): Consider queueing these requests.
                            warn!("WM window does not exist. Ignoring GraphicalPresenter requests");
                        }
                    }
                },

                Event::WindowEvent { event: window_event, .. } => {
                    let wm = wm.as_mut().expect("wm instance should exist before WindowEvents");

                    match window_event {
                        WindowEvent::Resized { width, height, .. } => {
                            wm.resize(width, height)?;
                            wm.window.redraw();
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
                                        wm.focus_next()?;
                                        wm.layout()?;
                                        wm.refocus();
                                    }

                                    ShortcutAction::FocusPrev => {
                                        wm.focus_previous()?;
                                        wm.layout()?;
                                        wm.refocus();
                                    }

                                    ShortcutAction::Close => {
                                        if let Some((child_view_id, _)) = wm.child_views.last() {
                                            wm.remove_child_view(*child_view_id)?;
                                            wm.layout()?;
                                            wm.refocus();
                                        }
                                    }
                                }

                                wm.window.redraw();
                            } else {
                                error!("Received unknown shortcut invocation: {:?}", id);
                            }

                            responder.send(ui_shortcut2::Handled::Handled)?;
                        }
                        WindowEvent::NeedsRedraw { .. }
                        | WindowEvent::Focused { .. }
                        | WindowEvent::Closed { .. }
                        | WindowEvent::Pointer { .. } => {}
                    }
                }

                Event::ChildViewEvent { child_view_id, event, .. } => {
                    let wm = wm.as_mut().expect("wm instance should exist before WindowEvents");

                    match event {
                        ChildViewEvent::Available => {
                            wm.add_child_view(child_view_id)?;
                            wm.window.redraw();
                        }

                        ChildViewEvent::Attached { view_ref } => {
                            if let Some(child_view) = wm.child_views.get_mut(&child_view_id) {
                                child_view.set_view_ref(view_ref);
                                wm.window.request_focus(
                                    child_view
                                        .get_view_ref()
                                        .expect("Failed to get child view ref"),
                                );
                            }
                        }

                        ChildViewEvent::Detached | ChildViewEvent::Dismissed => {
                            wm.remove_child_view(child_view_id)?;
                            wm.layout()?;
                            wm.refocus();
                            wm.window.redraw();
                        }
                    }
                }

                Event::Exit => break,

                Event::Init | Event::DeviceEvent | Event::UserEvent(..) => {}
            }
        }

        if let Some(mut wm) = wm {
            wm.release().await?;
        }

        receiver.close();

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
