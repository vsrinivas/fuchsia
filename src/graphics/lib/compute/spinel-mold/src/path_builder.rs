use std::mem;
#[cfg(not(feature = "lib"))]
use std::{cell::RefCell, rc::Rc};

use mold::{Path, Point};

#[cfg(not(feature = "lib"))]
use crate::{Context, PathId};

#[derive(Debug)]
pub struct PathBuilder {
    #[cfg(not(feature = "lib"))]
    context: Rc<RefCell<Context>>,
    current_path: Path,
    end_point: Point<f32>,
    end_control_point: Point<f32>,
}

impl PathBuilder {
    #[cfg(feature = "lib")]
    pub fn new() -> Self {
        Self {
            current_path: Path::new(),
            end_point: Point::new(0.0, 0.0),
            end_control_point: Point::new(0.0, 0.0),
        }
    }

    #[cfg(not(feature = "lib"))]
    pub fn new(context: Rc<RefCell<Context>>) -> Self {
        Self {
            context,
            current_path: Path::new(),
            end_point: Point::new(0.0, 0.0),
            end_control_point: Point::new(0.0, 0.0),
        }
    }

    pub fn move_to(&mut self, x0: f32, y0: f32) {
        let point = Point::new(x0, y0);

        self.end_point = point;
        self.end_control_point = point;
    }

    pub fn line_to(&mut self, x1: f32, y1: f32) {
        let p1 = Point::new(x1, y1);

        self.current_path.line(self.end_point, p1);
        self.end_point = p1;
        self.end_control_point = p1;
    }

    pub fn quad_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32) {
        let p1 = Point::new(x1, y1);
        let p2 = Point::new(x2, y2);

        self.current_path.quad(self.end_point, p1, p2);
        self.end_point = p2;
        self.end_control_point = p1;
    }

    pub fn quad_smooth_to(&mut self, x2: f32, y2: f32) {
        let p1 = Point::new(
            2.0 * self.end_point.x - self.end_control_point.x,
            2.0 * self.end_point.y - self.end_control_point.y,
        );
        let p2 = Point::new(x2, y2);

        self.current_path.quad(self.end_point, p1, p2);
        self.end_point = p2;
        self.end_control_point = p1;
    }

    pub fn cubic_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x3: f32, y3: f32) {
        let p1 = Point::new(x1, y1);
        let p2 = Point::new(x2, y2);
        let p3 = Point::new(x3, y3);

        self.current_path.cubic(self.end_point, p1, p2, p3);
        self.end_point = p3;
        self.end_control_point = p2;
    }

    pub fn cubic_smooth_to(&mut self, x2: f32, y2: f32, x3: f32, y3: f32) {
        let p1 = Point::new(
            2.0 * self.end_point.x - self.end_control_point.x,
            2.0 * self.end_point.y - self.end_control_point.y,
        );
        let p2 = Point::new(x2, y2);
        let p3 = Point::new(x3, y3);

        self.current_path.cubic(self.end_point, p1, p2, p3);
        self.end_point = p3;
        self.end_control_point = p2;
    }

    pub fn rat_quad_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, w0: f32) {
        let p1 = Point::new(x1, y1);
        let p2 = Point::new(x2, y2);

        self.current_path.rat_quad((self.end_point, 1.0), (p1, w0), (p2, 1.0));
        self.end_point = p2;
        self.end_control_point = p2;
    }

    pub fn rat_cubic_to(
        &mut self,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
        w0: f32,
        w1: f32,
    ) {
        let p1 = Point::new(x1, y1);
        let p2 = Point::new(x2, y2);
        let p3 = Point::new(x3, y3);

        self.current_path.rat_cubic((self.end_point, 1.0), (p1, w0), (p2, w1), (p3, 1.0));
        self.end_point = p3;
        self.end_control_point = p3;
    }

    #[cfg(feature = "lib")]
    pub fn build(&mut self) -> Path {
        let mut path = Path::new();
        mem::swap(&mut self.current_path, &mut path);

        path
    }

    #[cfg(not(feature = "lib"))]
    pub fn build(&mut self) -> PathId {
        let mut path = Path::new();
        mem::swap(&mut self.current_path, &mut path);

        self.context.borrow_mut().insert_path(path)
    }
}
