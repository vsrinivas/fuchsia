#[cfg(not(feature = "lib"))]
use std::{
    cell::RefCell,
    collections::{HashMap, HashSet},
    convert::TryFrom,
    mem, ptr,
    rc::Rc,
    slice,
};

mod composition;
mod path_builder;
mod raster_builder;
mod styling;

#[cfg(not(feature = "lib"))]
use composition::Composition;
#[cfg(not(feature = "lib"))]
use path_builder::PathBuilder;
#[cfg(not(feature = "lib"))]
use raster_builder::RasterBuilder;
#[cfg(not(feature = "lib"))]
use styling::Styling;

#[cfg(feature = "lib")]
pub use composition::Composition;
#[cfg(feature = "lib")]
pub use path_builder::PathBuilder;
#[cfg(feature = "lib")]
pub use raster_builder::RasterBuilder;
#[cfg(feature = "lib")]
pub use styling::Styling;

#[cfg(not(feature = "lib"))]
use mold::{tile::Map, ColorBuffer, Path, PixelFormat, Point, RasterInner};

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum SpnResult {
    SpnSuccess,
    SpnErrorNotImplemented,
    SpnErrorContextLost,
    SpnErrorPathBuilderLost,
    SpnErrorRasterBuilderLost,
    SpnErrorRasterBuilderSealed,
    SpnErrorRasterBuilderTooManyRasters,
    SpnErrorRenderExtensionInvalid,
    SpnErrorRenderExtensionVkSubmitInfoWaitCountExceeded,
    SpnErrorLayerIdInvalid,
    SpnErrorLayerNotEmpty,
    SpnErrorPoolEmpty,
    SpnErrorCondvarWait,
    SpnErrorTransformWeakrefInvalid,
    SpnErrorStrokeStyleWeakrefInvalid,
    SpnErrorCommandNotReady,
    SpnErrorCommandNotCompleted,
    SpnErrorCommandNotStarted,
    SpnErrorCommandNotReadyOrCompleted,
    SpnErrorCompositionSealed,
    SpnErrorStylingSealed,
    SpnErrorHandleInvalid,
    SpnErrorHandleOverflow,
}

pub const SPN_STYLING_OPCODE_NOOP: u32 = 0;

pub const SPN_STYLING_OPCODE_COVER_NONZERO: u32 = 1;
pub const SPN_STYLING_OPCODE_COVER_EVENODD: u32 = 2;
pub const SPN_STYLING_OPCODE_COVER_ACCUMULATE: u32 = 3;
pub const SPN_STYLING_OPCODE_COVER_MASK: u32 = 4;

pub const SPN_STYLING_OPCODE_COVER_WIP_ZERO: u32 = 5;
pub const SPN_STYLING_OPCODE_COVER_ACC_ZERO: u32 = 6;
pub const SPN_STYLING_OPCODE_COVER_MASK_ZERO: u32 = 7;
pub const SPN_STYLING_OPCODE_COVER_MASK_ONE: u32 = 8;
pub const SPN_STYLING_OPCODE_COVER_MASK_INVERT: u32 = 9;

pub const SPN_STYLING_OPCODE_COLOR_FILL_SOLID: u32 = 10;
pub const SPN_STYLING_OPCODE_COLOR_FILL_GRADIENT_LINEAR: u32 = 11;

pub const SPN_STYLING_OPCODE_COLOR_WIP_ZERO: u32 = 12;
pub const SPN_STYLING_OPCODE_COLOR_ACC_ZERO: u32 = 13;

pub const SPN_STYLING_OPCODE_BLEND_OVER: u32 = 14;
pub const SPN_STYLING_OPCODE_BLEND_PLUS: u32 = 15;
pub const SPN_STYLING_OPCODE_BLEND_MULTIPLY: u32 = 16;
pub const SPN_STYLING_OPCODE_BLEND_KNOCKOUT: u32 = 17;

