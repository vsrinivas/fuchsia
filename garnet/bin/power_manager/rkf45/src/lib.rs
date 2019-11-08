use failure::{format_err, Error};

/// Runge-Kutta Fehlberg 4(5) parameters. These are readily available in literature,
/// e.g. <https://en.wikipedia.org/wiki/Runge%E2%80%93Kutta%E2%80%93Fehlberg_method>.
mod params {
    /// Number of stages
    pub const NUM_STAGES: usize = 6;

    /// Runge-Kutta matrix
    pub static A: [[f64; NUM_STAGES - 1]; NUM_STAGES - 1] = [
        [1.0 / 4.0, 0.0, 0.0, 0.0, 0.0],
        [3.0 / 32.0, 9.0 / 32.0, 0.0, 0.0, 0.0],
        [1932.0 / 2197.0, -7200.0 / 2197.0, 7296.0 / 2197.0, 0.0, 0.0],
        [439.0 / 216.0, -8.0, 3680.0 / 513.0, -845.0 / 4104.0, 0.0],
        [-8.0 / 27.0, 2.0, -3544.0 / 2565.0, 1859.0 / 4104.0, -11.0 / 40.0],
    ];

    /// 4th-order weights
    pub static B4: [f64; NUM_STAGES] =
        [25.0 / 216.0, 0.0, 1408.0 / 2565.0, 2197.0 / 4104.0, -1.0 / 5.0, 0.0];

    /// 5th-order weights
    pub static B5: [f64; NUM_STAGES] =
        [16.0 / 135.0, 0.0, 6656.0 / 12825.0, 28561.0 / 56430.0, -9.0 / 50.0, 2.0 / 55.0];

    /// Nodes
    pub static C: [f64; NUM_STAGES - 1] = [1.0 / 4.0, 3.0 / 8.0, 12.0 / 13.0, 1.0, 1.0 / 2.0];
}

/// Options to the RKF solver.
pub struct RkfOptions {
    /// Initial time for the solution.
    pub t_initial: f64,
    /// Final time for the solution.
    pub t_final: f64,
    /// Length of time step.
    pub dt: f64,
}

/// Vector operations to implement on `[f64]` for convenience.
trait VectorOperations {
    /// Scale by a scalar.
    fn scale(&mut self, a: f64);

    /// Add a vector to this one.
    fn add(&mut self, x: &Self);

    /// Add a scalar times another vector. Caller must ensure that lengths are equal.
    fn add_ax(&mut self, a: f64, x: &Self);

    /// Subtract a vector from this one.
    fn subtract(&mut self, x: &Self);

    /// Sets all elements to zero.
    fn to_zeros(&mut self);
}

impl VectorOperations for [f64] {
    fn scale(&mut self, a: f64) {
        self.iter_mut().for_each(|p| *p = *p * a);
    }

    fn add(&mut self, x: &Self) {
        self.iter_mut().zip(x.iter()).for_each(|(p, q)| *p += *q);
    }

    fn add_ax(&mut self, a: f64, x: &Self) {
        self.iter_mut().zip(x.iter()).for_each(|(p, q)| *p += a * *q);
    }

    fn subtract(&mut self, x: &Self) {
        self.iter_mut().zip(x.iter()).for_each(|(p, q)| *p -= *q);
    }

    fn to_zeros(&mut self) {
        self.iter_mut().for_each(|p| *p = 0.0)
    }
}

/// Takes a single time step using RKF45.
///
/// Args:
///   y: Slice containing solution values at the beginning of the time step.
///       This will be updated in-place.
///   dydt: Function that evaluates the time derivative of y.
///   tn: Time value at the beginning of the step.
///   dt: Length of the step.
/// Returns:
///   Vector estimating elementwise numerical error over this single step.
fn rkf45_step(y: &mut [f64], dydt: &dyn Fn(f64, &[f64]) -> Vec<f64>, tn: f64, dt: f64) -> Vec<f64> {
    // Work array reused by several loops below.
    let mut work = vec![0.0; y.len()];

    let mut k = Vec::with_capacity(6);
    k.push(dydt(tn, y));
    for i in 1..params::NUM_STAGES {
        work.to_zeros();
        for j in 0..i {
            work.add_ax(params::A[i - 1][j], &k[j]);
        }
        // Effectively, k[i] = dydt(tn + C[i-1] * dt, y + dt * work).
        work.scale(dt);
        work.add(&y);
        k.push(dydt(tn + params::C[i - 1] * dt, &work));
    }

    // The 5th-order solution will be used for error-estimation.
    let mut y_5th_order = Vec::with_capacity(y.len());
    y_5th_order.extend_from_slice(y);
    work.to_zeros();
    for i in 0..params::B5.len() {
        work.add_ax(params::B5[i], &k[i]);
    }
    y_5th_order.add_ax(dt, &work);

    // y is updated with the 4th-order solution.
    work.to_zeros();
    for i in 0..params::B4.len() {
        work.add_ax(params::B4[i], &k[i]);
    }
    y.add_ax(dt, &work);

    let mut error_estimate = y_5th_order;
    error_estimate.subtract(&y);
    error_estimate.iter_mut().for_each(|x| *x = (*x).abs());
    error_estimate
}

