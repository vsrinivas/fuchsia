use failure::{format_err, Error};

/// Runge-Kutta Fehlberg 4(5) parameters
///
/// These are readily available in literature, e.g.
/// <https://en.wikipedia.org/wiki/Runge%E2%80%93Kutta%E2%80%93Fehlberg_method>.
mod rkf45_params {
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

/// Parameters for adaptive time-stepping logic.
///
/// These incorporate a variety of heuristics and are mostly described in Press and Teukolsky's
/// [Adaptive Stepsize Runge-Kutta Integration, Computers in Physics 6, 188
/// (1992)](https://doi.org/10.1063/1.4823060).
mod adaptive_stepping_params {
    /// Safety factor to use when modifying time steps. This makes a revised time step slightly
    /// smaller than required by formal analysis, providing margin for inaccuracy in the error
    /// estimate.
    pub static SAFETY_FACTOR: f64 = 0.9;

    /// Only refine/coarsen the time step when the ratio of estimate error to desired error
    /// crosses these thresholds. Doing so prevents excessive recalculation of time steps.
    pub static ERROR_RATIO_REFINING_THRESHOLD: f64 = 1.1;
    pub static ERROR_RATIO_COARSENING_THRESHOLD: f64 = 0.5;

    /// RKF45's local truncation (single-step) error can be written as
    ///     E(dt) = C*dt^5 + O(dt^6).
    ///
    /// Suppose the desired error D is fixed with respect to dt. Then the optimal step size
    /// approximately satisfies
    ///     C*dt_opt^5 = D  ==> dt_opt = (D / C)^(1/5).
    /// If we have attempted a time step of size dt, then we can approximate
    ///     C = E(dt) / dt^5.
    /// This yields
    ///     dt_opt = dt * (D / E(dt))^(1/5) = (E(dt) / D)^(-1/5).
    ///
    /// Suppose instead that D is proprotional to dt, so D(dt) = D1*dt. Then the optimal step size
    /// satisfies
    ///     C*dt_opt^5 = D1*dt_opt  ==>  C = (D1 / C)^(1/4).
    /// If we have attempted a time step of size dt, with estimated error E(dt) and desired error
    /// D(dt), then we observe that
    ///     D(dt) / E(dt) = (D1*dt) / (C*dt^5)
    ///     ==> D1 / C = dt^4 (D(dt) / E(dt)).
    /// Using this relationship above yields
    ///     dt_opt = dt * (D(dt) / E(dt))^(1/4) = (E(dt) / D(dt))^(-1/4).
    ///
    /// Following the guidance of Press and Teukolsky, rather than inspect the form of the desired
    /// error, we simply use the more conservative exponent depending on the situation. When
    /// refining, we will have E/D > 1. Raising to the smaller exponent (-0.25) will yield a
    /// smaller time step. Converseley, when coarsening we have E/D < 1, and the larger exponent
    /// (-0.20) will yield a smaller timestep.
    pub static REFINING_EXPONENT: f64 = -0.25;
    pub static COARSENING_EXPONENT: f64 = -0.20;

    /// Bounds for the factor used when refining/coarsening the time step, to avoid too much of
    /// a change at once.
    pub static MIN_REFINING_FACTOR: f64 = 0.2;
    pub static MAX_COARSENING_FACTOR: f64 = 5.0;
}

/// Vector operations to implement on `[f64]` for convenience.
///
/// All methods involving another [f64] expect the two slices to be of equal length. The caller
/// is responsible for ensuring this. (Practically speaking, the slices involved are from `Vec`s
/// created using the length of `rkf45_adaptive`'s input `y`.)
trait VectorOperations {
    /// Scale by a scalar.
    fn scale(&mut self, a: f64);

    /// Copy to this vector from another one. Slice lengths must be equal.
    fn copy_from(&mut self, x: &Self);

    /// Add a vector to this one. Slice lengths must be equal.
    fn add(&mut self, x: &Self);

