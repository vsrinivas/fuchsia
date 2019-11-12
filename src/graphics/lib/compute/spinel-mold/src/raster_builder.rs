#[cfg(not(feature = "lib"))]
use std::{cell::RefCell, rc::Rc};

#[cfg(feature = "lib")]
use mold::Path;
use mold::Raster;

#[cfg(not(feature = "lib"))]
use crate::{Context, PathId, RasterId};

#[derive(Debug)]
pub struct RasterBuilder {
    #[cfg(not(feature = "lib"))]
    context: Rc<RefCell<Context>>,
    #[cfg(not(feature = "lib"))]
    paths_transforms: Vec<(PathId, [f32; 9])>,
    #[cfg(feature = "lib")]
    paths_transforms: Vec<(Path, [f32; 9])>,
}

impl RasterBuilder {
    #[cfg(feature = "lib")]
    pub fn new() -> Self {
        Self { paths_transforms: vec![] }
    }

    #[cfg(not(feature = "lib"))]
    pub fn new(context: Rc<RefCell<Context>>) -> Self {
        Self { context, paths_transforms: vec![] }
    }

    #[cfg(feature = "lib")]
    pub fn push_path(&mut self, path: Path, transform: &[f32; 9]) {
        self.paths_transforms.push((path, *transform));
    }

    #[cfg(not(feature = "lib"))]
    pub fn push_path(&mut self, path: PathId, transform: &[f32; 9]) {
        self.paths_transforms.push((path, *transform));
    }

    #[cfg(feature = "lib")]
    pub fn build(&mut self) -> Raster {
        let raster = Raster::from_paths_and_transforms(
            self.paths_transforms.iter().map(|(path, transform)| (path, transform)),
        );

        self.paths_transforms.clear();

        raster
    }

    #[cfg(not(feature = "lib"))]
    pub fn build(&mut self) -> RasterId {
        let mut context = self.context.borrow_mut();
        let raster = Raster::from_paths_and_transforms(
            self.paths_transforms
                .iter()
                .map(|(path, transform)| (context.get_path(*path), transform)),
        );

        let id = context.insert_raster(raster.inner);

        self.paths_transforms.clear();

        id
    }
}
