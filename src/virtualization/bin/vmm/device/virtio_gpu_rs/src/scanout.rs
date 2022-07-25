// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resource::Resource2D,
    crate::wire,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_element::{GraphicalPresenterMarker, ViewSpec},
    fidl_fuchsia_math as fmath,
    fidl_fuchsia_ui_composition::{
        BlendMode, ColorRgba, ContentId, FlatlandEvent, FlatlandMarker, FlatlandProxy,
        ImageProperties, ParentViewportWatcherMarker, PresentArgs, TransformId, ViewBoundProtocols,
    },
    fidl_fuchsia_ui_views::{ViewCreationToken, ViewIdentityOnCreation, ViewportCreationToken},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic::ViewRefPair,
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::cell::Cell,
    std::collections::HashMap,
    std::rc::Rc,
};

/// ContentId to use to detach content with a transform.
const NULL_CONTENT_ID: ContentId = ContentId { value: 0 };

fn new_transform_id(counter: &mut u64) -> TransformId {
    let value = *counter;
    *counter += 1;
    TransformId { value }
}

fn new_content_id(counter: &mut u64) -> ContentId {
    let value = *counter;
    *counter += 1;
    ContentId { value }
}

fn create_view_creation_tokens() -> (ViewCreationToken, ViewportCreationToken) {
    let (c1, c2) =
        zx::Channel::create().expect("Failed to create zx::channel for ViewCreationTokens");
    let view_token = ViewCreationToken { value: c1 };
    let viewport_token = ViewportCreationToken { value: c2 };
    (view_token, viewport_token)
}

pub struct Scanout {
    flatland: FlatlandProxy,
    next_flatland_id: u64,
    imported_resources: HashMap<u32, ContentId>,
    content_transform_id: TransformId,
    present_credits: Rc<Cell<u64>>,
}

