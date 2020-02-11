// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum Op<R> {
    Raster(R),
    Union(Vec<R>),
}

impl<R> Op<R> {
    pub fn len(&self) -> usize {
        match self {
            Self::Raster(_) => 1,
            Self::Union(rasters) => rasters.len(),
        }
    }

    pub fn iter(&self) -> OpIter<'_, R> {
        OpIter {
            op: self,
            i: 0,
        }
    }

    pub fn add(self, op: Self) -> Self {
         match (self, op) {
            (Self::Raster(r0), Self::Raster(r1)) => Self::Union(vec![r0, r1]),
            (Self::Raster(raster), Self::Union(mut rasters)) => {
                rasters.push(raster);
                Self::Union(rasters)
            }
            (Self::Union(mut rasters), Self::Raster(raster)) => {
                rasters.push(raster);
                Self::Union(rasters)
            }
            (Self::Union(mut r0), Self::Union(r1)) => {
                r0.extend(r1);
                Self::Union(r0)
            }
        }
    }
}

pub struct OpIter<'a, R> {
    op: &'a Op<R>,
    i: usize,
}

impl<'a, R> Iterator for OpIter<'a, R> {
    type Item = &'a R;

    fn next(&mut self) -> Option<Self::Item> {
        if self.i == self.op.len() {
            return None;
        }

        let result = match self.op {
            Op::Raster(raster) => Some(raster),
            Op::Union(rasters) => Some(&rasters[self.i]),
        };

        self.i += 1;

        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add() {
        let op1 = Op::Raster(1);
        let op2 = Op::Raster(2);
        let op3 = Op::Raster(3);
        let op4 = Op::Raster(4);

        assert_eq!((op1.add(op2)).add(op3.add(op4)), Op::Union(vec![1, 2, 3, 4]));
    }
}
