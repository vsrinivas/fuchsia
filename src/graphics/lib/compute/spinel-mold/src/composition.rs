use std::collections::HashMap;
#[cfg(not(feature = "lib"))]
use std::{cell::RefCell, rc::Rc};

use mold::Raster;

use crate::styling::COMMANDS_LAYER_ID_MAX;
#[cfg(not(feature = "lib"))]
use crate::Context;

#[derive(Debug)]
pub struct Composition {
    #[cfg(not(feature = "lib"))]
    pub(crate) context: Rc<RefCell<Context>>,
    placements: HashMap<u32, Vec<Raster>>,
    layers: HashMap<u32, Raster>,
}

impl Composition {
    #[cfg(feature = "lib")]
    pub fn new() -> Self {
        Self { placements: HashMap::new(), layers: HashMap::new() }
    }

    #[cfg(not(feature = "lib"))]
    pub fn new(context: Rc<RefCell<Context>>) -> Self {
        Self { context, placements: HashMap::new(), layers: HashMap::new() }
    }

    pub fn reset(&mut self) {
        self.placements.clear();
        self.layers.clear();
    }

    pub fn place(&mut self, layer_id: u32, raster: Raster) {
        if layer_id >= COMMANDS_LAYER_ID_MAX {
            panic!("layer_id overflowed the maximum of {}", COMMANDS_LAYER_ID_MAX);
        }

        self.layers.remove(&layer_id);
        self.placements
            .entry(layer_id)
            .and_modify(|layer| layer.push(raster.clone()))
            .or_insert_with(|| vec![raster]);
    }

    pub fn layers(&mut self) -> &HashMap<u32, Raster> {
        for (&layer_id, rasters) in &self.placements {
            self.layers.entry(layer_id).or_insert_with(|| match rasters.len() {
                0 => Raster::empty(),
                1 => rasters[0].clone(),
                _ => Raster::union(rasters.iter()),
            });
        }

        &self.layers
    }
}