/// Solves an initial value problem using RKF45.
///
/// NOTE: RKF45 is often used in conjunction with adaptive time stepping. For sake of simplicity
/// based on current needs, this implementation uses a fixed time step, but returns a total error
/// estimate that may be used by the caller.
///
/// Args:
///   y: Slice containing the initial value of the solution. This will be updated in-place.
///   dydt: Function that evaluates the time derivative of y.
///   options: Specifies parameters for the solver.
/// Returns:
///   Vector estimating total elementwise numerical error incurred by integration.
pub fn rkf45_advance(
    y: &mut [f64],
    dydt: &dyn Fn(f64, &[f64]) -> Vec<f64>,
    options: &RkfOptions,
) -> Result<Vec<f64>, Error> {
    if options.t_final <= options.t_initial {
        return Err(format_err!(
            "t_final ({0}) must be greater than t_initial ({1})",
            options.t_final,
            options.t_initial
        ));
    } else if options.dt <= 0.0 {
        return Err(format_err!("dt ({}) must be positive", options.dt));
    }

    let mut t = options.t_initial;
    let mut dt = options.dt;
    let mut total_error = vec![0.0; y.len()];

    while t < options.t_final {
        if t + dt > options.t_final {
            dt = options.t_final - t;
        }

        let error_estimate = rkf45_step(y, dydt, t, dt);
        total_error.add(&error_estimate);

        t += dt;
    }
    Ok(total_error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    // Expect estimated numerical error to be within this factor of the actual error.
    // Rough agreement is fine here.
    const ESTIMATED_ERROR_FACTOR: f64 = 5.0;

    // Test a first-order problem:
    //     y' = lambda*y, y(0)=1.
    // The exact solution is y(t)=exp(lambda*t).
    #[test]
    fn test_first_order_problem() -> Result<(), Error> {
        let lambda = -0.1;
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![lambda * y[0]] };
        let y_true = |t: f64| -> f64 { f64::exp(lambda * t) };

        // Record estimated and actual error values over succesively-halved
        // time steps.
        let mut actual_errors = Vec::new();
        let mut estimated_errors = Vec::new();
        for dt in &[1.0, 0.5, 0.25] {
            let options = RkfOptions { t_initial: 0.0, t_final: 3.0, dt: *dt };
            let mut y = [1.0];
            estimated_errors.push(rkf45_advance(&mut y, &dydt, &options)?[0]);
            actual_errors.push((y_true(options.t_final) - y[0]).abs());
        }

        // Sanity-check the errors for the coarsest time step.
        assert!(actual_errors[0] < 1e-4);
        assert!(estimated_errors[0] < ESTIMATED_ERROR_FACTOR * actual_errors[0]);

        // The estimated and actual errors should both converge as dt^4, so each
        // time dt is halved, the error should be reduced by roughly 1/16.
        //
        // Allow a 10% margin versus the expected convergence rate.
        assert!(estimated_errors[1] < estimated_errors[0] / 16.0 * 1.10);
        assert!(estimated_errors[2] < estimated_errors[1] / 16.0 * 1.10);
        assert!(actual_errors[1] < actual_errors[0] / 16.0 * 1.10);
        assert!(actual_errors[2] < actual_errors[1] / 16.0 * 1.10);
        Ok(())
    }

    // Test a second-order problem:
    //     y'' = -y; y(0)=1, y'(0)=0.
    // The exact solution is y(t)=cos(t).
    //
    // To apply the ODE solver, we translate this to the first-order system
    //     y[0]' = y[1]
    //     y[1]' = -y[0].
    // with initial conditions y[0](0)=1, y[1](0)=0.
    #[test]
    fn test_second_order_problem() -> Result<(), Error> {
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![y[1], -y[0]] };
        let y_true = |t: f64| -> f64 { f64::cos(t) };

        // Record estimated and actual error values over succesively-halved
        // time steps.
        let mut actual_errors = Vec::new();
        let mut estimated_errors = Vec::new();
        for dt in &[PI / 4.0, PI / 8.0, PI / 16.0] {
            let options = RkfOptions { t_initial: 0.0, t_final: 2.0 * PI, dt: *dt };
            let mut y = [1.0, 0.0];
            estimated_errors.push(rkf45_advance(&mut y, &dydt, &options)?[0]);
            actual_errors.push((y_true(options.t_final) - y[0]).abs());
        }

        // Sanity-check the errors for the coarsest time step.
        assert!(actual_errors[0] < 5e-3);
        assert!(estimated_errors[0] < ESTIMATED_ERROR_FACTOR * actual_errors[0]);

        // Allow a 10% margin versus the expected convergence rate.
        assert!(estimated_errors[1] < estimated_errors[0] / 16.0 * 1.10);
        assert!(estimated_errors[2] < estimated_errors[1] / 16.0 * 1.10);
        assert!(actual_errors[1] < actual_errors[0] / 16.0 * 1.10);
        assert!(actual_errors[2] < actual_errors[1] / 16.0 * 1.10);
        Ok(())
    }

    // Test a third-order time-variant problem. This is a contrived problem with solution
    //     y(t) = cos(alpha*t^2).
    // As a third-order scalar equation,
    //     y''' + 4*alpha^2*t^2*y' + 12*alpha^2*t*y = 0; y(0)=1, y'(0)=0, y''(0)=0.
    // As a first-order system,
    //     y[0]' = y[1]
    //     y[1]' = y[2].
    //     y[2]' = -12*alpha^2*t*y[0] - 4*alpha^2*t^2*y[1]
    // with initial conditions y[0](0)=1, y[1](0)=0, y[2](0)=0.
    #[test]
    fn test_third_order_problem() -> Result<(), Error> {
        let alpha = 0.1;
        let square = |x: f64| -> f64 { x * x };
        let dydt = |t: f64, y: &[f64]| -> Vec<f64> {
            vec![y[1], y[2], -12.0 * square(alpha) * t * y[0] - 4.0 * square(alpha * t) * y[1]]
        };
        let y_true = |t: f64| -> f64 { f64::cos(alpha * square(t)) };

        // Record estimated and actual error values over succesively-halved
        // time steps.
        let mut actual_errors = Vec::new();
        let mut estimated_errors = Vec::new();
        for dt in &[0.25, 0.125, 0.0625] {
            let options = RkfOptions { t_initial: 0.0, t_final: 4.0, dt: *dt };
            let mut y = [1.0, 0.0, 0.0];
            estimated_errors.push(rkf45_advance(&mut y, &dydt, &options)?[0]);
            actual_errors.push((y_true(options.t_final) - y[0]).abs());
        }

        // Sanity-check the errors for the coarsest time step.
        assert!(actual_errors[0] < 1e-4);
        assert!(estimated_errors[0] < ESTIMATED_ERROR_FACTOR * actual_errors[0]);

        // Allow a 10% margin versus the expected convergence rate.
        assert!(estimated_errors[1] < estimated_errors[0] / 16.0 * 1.10);
        assert!(estimated_errors[2] < estimated_errors[1] / 16.0 * 1.10);
        assert!(actual_errors[1] < actual_errors[0] / 16.0 * 1.10);
        assert!(actual_errors[2] < actual_errors[1] / 16.0 * 1.10);
        Ok(())
    }

    #[test]
    fn test_error_checks() {
        let dydt = |_t: f64, _y: &[f64]| -> Vec<f64> { vec![0.0] };

        assert!(rkf45_advance(
            &mut [1.0],
            &dydt,
            &RkfOptions { t_initial: 2.0, t_final: 1.0, dt: 0.1 }
        )
        .is_err());

        assert!(rkf45_advance(
            &mut [1.0],
            &dydt,
            &RkfOptions { t_initial: 1.0, t_final: 2.0, dt: -0.1 }
        )
        .is_err());
    }
}
