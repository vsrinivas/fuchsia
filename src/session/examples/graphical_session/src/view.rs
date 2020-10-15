// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_images as images,
    fidl_fuchsia_ui_gfx::{ColorRgb, ColorRgba, DisplayInfo, Vec3},
    fuchsia_scenic::{
        AmbientLight, Camera, DisplayCompositor, EntityNode, HostImage, HostMemory, Layer,
        LayerStack, Material, Rectangle, Renderer, Scene, SessionPtr, ShapeNode,
    },
    fuchsia_zircon::Time,
    png,
    std::f32::consts::PI,
    std::fs::File,
    std::sync::{Arc, Mutex},
};

use crate::graphics_util;

/// A `View` represents the content which is displayed in the Whale Session.
///
/// The view will create all the relevant Scenic resources in a given Scenic session.
#[allow(dead_code)]
pub struct View {
    /// The `DisplayCompositor` which the `View` creates in the Scenic session.
    compositor: DisplayCompositor,

    /// The `LayerStack` which the `View` creates in the Scenic session.
    layer_stack: LayerStack,

    /// The `Layer` which the `View` creates in the Scenic session.
    layer: Layer,

    /// The `Renderer` which the `View` creates in the Scenic session.
    renderer: Renderer,

    /// The `Scene` which the `View` creates in the Scenic session.
    scene: Scene,

    /// The `Camera` which the `View` creates in the Scenic session.
    camera: Camera,

    /// The `ShapeNode` which draws the background content of the view.
    bg_shape: ShapeNode,

    /// The `ShapeNode` which draws the whale image in the view.
    rect_shape: ShapeNode,
}

/// A `Context` contains all the information required to update the Scenic session and drive
/// the associated animations etc.
pub struct Context {
    /// The time at which the whale animation started, used to calculate the whale rotation.
    pub start_time: Time,

    /// The current presentation time, used to calculate the whale rotation.
    pub presentation_time: Time,

    /// The Scenic session associated with the `View`.
    pub session: SessionPtr,

    /// Information about the display associated with the Scenic session.
    pub display_info: DisplayInfo,
}

/// A `ContextPtr` is used to share `Context` in the `App`.
pub type ContextPtr = Arc<Mutex<Context>>;

impl Context {
    /// Creates a new `Context` for the given Scenic session, wrapped in a `ContextPtr`.
    ///
    /// # Parameters
    /// - `session`: The `SessionPtr` to the Scenic session.
    /// - `display_info`: The Scenic session's display info.
    ///
    /// # Returns
    /// A `Context` instance wrapped in an `Arc` and `Mutex`.
    pub fn new_ptr(session: SessionPtr, display_info: DisplayInfo) -> ContextPtr {
        Arc::new(Mutex::new(Context {
            presentation_time: Time::get_monotonic(),
            start_time: Time::get_monotonic(),
            session,
            display_info,
        }))
    }
}

impl View {
    /// Creates a new `View` with the associated `context`.
    ///
    /// # Parameters
    /// - `context`: The `Context` used to set up the Scenic resources.
    ///
    /// # Returns
    /// A `View` if setup was successful, including loading images etc., otherwise an `Error`.
    pub fn new(context: ContextPtr) -> Result<View, Error> {
        let ctx = context.lock().unwrap();
        let session = &ctx.session;
        let (width, height) =
            (ctx.display_info.width_in_px as f32, ctx.display_info.height_in_px as f32);

        let compositor = DisplayCompositor::new(session.clone());
        let layer_stack = LayerStack::new(session.clone());
        let layer = Layer::new(session.clone());
        let renderer = Renderer::new(session.clone());
        let scene = Scene::new(session.clone());
        let camera = Camera::new(session.clone(), &scene);

        compositor.set_layer_stack(&layer_stack);
        layer_stack.add_layer(&layer);
        layer.set_size(width, height);
        layer.set_renderer(&renderer);
        renderer.set_camera(&camera);

        let ambient_light = AmbientLight::new(session.clone());
        ambient_light.set_color(ColorRgb { red: 1.0, green: 1.0, blue: 1.0 });
        scene.add_ambient_light(&ambient_light);

        let root_node = EntityNode::new(session.clone());
        scene.add_child(&root_node);

        // Setup the background
        let bg_shape = View::make_background(session.clone(), width, height);
        root_node.add_child(&bg_shape);

        // Create the whale shape
        let rect_shape = View::make_whale_shape(session.clone())?;
        root_node.add_child(&rect_shape);

        Ok(View { compositor, layer_stack, layer, renderer, scene, camera, bg_shape, rect_shape })
    }