impl Scanout {
    /// Creates a new `Scanout`.
    ///
    /// The creation returns once the backing view for the `Scanout` has been created and presented
    /// to the user.
    pub async fn create() -> Result<Self, Error> {
        let flatland =
            connect_to_protocol::<FlatlandMarker>().context("error connecting to Flatland")?;

        // Create a view.
        let (_, parent_viewport_watcher_request) = create_proxy::<ParentViewportWatcherMarker>()
            .context("failed to create ParentViewportWatcherProxy")?;
        let (mut view_token, viewport_token) = create_view_creation_tokens();
        let viewref_pair = ViewRefPair::new()?;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        let mut view_identity = ViewIdentityOnCreation::from(viewref_pair);
        let view_bound_protocols = ViewBoundProtocols { ..ViewBoundProtocols::EMPTY };
        flatland
            .create_view2(
                &mut view_token,
                &mut view_identity,
                view_bound_protocols,
                parent_viewport_watcher_request,
            )
            .context("Failed to create_view")?;

        // Display with graphical presenter.
        let view_spec = ViewSpec {
            viewport_creation_token: Some(viewport_token),
            view_ref: Some(view_ref_dup),
            ..ViewSpec::EMPTY
        };
        let graphical_presenter = connect_to_protocol::<GraphicalPresenterMarker>()
            .context("failed to connect to GraphicalPresenter service")?;
        graphical_presenter
            .present_view(view_spec, None, None)
            .await?
            .map_err(|e| anyhow!("{:?}", e))?;

        // Setup our root transform. This will be empty until we get a set_scanout with a non-zero
        // resource_id. We start at 1 because 0 is not a valid id.
        let mut next_flatland_id = 1u64;
        let root_transform_id = new_transform_id(&mut next_flatland_id);
        flatland.create_transform(&mut root_transform_id.clone())?;

        // Create a root transform node that is just a solid color.
        let root_content_id = new_content_id(&mut next_flatland_id);
        flatland.create_filled_rect(&mut root_content_id.clone())?;
        flatland.set_solid_fill(
            &mut root_content_id.clone(),
            &mut ColorRgba { red: 0.5, green: 0.5, blue: 0.5, alpha: 1.0 },
            &mut fmath::SizeU { width: u32::MAX, height: u32::MAX },
        )?;
        flatland.set_content(&mut root_transform_id.clone(), &mut root_content_id.clone())?;
        flatland.set_root_transform(&mut root_transform_id.clone())?;

        // Add a content node that will hold any attached resources.
        let content_transform_id = new_transform_id(&mut next_flatland_id);
        flatland.create_transform(&mut content_transform_id.clone())?;
        flatland.add_child(&mut root_transform_id.clone(), &mut content_transform_id.clone())?;

        // Perform an initial present.
        flatland.present(PresentArgs::EMPTY)?;

        // Read events from the event streams to track the present credit count. We can only call
        // present if our counter is non-zero.
        //
        // Note that we initialize to 0 here because we consumed the initial present credit
        // presenting the initial view.
        let credit_counter = Rc::new(Cell::new(0));
        let present_credits = credit_counter.clone();
        let mut event_stream = flatland.take_event_stream();
        fasync::Task::local(async move {
            while let Some(Ok(event)) = event_stream.next().await {
                match &event {
                    FlatlandEvent::OnNextFrameBegin { values } => {
                        if let Some(credits) = values.additional_present_credits {
                            credit_counter.set(credit_counter.get() + credits as u64);
                        }
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(Self {
            flatland,
            next_flatland_id,
            imported_resources: HashMap::new(),
            content_transform_id,
            present_credits,
        })
    }

    pub fn present(&mut self) -> Result<(), Error> {
        let credits = self.present_credits.get();
        // TODO(fxbug.dev/102870): For now we just skip the present if we don't have any credits.
        // We need to improve this so that once we do get a credit we'll eventually flush any
        // changes to screen.
        if credits > 0 {
            self.flatland.present(PresentArgs::EMPTY)?;
            self.present_credits.set(credits - 1);
        }
        Ok(())
    }

    pub fn set_resource<'a>(
        &mut self,
        _cmd: &wire::VirtioGpuSetScanout,
        resource: Option<&Resource2D<'a>>,
    ) -> Result<(), Error> {
        if let Some(resource) = resource {
            // TODO(fxbug.dev/102870): We need to set the source region from the rect in the
            // VirtioGpuSetScanout command.
            //
            // In practice for single scanout configurations drivers usually provide precisely
            // sized resources.
            let content_id = self.get_or_create_resource(resource)?;
            self.flatland
                .set_content(&mut self.content_transform_id.clone(), &mut content_id.clone())?;
        } else {
            self.flatland.set_content(
                &mut self.content_transform_id.clone(),
                &mut NULL_CONTENT_ID.clone(),
            )?;
        }
        Ok(())
    }

    fn get_or_create_resource<'a>(
        &mut self,
        resource: &Resource2D<'a>,
    ) -> Result<ContentId, Error> {
        if let Some(content_id) = self.imported_resources.get(&resource.id()) {
            Ok(*content_id)
        } else {
            let content_id = self.import_resource(resource)?;
            self.imported_resources.insert(resource.id(), content_id);
            Ok(content_id)
        }
    }

    fn import_resource<'a>(&mut self, resource: &Resource2D<'a>) -> Result<ContentId, Error> {
        let content_id = new_content_id(&mut self.next_flatland_id);
        let image_props = ImageProperties {
            size: Some(fmath::SizeU { width: resource.width(), height: resource.height() }),
            ..ImageProperties::EMPTY
        };

        let mut import_token = resource
            .import_token()
            .ok_or_else(|| anyhow!("No import_token available for resource VMO"))?;

        self.flatland
            .create_image(&mut content_id.clone(), &mut import_token, 0, image_props)
            .context("Failed to create image")?;
        let mut size = fmath::SizeU { width: resource.width(), height: resource.height() };
        self.flatland.set_image_destination_size(&mut content_id.clone(), &mut size)?;
        // Src blend mode will prevent any alpha blending with the background.
        self.flatland.set_image_blending_function(&mut content_id.clone(), BlendMode::Src)?;
        Ok(content_id)
    }
}