    /// Add a scalar times another vector. Slice lengths must be equal.
    fn add_ax(&mut self, a: f64, x: &Self);

    /// Subtract a vector from this one. Slice lengths must be equal.
    fn subtract(&mut self, x: &Self);

    /// Sets all elements to zero.
    fn to_zeros(&mut self);
}

impl VectorOperations for [f64] {
    fn scale(&mut self, a: f64) {
        self.iter_mut().for_each(|p| *p = *p * a);
    }

    fn copy_from(&mut self, x: &Self) {
        self.iter_mut().zip(x.iter()).for_each(|(p, q)| *p = *q);
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
/// - `y`: Slice containing solution values at the beginning of the time step. This will be
///    updated in-place.
/// - `dydt`: Function that evaluates the time derivative of `y`.
/// - `tn`: Time value at the beginning of the step.
/// - `dt`: Length of the step.
///
/// Returns:
/// - Vector estimating elementwise numerical error over this single step, and the maximum ratio of
///   estimated error to error bound.
fn rkf45_step(
    y: &mut [f64],
    dydt: &impl Fn(f64, &[f64]) -> Vec<f64>,
    tn: f64,
    dt: f64,
    error_control: &ErrorControlOptions,
) -> (Vec<f64>, f64) {
    // Work array reused by several loops below.
    let mut work = vec![0.0; y.len()];

    let mut k = Vec::with_capacity(6);
    k.push(dydt(tn, y));

    use rkf45_params as params;
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
    let mut y_5th_order = y.to_vec();
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

    let mut max_error_ratio = 0.0;
    for i in 0..y.len() {
        // We use the fact that k[0] stores dydt(tn, yn).
        let error_bound = error_control.absolute_magnitude
            + error_control.relative_magnitude
                * (error_control.function_scale * y[i].abs()
                    + error_control.derivative_scale * dt * k[0][i].abs());
        max_error_ratio = f64::max(max_error_ratio, error_estimate[i] / error_bound);
    }

    (error_estimate, max_error_ratio)
}

/// Options to configure adaptive time-stepping.
pub struct AdaptiveOdeSolverOptions {
    /// Initial time for the solution.
    pub t_initial: f64,
    /// Final time for the solution.
    pub t_final: f64,
    /// Length of first attempted time step.
    pub dt_initial: f64,
    /// Parameters that determine the error bounds used to accept or reject time steps.
    pub error_control: ErrorControlOptions,
}

/// Options for computing desired error when performing adaptive time-stepping.
///
/// `rkf45_adaptive` compares its estimate to a desired error, which is computed as
/// ```
/// D = max(absolute_magnitude + relative_magnitude * (function_scale * y[i] + derivative_scale * dt * dydt[i]))
/// ```
/// where `[i]` denotes the `i`th component, and the max is taken over all components.
///
/// This form supports a variety of ways of controlling the size of the desired error
/// relative to `y` itself or to its increments.
pub struct ErrorControlOptions {
    /// Magnitude of the absolute component of desired error. Even if relative error is the primary
    /// feature of interest, this must be set to a nonzero value as a safety measure in case
    /// both y and dydt are near zero.
    pub absolute_magnitude: f64,
    /// Magnitude of the relative component of desired error.
    pub relative_magnitude: f64,
    /// Contribution of `y` to the relative component of desired error.
    pub function_scale: f64,
    /// Contribution of `y`'s increment (`dt * dydt`) to the relative component of desired error.
    pub derivative_scale: f64,
}

impl ErrorControlOptions {
    /// A simple error control option that sets the desired error to
    /// ```
    /// D = max(scale * (1 + y[i] + dt * dydt[i]))
    /// ```
    /// This has a lower bound of `scale`, but it grows proportional to `y` or `dydt` as either one
    /// becomes large.
    pub fn simple(scale: f64) -> ErrorControlOptions {
        ErrorControlOptions {
            absolute_magnitude: scale,
            relative_magnitude: scale,
            function_scale: 1.0,
            derivative_scale: 1.0,
        }
    }
}

/// Solves an initial value problem using RKF45 with adaptive time-stepping.
///
/// The method of error control and time step refinement are described in
/// Press and Teukolsky's [Adaptive Stepsize Runge-Kutta Integration, Computers in Physics 6,
/// 188 (1992)](https://doi.org/10.1063/1.4823060).
///
/// Args:
/// - `y`: Slice containing the initial value of the solution. This will be updated in-place.
/// - `dydt`: Function that evaluates the time derivative of `y`.
/// - `options`: Specifies parameters for the solver.
///
/// Returns:
/// - Vector estimating total elementwise numerical error incurred by integration.
pub fn rkf45_adaptive(
    y: &mut [f64],
    dydt: &impl Fn(f64, &[f64]) -> Vec<f64>,
    options: &AdaptiveOdeSolverOptions,
) -> Result<Vec<f64>, Error> {
    macro_rules! validate_input {
        ($x:expr) => {
            if !($x) {
                return Err(format_err!("Failed input validation: {}", stringify!($x)));
            }
        };
    }

    validate_input!(options.t_final > options.t_initial);
    validate_input!(options.dt_initial > 0.0);
    validate_input!(options.error_control.absolute_magnitude > 0.0);
    validate_input!(options.error_control.relative_magnitude >= 0.0);
    validate_input!(options.error_control.function_scale >= 0.0);
    validate_input!(options.error_control.derivative_scale >= 0.0);

    let mut t = options.t_initial;
    let mut dt = options.dt_initial;
    let mut total_error = vec![0.0; y.len()];

    let mut work = y.to_vec();
    while t < options.t_final {
        if t + dt > options.t_final {
            dt = options.t_final - t;
        }

        let (error_estimate, max_error_ratio) =
            rkf45_step(&mut work, dydt, t, dt, &options.error_control);
        use adaptive_stepping_params as params;
        if max_error_ratio < params::ERROR_RATIO_REFINING_THRESHOLD {
            // Accept the step, and update time.
            y.copy_from(&work);
            total_error.add(&error_estimate);
            t += dt;
            if max_error_ratio < params::ERROR_RATIO_COARSENING_THRESHOLD {
                // Error was particularly small, so increase step size.
                let factor = f64::min(
                    params::SAFETY_FACTOR * max_error_ratio.powf(params::COARSENING_EXPONENT),
                    params::MAX_COARSENING_FACTOR,
                );
                dt *= factor;
            }
        } else {
            // Throw out this time stemp. Revert the work array, and reduce dt.
            work.copy_from(y);
            let factor = f64::max(
                params::SAFETY_FACTOR * max_error_ratio.powf(params::REFINING_EXPONENT),
                params::MIN_REFINING_FACTOR,
            );
            dt *= factor;
        }
    }

    Ok(total_error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    macro_rules! assert_lt {
        ($x:expr, $y:expr) => {
            assert!(
                $x < $y,
                "assertion {} < {} failed ({} vs. {})",
                stringify!($x),
                stringify!($y),
                $x,
                $y
            );
        };
    }

    macro_rules! assert_gt {
        ($x:expr, $y:expr) => {
            assert!(
                $x > $y,
                "assertion {} > {} failed ({} vs. {})",
                stringify!($x),
                stringify!($y),
                $x,
                $y
            );
        };
    }

    // rkf45_step requires ErrorControlOptions as an input. But when performing convergence
    // tests on that function, the options are meaningless. This is named to document that fact.
    static MEANINGLESS_OPTIONS: ErrorControlOptions = ErrorControlOptions {
        absolute_magnitude: 1e-8,
        relative_magnitude: 1e-8,
        function_scale: 1.0,
        derivative_scale: 1.0,
    };

    // Test rkf45_step on a first-order problem:
    //     y' = lambda*y, y(0)=1.
    // The exact solution is y(t)=exp(lambda*t).
    //
    // rkf45_step should be accurate up to O(dt^5) over a single time step (local truncation
    // error). Its error estimate should be accurate up to O(dt). We test both expectations
    // by the way the numerical errors decrease as dt is refined.
    #[test]
    fn test_first_order_problem_rkf45_step() {
        let lambda = -0.1;
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![lambda * y[0]] };
        let y_true = |t: f64| -> f64 { f64::exp(lambda * t) };

        // Record the actual error values, and the errors in estimated error values (i.e.
        // how close the estimated numerical error is to the actual numerical error) over
        // succesively-halved time steps.
        let mut actual_errors = Vec::new();
        let mut errors_in_estimated_error = Vec::new();
        for dt in &[1.0, 0.5, 0.25] {
            let mut y = [1.0];
            let estimated_error = rkf45_step(&mut y, &dydt, 0.0, *dt, &MEANINGLESS_OPTIONS).0[0];
            let actual_error = (y_true(*dt) - y[0]).abs();
            errors_in_estimated_error.push((estimated_error - actual_error).abs());
            actual_errors.push(actual_error);
        }

        // Each time dt is halved, actual_errors should shrink by roughly 1/32, and
        // errors_in_estimated_error should shrink by roughly 1/2. The factor of 1.10 allows a 10%
        // margin versus the expected convergence rate.
        assert_lt!(actual_errors[1], actual_errors[0] / 32.0 * 1.10);
        assert_lt!(actual_errors[2], actual_errors[1] / 32.0 * 1.10);
        assert_lt!(errors_in_estimated_error[1], errors_in_estimated_error[0] / 2.0 * 1.10);
        assert_lt!(errors_in_estimated_error[2], errors_in_estimated_error[1] / 2.0 * 1.10);
    }

    // Test rkf45_adaptive on the same first-order problem as above.
    //
    // This checks that we integrate to t_final with the requested degree of accuracy.
    #[test]
    fn test_first_order_problem_rkf45_adaptive() -> Result<(), Error> {
        let lambda = -0.1;
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![lambda * y[0]] };
        let y_true = |t: f64| -> f64 { f64::exp(lambda * t) };

        let options = AdaptiveOdeSolverOptions {
            t_initial: 0.0,
            t_final: 3.0,
            dt_initial: 0.1,
            error_control: ErrorControlOptions::simple(1e-6),
        };
        let mut y = [1.0];
        rkf45_adaptive(&mut y, &dydt, &options)?;
        let actual_error = (y_true(options.t_final) - y[0]).abs();

        // Error should be near the size specified, but not smaller than it.
        assert_lt!(actual_error, 1e-5);
        assert_gt!(actual_error, 1e-7);

        Ok(())
    }

    // Test rkf45_step for a second-order problem:
    //     y'' = -y; y(0)=1, y'(0)=0.
    // The exact solution is y(t)=cos(t).
    //
    // To apply the ODE solver, we translate this to the first-order system
    //     y[0]' = y[1]
    //     y[1]' = -y[0].
    // with initial conditions y[0](0)=1, y[1](0)=0.
    //
    // This test follows the same methodology as test_first_order_problem_rkf45_step;
    // see it for detailed documentation.
    #[test]
    fn test_second_order_problem_rkf45_step() {
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![y[1], -y[0]] };
        let y_true = |t: f64| -> f64 { f64::cos(t) };

        let mut actual_errors = Vec::new();
        let mut errors_in_estimated_error = Vec::new();
        for dt in &[PI / 4.0, PI / 8.0, PI / 16.0] {
            let mut y = [1.0, 0.0];
            let estimated_error = rkf45_step(&mut y, &dydt, 0.0, *dt, &MEANINGLESS_OPTIONS).0[0];
            let actual_error = (y_true(*dt) - y[0]).abs();
            errors_in_estimated_error.push((estimated_error - actual_error).abs());
            actual_errors.push(actual_error);
        }

        assert_lt!(actual_errors[1], actual_errors[0] / 32.0 * 1.10);
        assert_lt!(actual_errors[2], actual_errors[1] / 32.0 * 1.10);
        assert_lt!(errors_in_estimated_error[1], errors_in_estimated_error[0] / 2.0 * 1.10);
        assert_lt!(errors_in_estimated_error[2], errors_in_estimated_error[1] / 2.0 * 1.10);
    }

