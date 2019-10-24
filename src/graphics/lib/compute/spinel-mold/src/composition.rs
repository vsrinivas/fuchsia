use std::{cell::RefCell, collections::HashMap, rc::Rc};

use mold::Raster;

use crate::{Context, styling::COMMANDS_LAYER_ID_MAX};

#[derive(Debug)]
pub struct Composition {
    pub(crate) context: Rc<RefCell<Context>>,
    placements: HashMap<u32, Vec<Raster>>,
    layers: HashMap<u32, Raster>,
}

impl Composition {
    pub fn new(context: Rc<RefCell<Context>>) -> Self {
        Self {
            context,
            placements: HashMap::new(),
            layers: HashMap::new(),
        }
    }

    pub fn reset(&mut self) {
        self.placements.clear();
        self.layers.clear();
    }

    pub fn place(&mut self, layer_id: u32, raster: Raster) {
        if layer_id >= COMMANDS_LAYER_ID_MAX {
            panic!(
                "layer_id overflowed the maximum of {}",
                COMMANDS_LAYER_ID_MAX
            );
        }

        self.layers.remove(&layer_id);
        self.placements
            .entry(layer_id)
            .and_modify(|layer| layer.push(raster.clone()))
            .or_insert_with(|| vec![raster]);
    }

    pub fn layers(&mut self) -> &HashMap<u32, Raster> {
        for (&layer_id, rasters) in &self.placements {
            self.layers
                .entry(layer_id)
                .or_insert_with(|| {
                    match rasters.len() {
                        0 => Raster::empty(),
                        1 => rasters[0].clone(),
                        _ => Raster::union(rasters.iter()),
                    }
                });
        }

        &self.layers
    }
}