pub const SPN_STYLING_OPCODE_COVER_WIP_MOVE_TO_MASK: u32 = 18;
pub const SPN_STYLING_OPCODE_COVER_ACC_MOVE_TO_MASK: u32 = 19;

pub const SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND: u32 = 20;
pub const SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE: u32 = 21;
pub const SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY: u32 = 22;

pub const SPN_STYLING_OPCODE_COLOR_ILL_ZERO: u32 = 23;
pub const SPN_STYLING_OPCODE_COLOR_ILL_COPY_ACC: u32 = 24;
pub const SPN_STYLING_OPCODE_COLOR_ACC_MULTIPLY_ILL: u32 = 25;

#[cfg(not(feature = "lib"))]
unsafe fn retain_from_ptr<T>(ptr: *const T) {
    let object = Rc::from_raw(ptr);

    Rc::into_raw(Rc::clone(&object));
    Rc::into_raw(object);
}

#[cfg(not(feature = "lib"))]
unsafe fn clone_from_ptr<T>(ptr: *const T) -> Rc<T> {
    let original = Rc::from_raw(ptr);
    let clone = Rc::clone(&original);

    Rc::into_raw(original);

    clone
}

#[cfg(not(feature = "lib"))]
#[repr(C)]
#[derive(Clone, Debug)]
pub struct RawBuffer {
    pub buffer_ptr: *const *mut u8,
    pub stride: usize,
    pub format: PixelFormat,
}

#[cfg(not(feature = "lib"))]
unsafe impl Send for RawBuffer {}
#[cfg(not(feature = "lib"))]
unsafe impl Sync for RawBuffer {}

#[cfg(not(feature = "lib"))]
impl ColorBuffer for RawBuffer {
    fn pixel_format(&self) -> PixelFormat {
        self.format
    }

    fn stride(&self) -> usize {
        self.stride
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        let dst = (*self.buffer_ptr).add(offset);
        ptr::copy_nonoverlapping(src, dst, len);
    }
}

#[cfg(not(feature = "lib"))]
#[repr(C)]
#[derive(Debug)]
pub struct Context {
    map: Option<Map>,
    paths: HashMap<u32, (Path, usize)>,
    path_index: u32,
    rasters: HashMap<u32, (Rc<RasterInner>, usize)>,
    raster_index: u32,
    raw_buffer: RawBuffer,
    old_prints: HashSet<u32>,
    new_prints: HashSet<u32>,
}

#[cfg(not(feature = "lib"))]
impl Context {
    pub fn get_path(&self, id: PathId) -> &Path {
        &self.paths[&id].0
    }

    pub fn insert_path(&mut self, path: Path) -> PathId {
        if self.paths.len() == u32::max_value() as usize {
            panic!("cannot store more than {} spn_path_t", u32::max_value());
        }

        while self.paths.contains_key(&self.path_index) {
            self.path_index = self.path_index.wrapping_add(1);
        }

        self.paths.insert(self.path_index, (path, 1));

        self.path_index
    }

    pub unsafe fn retain_path(&mut self, id: PathId) {
        self.paths.entry(id).and_modify(|(_, rc)| *rc += 1);
    }

    pub unsafe fn release_path(&mut self, id: PathId) {
        self.paths.entry(id).and_modify(|(_, rc)| *rc -= 1);
        if self.paths[&id].1 == 0 {
            self.paths.remove(&id);
        }
    }

    pub fn get_raster(&self, id: RasterId) -> &Rc<RasterInner> {
        &self.rasters[&id].0
    }

    pub fn insert_raster(&mut self, raster: Rc<RasterInner>) -> RasterId {
        if self.rasters.len() == u32::max_value() as usize {
            panic!("cannot store more than {} spn_path_t", u32::max_value());
        }

        while self.rasters.contains_key(&self.raster_index) {
            self.raster_index = self.raster_index.wrapping_add(1);
        }

        self.rasters.insert(self.raster_index, (raster, 1));

        self.raster_index
    }