    // Test rkf45_adaptive for the same second-order problem as above.
    #[test]
    fn test_second_order_problem_rkf45_adaptive() -> Result<(), Error> {
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![y[1], -y[0]] };
        let y_true = |t: f64| -> f64 { f64::cos(t) };

        let options = AdaptiveOdeSolverOptions {
            t_initial: 0.0,
            t_final: 2.0 * PI,
            dt_initial: PI / 4.0,
            error_control: ErrorControlOptions::simple(1e-6),
        };
        let mut y = [1.0, 0.0];
        rkf45_adaptive(&mut y, &dydt, &options)?;
        let actual_error = (y_true(options.t_final) - y[0]).abs();

        // Error should be near the size specified, but not smaller than it.
        assert_lt!(actual_error, 1e-4);
        assert_gt!(actual_error, 1e-7);

        Ok(())
    }

    // Test rkf45_step for a third-order time-variant problem.
    //
    // This is a contrived problem with solution
    //     y(t) = cos(alpha*t^2).
    // As a third-order scalar equation,
    //     y''' + 4*alpha^2*t^2*y' + 12*alpha^2*t*y = 0; y(0)=1, y'(0)=0, y''(0)=0.
    // As a first-order system,
    //     y[0]' = y[1]
    //     y[1]' = y[2].
    //     y[2]' = -12*alpha^2*t*y[0] - 4*alpha^2*t^2*y[1]
    // with initial conditions y[0](0)=1, y[1](0)=0, y[2](0)=0.
    //
    // This test follows the same methodology as test_first_order_problem_rkf45_step;
    // see it for detailed documentation.
    #[test]
    fn test_third_order_problem_rkf45_step() {
        let alpha = 0.1;
        let square = |x: f64| -> f64 { x * x };
        let dydt = |t: f64, y: &[f64]| -> Vec<f64> {
            vec![y[1], y[2], -12.0 * square(alpha) * t * y[0] - 4.0 * square(alpha * t) * y[1]]
        };
        let y_true = |t: f64| -> f64 { f64::cos(alpha * square(t)) };

        let mut actual_errors = Vec::new();
        let mut errors_in_estimated_error = Vec::new();
        for dt in &[0.25, 0.125, 0.0625] {
            let mut y = [1.0, 0.0, 0.0];
            let estimated_error = rkf45_step(&mut y, &dydt, 0.0, *dt, &MEANINGLESS_OPTIONS).0[0];
            let actual_error = (y_true(*dt) - y[0]).abs();
            errors_in_estimated_error.push((estimated_error - actual_error).abs());
            actual_errors.push(actual_error);
        }

        assert_lt!(actual_errors[1], actual_errors[0] / 32.0 * 1.10);
        assert_lt!(actual_errors[2], actual_errors[1] / 32.0 * 1.10);
        assert_lt!(errors_in_estimated_error[1], errors_in_estimated_error[0] / 2.0 * 1.10);
        assert_lt!(errors_in_estimated_error[2], errors_in_estimated_error[1] / 2.0 * 1.10);
    }
    // This tests rkf45_adaptive for the same third-order problem as above.
    #[test]
    fn test_third_order_problem_rkf45_adaptive() -> Result<(), Error> {
        let alpha = 0.1;
        let square = |x: f64| -> f64 { x * x };
        let dydt = |t: f64, y: &[f64]| -> Vec<f64> {
            vec![y[1], y[2], -12.0 * square(alpha) * t * y[0] - 4.0 * square(alpha * t) * y[1]]
        };
        let y_true = |t: f64| -> f64 { f64::cos(alpha * square(t)) };

        let options = AdaptiveOdeSolverOptions {
            t_initial: 0.0,
            t_final: 4.0,
            dt_initial: 0.25,
            error_control: ErrorControlOptions::simple(1e-6),
        };
        let mut y = [1.0, 0.0, 0.0];
        rkf45_adaptive(&mut y, &dydt, &options)?;
        let actual_error = (y_true(options.t_final) - y[0]).abs();

        // Error should be near the size specified, but not smaller than it.
        assert_lt!(actual_error, 1e-4);
        assert_gt!(actual_error, 1e-7);

        Ok(())
    }

