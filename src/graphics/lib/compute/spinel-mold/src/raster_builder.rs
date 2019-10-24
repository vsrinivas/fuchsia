use std::{cell::RefCell, rc::Rc};

use mold::Raster;

use crate::{Context, PathId, RasterId};

#[derive(Debug)]
pub struct RasterBuilder {
    context: Rc<RefCell<Context>>,
    paths_transforms: Vec<(PathId, [f32; 9])>,
}

impl RasterBuilder {
    pub fn new(context: Rc<RefCell<Context>>) -> Self {
        Self {
            context,
            paths_transforms: vec![],
        }
    }

    pub fn push_path(&mut self, path: PathId, transform: &[f32; 9]) {
        self.paths_transforms.push((path, *transform));
    }

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