    pub fn retain_raster(&mut self, id: RasterId) {
        self.rasters.entry(id).and_modify(|(_, rc)| *rc += 1);
    }

    pub fn release_raster(&mut self, id: RasterId) {
        self.rasters.entry(id).and_modify(|(_, rc)| *rc -= 1);
        if self.rasters[&id].1 == 0 {
            self.rasters.remove(&id);
        }
    }

    pub fn swap_prints(&mut self) {
        mem::swap(&mut self.old_prints, &mut self.new_prints);
    }
}

#[cfg(not(feature = "lib"))]
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RenderSubmit {
    pub ext: *mut (),
    pub styling: StylingPtr,
    pub composition: CompositionPtr,
    pub tile_clip: [u32; 4],
}

#[cfg(not(feature = "lib"))]
pub type ContextPtr = *const RefCell<Context>;
#[cfg(not(feature = "lib"))]
pub type PathBuilderPtr = *const RefCell<PathBuilder>;
#[cfg(not(feature = "lib"))]
pub type PathId = u32;
#[cfg(not(feature = "lib"))]
pub type RasterId = u32;
#[cfg(not(feature = "lib"))]
pub type RasterPtr = *const RasterInner;
#[cfg(not(feature = "lib"))]
pub type RasterBuilderPtr = *const RefCell<RasterBuilder>;
#[cfg(not(feature = "lib"))]
pub type CompositionPtr = *const RefCell<Composition>;
#[cfg(not(feature = "lib"))]
pub type StylingPtr = *const RefCell<Styling>;

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn mold_context_create(
    context_ptr: *mut ContextPtr,
    raw_buffer: *const RawBuffer,
) {
    let context = Context {
        map: None,
        paths: HashMap::new(),
        path_index: 0,
        rasters: HashMap::new(),
        raster_index: 0,
        raw_buffer: (*raw_buffer).clone(),
        old_prints: HashSet::new(),
        new_prints: HashSet::new(),
    };
    *context_ptr = Rc::into_raw(Rc::new(RefCell::new(context)));
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_context_retain(context: ContextPtr) -> SpnResult {
    retain_from_ptr(context);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_context_release(context: ContextPtr) -> SpnResult {
    Rc::from_raw(context);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_context_reset(context: ContextPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_context_status(context: ContextPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_create(
    context: ContextPtr,
    path_builder_ptr: *mut PathBuilderPtr,
) -> SpnResult {
    let path_builder = PathBuilder::new(clone_from_ptr(context));
    *path_builder_ptr = Rc::into_raw(Rc::new(RefCell::new(path_builder)));

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_retain(path_builder: PathBuilderPtr) -> SpnResult {
    retain_from_ptr(path_builder);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_release(path_builder: PathBuilderPtr) -> SpnResult {
    Rc::from_raw(path_builder);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_path_builder_flush(path_builder: PathBuilderPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_path_builder_begin(path_builder: PathBuilderPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_end(
    path_builder: PathBuilderPtr,
    path_ptr: *mut PathId,
) -> SpnResult {
    *path_ptr = (*path_builder).borrow_mut().build();

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_move_to(
    path_builder: PathBuilderPtr,
    x0: f32,
    y0: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().move_to(x0, y0);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_line_to(
    path_builder: PathBuilderPtr,
    x1: f32,
    y1: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().line_to(x1, y1);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_quad_to(
    path_builder: PathBuilderPtr,
    x1: f32,
    y1: f32,
    x2: f32,
    y2: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().quad_to(x1, y1, x2, y2);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_quad_smooth_to(
    path_builder: PathBuilderPtr,
    x2: f32,
    y2: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().quad_smooth_to(x2, y2);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_cubic_to(
    path_builder: PathBuilderPtr,
    x1: f32,
    y1: f32,
    x2: f32,
    y2: f32,
    x3: f32,
    y3: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().cubic_to(x1, y1, x2, y2, x3, y3);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_cubic_smooth_to(
    path_builder: PathBuilderPtr,
    x2: f32,
    y2: f32,
    x3: f32,
    y3: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().cubic_smooth_to(x2, y2, x3, y3);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_rat_quad_to(
    path_builder: PathBuilderPtr,
    x1: f32,
    y1: f32,
    x2: f32,
    y2: f32,
    w0: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().rat_quad_to(x1, y1, x2, y2, w0);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_path_builder_rat_cubic_to(
    path_builder: PathBuilderPtr,
    x1: f32,
    y1: f32,
    x2: f32,
    y2: f32,
    x3: f32,
    y3: f32,
    w0: f32,
    w1: f32,
) -> SpnResult {
    (*path_builder).borrow_mut().rat_cubic_to(x1, y1, x2, y2, x3, y3, w0, w1);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_path_retain(
    context: ContextPtr,
    paths: *const PathId,
    count: u32,
) -> SpnResult {
    for &path in slice::from_raw_parts(paths, count as usize) {
        (*context).borrow_mut().retain_path(path);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_path_release(
    context: ContextPtr,
    paths: *const PathId,
    count: u32,
) -> SpnResult {
    for &path in slice::from_raw_parts(paths, count as usize) {
        (*context).borrow_mut().release_path(path);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_raster_builder_create(
    context: ContextPtr,
    raster_builder_ptr: *mut RasterBuilderPtr,
) -> SpnResult {
    let raster_builder = RasterBuilder::new(clone_from_ptr(context));
    *raster_builder_ptr = Rc::into_raw(Rc::new(RefCell::new(raster_builder)));

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_raster_builder_retain(raster_builder: RasterBuilderPtr) -> SpnResult {
    retain_from_ptr(raster_builder);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_raster_builder_release(raster_builder: RasterBuilderPtr) -> SpnResult {
    Rc::from_raw(raster_builder);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_raster_builder_flush(raster_builder: RasterBuilderPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_raster_builder_begin(raster_builder: RasterBuilderPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_raster_builder_end(
    raster_builder: RasterBuilderPtr,
    raster_ptr: *mut RasterId,
) -> SpnResult {
    *raster_ptr = (*raster_builder).borrow_mut().build();

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_raster_builder_add(
    raster_builder: RasterBuilderPtr,
    paths: *const PathId,
    _transform_weakrefs: *const (),
    mut transforms: *const f32,
    _clip_weakrefs: *const (),
    _clips: *const f32,
    count: u32,
) -> SpnResult {
    // TODO(dragostis): Implement Path clipping for Rasters.
    for &path in slice::from_raw_parts(paths, count as usize) {
        let mut transform = [1.0; 9];

        for slot in transform.iter_mut().take(8) {
            *slot = transforms.read() / 32.0;
            transforms = transforms.add(1);
        }

        (*raster_builder).borrow_mut().push_path(path, &transform);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_raster_retain(
    context: ContextPtr,
    rasters: *const RasterId,
    count: u32,
) -> SpnResult {
    for &raster in slice::from_raw_parts(rasters, count as usize) {
        (*context).borrow_mut().retain_raster(raster);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_raster_release(
    context: ContextPtr,
    rasters: *const RasterId,
    count: u32,
) -> SpnResult {
    for &raster in slice::from_raw_parts(rasters, count as usize) {
        (*context).borrow_mut().release_raster(raster);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_composition_create(
    context: ContextPtr,
    composition_ptr: *mut CompositionPtr,
) -> SpnResult {
    let composition = Composition::new(clone_from_ptr(context));
    *composition_ptr = Rc::into_raw(Rc::new(RefCell::new(composition)));

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_composition_clone(
    context: ContextPtr,
    composition: CompositionPtr,
    clone_ptr: *mut CompositionPtr,
) -> SpnResult {
    *clone_ptr = Rc::into_raw(clone_from_ptr(composition));

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_composition_retain(composition: CompositionPtr) -> SpnResult {
    retain_from_ptr(composition);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_composition_release(composition: CompositionPtr) -> SpnResult {
    Rc::from_raw(composition);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_composition_place(
    composition: CompositionPtr,
    rasters: *const RasterId,
    layer_ids: *const u32,
    mut txtys: *const i32,
    count: u32,
) -> SpnResult {
    let rasters = slice::from_raw_parts(rasters, count as usize)
        .iter()
        .zip(slice::from_raw_parts(layer_ids, count as usize));

    for (&raster, &layer_id) in rasters {
        let mut translation = Point::new(0, 0);

        if !txtys.is_null() {
            translation.x = txtys.read();
            txtys = txtys.add(1);
            translation.y = txtys.read();
            txtys = txtys.add(1);
        }

        let raster = {
            let borrow = (*composition).borrow();
            let context = borrow.context.borrow();
            RasterInner::translated(&context.get_raster(raster), translation)
        };

        (*composition).borrow_mut().place(layer_id, raster);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_composition_seal(composition: CompositionPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_composition_unseal(composition: CompositionPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_composition_reset(composition: CompositionPtr) -> SpnResult {
    (*composition).borrow_mut().reset();

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_composition_get_bounds(
    composition: CompositionPtr,
    bounds: *mut i32,
) -> SpnResult {
    unimplemented!()
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_styling_create(
    context: ContextPtr,
    styling_ptr: *mut StylingPtr,
    layers_count: u32,
    cmds_count: u32,
) -> SpnResult {
    let styling = Styling::new();
    *styling_ptr = Rc::into_raw(Rc::new(RefCell::new(styling)));

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_retain(styling: StylingPtr) -> SpnResult {
    retain_from_ptr(styling);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_release(styling: StylingPtr) -> SpnResult {
    Rc::from_raw(styling);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_styling_seal(styling: StylingPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
#[allow(unused_variables)]
pub unsafe extern "C" fn spn_styling_unseal(styling: StylingPtr) -> SpnResult {
    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_reset(styling: StylingPtr) -> SpnResult {
    (*styling).borrow_mut().reset();

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_alloc(
    styling: StylingPtr,
    group_id: *mut u32,
) -> SpnResult {
    *group_id = (*styling).borrow_mut().group_alloc();

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_enter(
    styling: StylingPtr,
    group_id: u32,
    count: u32,
    cmds: *mut *mut u32,
) -> SpnResult {
    if count > 0 {
        *cmds = (*styling).borrow_mut().group_enter(group_id, count);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_leave(
    styling: StylingPtr,
    group_id: u32,
    count: u32,
    cmds: *mut *mut u32,
) -> SpnResult {
    if count > 0 {
        *cmds = (*styling).borrow_mut().group_leave(group_id, count);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_parents(
    styling: StylingPtr,
    group_id: u32,
    count: u32,
    parents: *mut *mut u32,
) -> SpnResult {
    if count > 0 {
        *parents = (*styling).borrow_mut().group_parents(group_id, count);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_range_lo(
    styling: StylingPtr,
    group_id: u32,
    layer_lo: u32,
) -> SpnResult {
    (*styling).borrow_mut().group_range_lo(group_id, layer_lo);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_range_hi(
    styling: StylingPtr,
    group_id: u32,
    layer_hi: u32,
) -> SpnResult {
    (*styling).borrow_mut().group_range_hi(group_id, layer_hi);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_group_layer(
    styling: StylingPtr,
    group_id: u32,
    layer_id: u32,
    count: u32,
    cmds: *mut *mut u32,
) -> SpnResult {
    if count > 0 {
        *cmds = (*styling).borrow_mut().layer(group_id, layer_id, count);
    }

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_layer_fill_rgba_encoder(mut cmds: *mut u32, rgba: &[f32; 4]) {
    let mut bytes = [0u8; 4];

    for i in 0..4 {
        bytes[i] = u8::try_from((rgba[i] * 255.0).round() as u32)
            .expect("RGBA colors must be between 0.0 and 1.0");
    }

    cmds.write(SPN_STYLING_OPCODE_COLOR_FILL_SOLID);
    cmds = cmds.add(1);

    cmds.write(u32::from_be_bytes(bytes));
    cmds = cmds.add(1);

    cmds.write(0);
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_styling_background_over_encoder(mut cmds: *mut u32, rgba: &[f32; 4]) {
    let mut bytes = [0u8; 4];

    for i in 0..4 {
        bytes[i] = u8::try_from((rgba[i] * 255.0).round() as u32)
            .expect("RGBA colors must be between 0.0 and 1.0");
    }

    cmds.write(SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND);
    cmds = cmds.add(1);

    cmds.write(u32::from_be_bytes(bytes));
    cmds = cmds.add(1);

    cmds.write(0);
}

#[cfg(not(feature = "lib"))]
#[no_mangle]
pub unsafe extern "C" fn spn_render(context: ContextPtr, submit: *const RenderSubmit) -> SpnResult {
    let submit = *submit;
    let width = (submit.tile_clip[2] - submit.tile_clip[0]) as usize;
    let height = (submit.tile_clip[3] - submit.tile_clip[1]) as usize;

    let mut context = (*context).borrow_mut();

    let mut map = context
        .map
        .take()
        .filter(|map| map.width() != width || map.height() != height)
        .unwrap_or_else(|| Map::new(width, height));

    let mut styling = (*submit.styling).borrow_mut();
    let mut composition = (*submit.composition).borrow_mut();

    styling.prints(&mut composition, &mut map, &mut context.new_prints);

    for &id in context.old_prints.difference(&context.new_prints) {
        map.remove(id);
    }

    context.old_prints.clear();
    context.swap_prints();

    map.render(context.raw_buffer.clone());

    context.map = Some(map);

    SpnResult::SpnSuccess
}

#[cfg(not(feature = "lib"))]
#[cfg(test)]
mod tests {
    use super::*;

    use std::mem::MaybeUninit;

    unsafe fn init<T, U>(f: impl FnOnce(*mut T) -> U) -> T {
        let mut value = MaybeUninit::uninit();
        f(value.as_mut_ptr());
        value.assume_init()
    }

    unsafe fn band(
        path_builder: PathBuilderPtr,
        x: f32,
        y: f32,
        width: f32,
        height: f32,
    ) -> PathId {
        spn_path_builder_begin(path_builder);

        spn_path_builder_move_to(path_builder, x, y);
        spn_path_builder_line_to(path_builder, x + width, y);
        spn_path_builder_line_to(path_builder, x + width, y - height);
        spn_path_builder_line_to(path_builder, x, y - height);
        spn_path_builder_line_to(path_builder, x, y);

        init(|ptr| spn_path_builder_end(path_builder, ptr))
    }

    unsafe fn raster(raster_builder: RasterBuilderPtr, path: PathId) -> RasterId {
        spn_raster_builder_begin(raster_builder);

        spn_raster_builder_add(
            raster_builder,
            &path,
            ptr::null(),
            [32.0, 0.0, 0.0, 0.0, 32.0, 0.0, 0.0, 0.0, 1.0].as_ptr(),
            ptr::null(),
            ptr::null(),
            1,
        );

        init(|ptr| spn_raster_builder_end(raster_builder, ptr))
    }

    #[test]
    fn end_to_end() {
        unsafe {
            let width = 3;
            let height = 3;
            let buffer_size = width * height;
            let mut buffer: Vec<u32> = Vec::with_capacity(buffer_size);
            let buffer_ptr: *mut u8 = mem::transmute(buffer.as_mut_ptr());

            let raw_buffer =
                RawBuffer { buffer_ptr: &buffer_ptr, stride: 3, format: PixelFormat::RGBA8888 };

            let context = init(move |ptr| mold_context_create(ptr, &raw_buffer));
            let path_builder = init(|ptr| spn_path_builder_create(context, ptr));

            let band_top = band(path_builder, 1.0, 3.0, 1.0, 3.0);
            let band_bottom = band(path_builder, 0.0, 2.0, 3.0, 1.0);

            let raster_builder = init(|ptr| spn_raster_builder_create(context, ptr));

            let raster_top = raster(raster_builder, band_top);
            let raster_bottom = raster(raster_builder, band_bottom);

            let composition = init(|ptr| spn_composition_create(context, ptr));

            let rasters = [raster_top, raster_bottom];
            spn_composition_place(
                composition,
                rasters.as_ptr(),
                [1, 0].as_ptr(),
                [0, 0, 0, 0].as_ptr(),
                rasters.len() as u32,
            );

            let styling = init(|ptr| spn_styling_create(context, ptr, 2, 32));

            let top_group = init(|ptr| spn_styling_group_alloc(styling, ptr));

            spn_styling_group_range_lo(styling, top_group, 0);
            spn_styling_group_range_hi(styling, top_group, 1);

            let enter_cmds = init(|ptr| spn_styling_group_enter(styling, top_group, 1, ptr));
            enter_cmds.write(SPN_STYLING_OPCODE_COLOR_ACC_ZERO);

            let mut leave_cmds = init(|ptr| spn_styling_group_leave(styling, top_group, 4, ptr));
            spn_styling_background_over_encoder(leave_cmds, &[0.0, 0.0, 1.0, 1.0]);
            leave_cmds = leave_cmds.add(3);
            leave_cmds.write(SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE);

            let mut layer_cmds = init(|ptr| spn_styling_group_layer(styling, top_group, 0, 6, ptr));
            layer_cmds.write(SPN_STYLING_OPCODE_COVER_WIP_ZERO);
            layer_cmds = layer_cmds.add(1);
            layer_cmds.write(SPN_STYLING_OPCODE_COVER_NONZERO);
            layer_cmds = layer_cmds.add(1);
            spn_styling_layer_fill_rgba_encoder(layer_cmds, &[0.0, 1.0, 0.0, 1.0]);
            layer_cmds = layer_cmds.add(3);
            layer_cmds.write(SPN_STYLING_OPCODE_BLEND_OVER);

            let mut layer_cmds = init(|ptr| spn_styling_group_layer(styling, top_group, 1, 6, ptr));
            layer_cmds.write(SPN_STYLING_OPCODE_COVER_WIP_ZERO);
            layer_cmds = layer_cmds.add(1);
            layer_cmds.write(SPN_STYLING_OPCODE_COVER_NONZERO);
            layer_cmds = layer_cmds.add(1);
            spn_styling_layer_fill_rgba_encoder(layer_cmds, &[1.0, 0.0, 0.0, 1.0]);
            layer_cmds = layer_cmds.add(3);
            layer_cmds.write(SPN_STYLING_OPCODE_BLEND_OVER);

            let submit = RenderSubmit {
                ext: ptr::null_mut(),
                styling,
                composition,
                tile_clip: [0, 0, width as u32, height as u32],
            };

            spn_render(context, &submit);

            spn_styling_release(styling);
            spn_composition_release(composition);

            spn_raster_release(context, rasters.as_ptr(), rasters.len() as u32);

            spn_raster_builder_release(raster_builder);

            let paths = [band_top, band_bottom];
            spn_path_release(context, paths.as_ptr(), paths.len() as u32);

            spn_path_builder_release(path_builder);
            spn_context_release(context);

            buffer.set_len(buffer_size);
            assert_eq!(
                buffer,
                vec![
                    0xffff_0000,
                    0x0000_00ff,
                    0xffff_0000,
                    0x0000_ff00,
                    0x0000_ff00,
                    0x0000_ff00,
                    0xffff_0000,
                    0x0000_00ff,
                    0xffff_0000,
                ]
            );
        }
    }
}