    /// Creates `ShapeNode` representing the background of the `View`.
    ///
    /// # Parameters:
    /// - `session`: The Scenic session to create resources in.
    /// - `width`: The intended width of the background, in px.
    /// - `height`: The intended height of the background, in px.
    ///
    /// # Returns
    /// A `ShapeNode` representing the background.
    fn make_background(session: SessionPtr, width: f32, height: f32) -> ShapeNode {
        let bg_material = Material::new(session.clone());
        bg_material.set_color(ColorRgba { red: 255, green: 255, blue: 255, alpha: 255 });
        let bg_rect = Rectangle::new(session.clone(), width, height);
        let bg_shape = ShapeNode::new(session.clone());
        bg_shape.set_shape(&bg_rect);
        bg_shape.set_material(&bg_material);
        bg_shape.set_translation(width / 2.0, height / 2.0, -50.0);

        bg_shape
    }

    /// Creates a `ShapeNode` containing an image of a whale.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create resources in.
    ///
    /// # Returns
    /// A `ShapeNode` containing an image of a whale, or an `Error` if the image loading
    /// fails.
    fn make_whale_shape(session: SessionPtr) -> Result<ShapeNode, Error> {
        let whale_material = View::make_whale_material(session.clone())?;
        let rect = Rectangle::new(session.clone(), 500.0, 436.0);
        let rect_shape = ShapeNode::new(session.clone());
        rect_shape.set_shape(&rect);
        rect_shape.set_material(&whale_material);
        rect_shape.set_translation(0.0, 0.0, -100.0);

        Ok(rect_shape)
    }

    /// Creates a `Material` containing an appropriately colored image of a whale.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create resources in.
    ///
    /// # Returns
    /// A `Material` containing an image of a whale, or an `Error` if the image loading
    /// fails.
    fn make_whale_material(session: SessionPtr) -> Result<Material, Error> {
        let decoder = png::Decoder::new(File::open("/pkg/data/whale.png")?);
        let (info, mut reader) = decoder.read_info()?;
        let mut buf = vec![0; info.buffer_size()];
        reader.next_frame(&mut buf)?;

        let px_size_bytes = std::mem::size_of::<u8>() * 4; // RGBA

        let (width, height) = (info.width, info.height);
        let size_bytes = width as usize * height as usize * px_size_bytes;
        let image_info = images::ImageInfo {
            transform: images::Transform::Normal,
            width,
            height,
            stride: width * px_size_bytes as u32,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Premultiplied,
        };
        let host_memory = HostMemory::allocate(session.clone(), size_bytes)?;
        let host_image = HostImage::new(&host_memory, 0, image_info);

        // Swizzle RGBA to BGRA.
        // Since PNG files always use non-premultiplied alpha in colors
        // (https://www.w3.org/TR/PNG-Rationale.html#R.Non-premultiplied-alpha),
        // We also convert them to premultiplied alpha bitmaps.
        for i in (0..size_bytes).step_by(px_size_bytes) {
            let (r, g, b, a) = (buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
            let alpha_factor = (a as f32) / 255.;
            let (premultiplied_r, premultiplied_g, premultiplied_b) = (
                (r as f32 * alpha_factor) as u8,
                (g as f32 * alpha_factor) as u8,
                (b as f32 * alpha_factor) as u8,
            );
            buf[i] = premultiplied_b;
            buf[i + 1] = premultiplied_g;
            buf[i + 2] = premultiplied_r;
            buf[i + 3] = a;
        }

        host_image.mapping().write(&buf);

        let material = Material::new(session);
        material.set_color(ColorRgba { red: 70, green: 150, blue: 207, alpha: 128 });
        material.set_texture(Some(&host_image));

        Ok(material)
    }

    /// Updates the `View`'s animations to reflect the time in the provided `Context`.
    ///
    /// # Parameters
    /// - `context`: The context used to animate correctly.
    pub fn update(&self, context: ContextPtr) {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let ctx = context.lock().unwrap();

        let t = ((ctx.presentation_time.into_nanos() - ctx.start_time.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;

        let (center_x, center_y) =
            (ctx.display_info.width_in_px as f32 / 2.0, ctx.display_info.height_in_px as f32 / 2.0);

        const BOUNCE_HEIGHT: f32 = 300.0;
        let y_bounce = ((2.0 * t - 1.0).powf(2.0) + 1.0) * BOUNCE_HEIGHT;
        self.rect_shape.set_translation(center_x, center_y - BOUNCE_HEIGHT + y_bounce, -100.0);

        let angle = t * PI * 2.0;
        let quat =
            graphics_util::quaternion_from_axis_angle(Vec3 { x: 0.0, y: 0.0, z: 1.0 }, angle);
        self.rect_shape.set_rotation(quat.x, quat.y, quat.z, quat.w);
    }
}