    // Test rkf45_adaptive on a problem with multiple time scales.
    //
    // This is the canonical use case for adaptive time-stepping. We have a problem with two time
    // scales, one that is much faster than the other. Our t_final is 1, and we suggest a single
    // step of length 1, which would be plenty for the slow time scale. However, the fast time
    // scale requires much smaller time steps, and the integrator must detect this.
    #[test]
    fn test_multiple_time_scales() -> Result<(), Error> {
        let lambda1 = 10.0;
        let lambda2 = 0.001;
        let dydt = |_t: f64, y: &[f64]| -> Vec<f64> { vec![-lambda1 * y[0], -lambda2 * y[1]] };
        let y_true = |t: f64| -> Vec<f64> { vec![f64::exp(-lambda1 * t), f64::exp(-lambda2 * t)] };

        let options = AdaptiveOdeSolverOptions {
            t_initial: 0.0,
            t_final: 1.0,
            dt_initial: 1.0,
            error_control: ErrorControlOptions::simple(1e-6),
        };
        let mut y = [1.0, 1.0];
        rkf45_adaptive(&mut y, &dydt, &options)?;
        let mut actual_error = y_true(options.t_final);
        actual_error.iter_mut().zip(y.iter()).for_each(|(p, q)| *p = (*p - *q).abs());

        // Our requested error scale bounds the error in both components above. The second component
        // has a much smaller error than our requested tolerance because it varies so slowly relative
        // to the first, which governs the time step selection. Hence, we don't test actual_error[1]
        // for a lower bound.
        assert_lt!(actual_error[0], 1e-5);
        assert_lt!(actual_error[1], 1e-5);
        assert_gt!(actual_error[0], 1e-7);

        Ok(())
    }

    // Test that rkf45_advance returns an error when passed invalid options.
    #[test]
    fn test_error_checks() {
        let dydt = |_t: f64, _y: &[f64]| -> Vec<f64> { vec![0.0] };

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 2.0, // Greater than t_final
                t_final: 1.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions::simple(1e-8),
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: -0.1, // Negative
                error_control: ErrorControlOptions {
                    absolute_magnitude: 1e-8,
                    relative_magnitude: 1e-8,
                    function_scale: 1.0,
                    derivative_scale: 1.0,
                }
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions {
                    absolute_magnitude: -1e-8, // Negative
                    relative_magnitude: 1e-8,
                    function_scale: 1.0,
                    derivative_scale: 1.0,
                }
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions {
                    absolute_magnitude: 1e-8,
                    relative_magnitude: -1e-8, // Negative
                    function_scale: 1.0,
                    derivative_scale: 1.0,
                }
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions {
                    absolute_magnitude: 1e-8,
                    relative_magnitude: 1e-8,
                    function_scale: -1.0, // Negative
                    derivative_scale: 1.0,
                }
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions {
                    absolute_magnitude: 1e-8,
                    relative_magnitude: 1e-8,
                    function_scale: 1.0,
                    derivative_scale: -1.0, // Negative
                }
            }
        )
        .is_err());

        assert!(rkf45_adaptive(
            &mut [1.0],
            &dydt,
            &AdaptiveOdeSolverOptions {
                t_initial: 1.0,
                t_final: 2.0,
                dt_initial: 0.1,
                error_control: ErrorControlOptions {
                    absolute_magnitude: 0.0, // Must be positive
                    relative_magnitude: 1e-8,
                    function_scale: 1.0,
                    derivative_scale: 1.0,
                }
            }
        )
        .is_err());
    }
}
