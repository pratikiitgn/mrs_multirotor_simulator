/**
 * @brief Dynamic simulation of a multirotor helicopter.
 *
 * Acknowledgement:
 * * https://github.com/HKUST-Aerial-Robotics/Fast-Planner
 */


// Cable-suspended load
// #ifndef MULTIROTOR_MODEL_H
// #define MULTIROTOR_MODEL_H
#ifndef MULTIROTOR_MODEL_CABLE_SUSPENDED_LOAD_H
#define MULTIROTOR_MODEL_CABLE_SUSPENDED_LOAD_H

// Cable-suspended load
// #define N_INTERNAL_STATES 18
#define N_INTERNAL_STATES 24

#include <boost/array.hpp>
#include "ode/boost/numeric/odeint.hpp"
#include "controllers/references.hpp"

namespace odeint = boost::numeric::odeint;

namespace mrs_multirotor_simulator
{

class MultirotorModel {

public:
  class ModelParams {
  public:
    ModelParams() {

      // | -------- default parameters of the x500 quadrotor -------- |
      // it is recommended to load them through the setParams() method

      n_motors             = 4;
      g                    = 9.81;
      mass                 = 2.0;
      kf                   = 0.00000027087;
      km                   = 0.07;
      prop_radius          = 0.15;
      arm_length           = 0.25;
      body_height          = 0.1;
      motor_time_constant  = 0.03;
      max_rpm              = 7800;
      min_rpm              = 1170;
      air_resistance_coeff = 0.30;

      // Cable-suspended load
      mp                   = 0.1;           // mass of load
      lp                   = 2;             // length of cable

      J       = Eigen::Matrix3d::Zero();
      J(0, 0) = mass * (3.0 * arm_length * arm_length + body_height * body_height) / 12.0;
      J(1, 1) = mass * (3.0 * arm_length * arm_length + body_height * body_height) / 12.0;
      J(2, 2) = (mass * arm_length * arm_length) / 2.0;

      allocation_matrix = Eigen::MatrixXd::Zero(4, 4);

      // clang-format off
    allocation_matrix <<
      -0.707, 0.707, 0.707,  -0.707,
      -0.707, 0.707, -0.707, 0.707,
      -1,     -1,    1,      1,
      1,      1,     1,      1;
      // clang-format on

      allocation_matrix.row(0) *= arm_length * kf;
      allocation_matrix.row(1) *= arm_length * kf;
      allocation_matrix.row(2) *= km * (3.0 * prop_radius) * kf;
      allocation_matrix.row(3) *= kf;

      ground_enabled        = false;
      takeoff_patch_enabled = true;

    }

    int    n_motors;
    double g;
    double mass;
    double kf;
    double km;
    double prop_radius;
    double arm_length;
    double body_height;
    double motor_time_constant;
    double max_rpm;
    double min_rpm;
    double air_resistance_coeff;

    // // Cable-suspended load
    double mp;
    double lp;

    Eigen::Matrix3d J;
    Eigen::MatrixXd allocation_matrix;

    bool   ground_enabled;
    double ground_z;

    bool takeoff_patch_enabled;
  };

  struct State
  {
    Eigen::Vector3d x;
    Eigen::Vector3d v;
    Eigen::Vector3d v_prev;
    Eigen::Matrix3d R;
    Eigen::Vector3d omega;
    Eigen::VectorXd motor_rpm;

    // Cable-suspended load
    Eigen::Vector3d q_cable;
    Eigen::Vector3d q_dot_cable;

  };

  MultirotorModel();

  MultirotorModel(const ModelParams& params, const Eigen::Vector3d& spawn_pos, const double spawn_heading);

  const MultirotorModel::State& getState(void) const;

  void setState(const MultirotorModel::State& state);

  void applyForce(const Eigen::Vector3d& state);

  void setStatePos(const Eigen::Vector3d& pos, const double heading);

  const Eigen::Vector3d& getExternalForce(void) const;
  void                   setExternalForce(const Eigen::Vector3d& force);

  const Eigen::Vector3d& getExternalMoment(void) const;
  void                   setExternalMoment(const Eigen::Vector3d& moment);

  void setInput(const reference::Actuators& input);

  void step(const double& dt);

  typedef boost::array<double, N_INTERNAL_STATES> InternalState;

  void operator()(const MultirotorModel::InternalState& x, MultirotorModel::InternalState& dxdt, const double t);

  Eigen::Vector3d getImuAcceleration() const;

  ModelParams getParams(void);
  void        setParams(const ModelParams& params);

  void initializeState(void);

private:

  void updateInternalState(void);

  MultirotorModel::State state_;

  Eigen::Vector3d imu_acceleration_;

  Eigen::VectorXd input_;
  Eigen::Vector3d external_force_;
  Eigen::Vector3d external_moment_;

  Eigen::Vector3d _initial_pos_;

  ModelParams params_;

  InternalState internal_state_;
};

// --------------------------------------------------------------
// |                       implementation                       |
// --------------------------------------------------------------

/* constructor MultirotorModel //{ */

MultirotorModel::MultirotorModel(void) {

  initializeState();

  updateInternalState();
}

MultirotorModel::MultirotorModel(const MultirotorModel::ModelParams& params, const Eigen::Vector3d& spawn_pos, const double spawn_heading) {

  params_ = params;

  initializeState();

  _initial_pos_ = spawn_pos;

  state_.x = spawn_pos;
  state_.R = Eigen::AngleAxis(-spawn_heading, Eigen::Vector3d(0, 0, 1));

  // Cable-suspended load
  state_.q_cable = Eigen::Vector3d(0.0,0.0,-1.0);

  updateInternalState();
}

//}

/* initializeState() //{ */

void MultirotorModel::initializeState(void) {

  state_.x      = Eigen::Vector3d::Zero();
  state_.v      = Eigen::Vector3d::Zero();
  state_.v_prev = Eigen::Vector3d::Zero();
  state_.R      = Eigen::Matrix3d::Identity();
  state_.omega  = Eigen::Vector3d::Zero();

  // Cable-suspended load
  state_.q_cable      = Eigen::Vector3d(0.0,0.0,-1.0);
  state_.q_dot_cable  = Eigen::Vector3d::Zero();

  imu_acceleration_   = Eigen::Vector3d::Zero();

  state_.motor_rpm    = Eigen::VectorXd::Zero(params_.n_motors);
  input_              = Eigen::VectorXd::Zero(params_.n_motors);

  external_force_.setZero();
  external_moment_.setZero();
}

//}

/* updatedInternalState() //{ */

void MultirotorModel::updateInternalState(void) {

  for (int i = 0; i < 3; i++) {
    internal_state_.at(0 + i)  = state_.x(i);
    internal_state_.at(3 + i)  = state_.v(i);
    internal_state_.at(6 + i)  = state_.R(i, 0);
    internal_state_.at(9 + i)  = state_.R(i, 1);
    internal_state_.at(12 + i) = state_.R(i, 2);
    internal_state_.at(15 + i) = state_.omega(i);

    // Cable-suspended load
    internal_state_.at(18 + i) = state_.q_cable(i);
    internal_state_.at(21 + i) = state_.q_dot_cable(i);

  }
}

//}

/* step() //{ */

void MultirotorModel::step(const double& dt) {

  auto save = internal_state_;

  boost::numeric::odeint::runge_kutta4<InternalState> rk;

  odeint::integrate_n_steps(rk, boost::ref(*this), internal_state_, 0.0, dt, 1);

  for (int i = 0; i < N_INTERNAL_STATES; ++i) {
    if (std::isnan(internal_state_.at(i))) {
      internal_state_ = save;
      break;
    }
  }

  for (int i = 0; i < 3; i++) {
    state_.x(i)     = internal_state_.at(0 + i);
    state_.v(i)     = internal_state_.at(3 + i);
    state_.R(i, 0)  = internal_state_.at(6 + i);
    state_.R(i, 1)  = internal_state_.at(9 + i);
    state_.R(i, 2)  = internal_state_.at(12 + i);
    state_.omega(i) = internal_state_.at(15 + i);

    // Cable-suspended load
    state_.q_cable(i)       = internal_state_.at(18 + i);
    state_.q_dot_cable(i)   = internal_state_.at(21 + i);

  }

  double filter_const = exp((-dt) / (params_.motor_time_constant));

  state_.motor_rpm = filter_const * state_.motor_rpm + (1.0 - filter_const) * input_;

  // Re-orthonormalize R (polar decomposition)
  Eigen::LLT<Eigen::Matrix3d> llt(state_.R.transpose() * state_.R);

  Eigen::Matrix3d P = llt.matrixL();
  Eigen::Matrix3d R = state_.R * P.inverse();
  state_.R          = R;

  // simulate the ground
  if (params_.ground_enabled) {
    if (state_.x(2) < params_.ground_z && state_.v(2) < 0) {
      state_.x(2)  = params_.ground_z;
      state_.v     = Eigen::Vector3d::Zero();
      state_.omega = Eigen::Vector3d::Zero();
    }
  }

  if (params_.takeoff_patch_enabled) {

    const double hover_rpm = sqrt((params_.mass * params_.g) / (params_.n_motors * params_.kf));
    if (input_.mean() <= 0.90 * hover_rpm) {

      if (state_.x(2) < _initial_pos_(2) && state_.v(2) < 0) {
        state_.x(2)  = _initial_pos_(2);
        state_.v     = Eigen::Vector3d::Zero();
        state_.omega = Eigen::Vector3d::Zero();
      }
    } else {
      params_.takeoff_patch_enabled = false;
    }
  }

  // fabricate what the onboard accelerometer would feel
  imu_acceleration_ = state_.R.transpose() * (((state_.v - state_.v_prev) / dt) + Eigen::Vector3d(0, 0, params_.g));
  state_.v_prev     = state_.v;

  // simulate the takeoff patch

  updateInternalState();
}

//}

/* applyModel() //{ */

void MultirotorModel::applyForce(const Eigen::Vector3d& force) {

  external_force_ = force;
}

//}

/* operator() //{ */

void MultirotorModel::operator()(const MultirotorModel::InternalState& x, MultirotorModel::InternalState& dxdt, [[maybe_unused]] const double t) {

  State cur_state;

  for (int i = 0; i < 3; i++) {
    cur_state.x(i)     = x.at(0 + i);
    cur_state.v(i)     = x.at(3 + i);
    cur_state.R(i, 0)  = x.at(6 + i);
    cur_state.R(i, 1)  = x.at(9 + i);
    cur_state.R(i, 2)  = x.at(12 + i);
    cur_state.omega(i) = x.at(15 + i);

  // Cable-suspended load
    cur_state.q_cable(i)       = x.at(18 + i);
    cur_state.q_dot_cable(i)   = x.at(21 + i);

  }

  // Re-orthonormalize R (polar decomposition)
  Eigen::LLT<Eigen::Matrix3d> llt(cur_state.R.transpose() * cur_state.R);
  Eigen::Matrix3d             P = llt.matrixL();
  Eigen::Matrix3d             R = cur_state.R * P.inverse();

  Eigen::Vector3d x_dot;
  Eigen::Vector3d v_dot;
  Eigen::Vector3d omega_dot;
  Eigen::Matrix3d R_dot;

  // Cable-suspended load
  Eigen::Vector3d q_dot_cable_local;
  Eigen::Vector3d q_dot_dot_cable_local;

  Eigen::Matrix3d omega_tensor(Eigen::Matrix3d::Zero());

  omega_tensor(2, 1) = cur_state.omega(0);
  omega_tensor(1, 2) = -cur_state.omega(0);
  omega_tensor(0, 2) = cur_state.omega(1);
  omega_tensor(2, 0) = -cur_state.omega(1);
  omega_tensor(1, 0) = cur_state.omega(2);
  omega_tensor(0, 1) = -cur_state.omega(2);

  Eigen::VectorXd motor_rpm_sq = state_.motor_rpm.array().square();

  Eigen::Vector4d torque_thrust = params_.allocation_matrix * motor_rpm_sq;
  double          thrust        = torque_thrust(3);

  double resistance = params_.air_resistance_coeff * M_PI * (params_.arm_length) * (params_.arm_length) * cur_state.v.norm() * cur_state.v.norm();

  Eigen::Vector3d vnorm = cur_state.v;
  if (vnorm.norm() != 0) {
    vnorm.normalize();
  }

  x_dot = cur_state.v;
  // Cable-suspended load
  q_dot_cable_local        = cur_state.q_dot_cable;
  Eigen::Vector3d u_vector = thrust * R.col(2);

  // Cable-suspended load
  // v_dot = -Eigen::Vector3d(0, 0, params_.g) + thrust * R.col(2) / params_.mass + external_force_ / params_.mass - resistance * vnorm / params_.mass;

  // Gravity terms
  v_dot = - (params_.mass + params_.mp)* Eigen::Vector3d(0, 0, params_.g);
  // Control Input term
  v_dot = v_dot + u_vector  -  (params_.mp / params_.mass) * cur_state.q_cable.cross(cur_state.q_cable.cross(u_vector));
  // External Disturbances
  v_dot = v_dot + external_force_ / params_.mass - resistance * vnorm / params_.mass;
  // Nonlinear term from cable
  v_dot = v_dot + params_.mp * params_.lp * q_dot_cable_local.norm() * q_dot_cable_local.norm() * cur_state.q_cable;
  // final
  v_dot = v_dot / (params_.mass + params_.mp);

  R_dot = R * omega_tensor;

  omega_dot = params_.J.inverse() * (torque_thrust.topRows(3) - cur_state.omega.cross(params_.J * cur_state.omega) + external_moment_);

  // ROS_INFO("u_vector state -> [%2.3f, %2.3f, %2.3f]",u_vector(0), u_vector(1), u_vector(2));

  q_dot_dot_cable_local    = cur_state.q_cable.cross(cur_state.q_cable.cross(u_vector)) - params_.mass * params_.lp * q_dot_cable_local.norm() * q_dot_cable_local.norm() * cur_state.q_cable;
  q_dot_dot_cable_local    = q_dot_dot_cable_local / (params_.mass * params_.lp);

  // ROS_INFO("q_dot_dot_cable_local state -> [%2.3f, %2.3f, %2.3f]",q_dot_dot_cable_local(0), q_dot_dot_cable_local(1), q_dot_dot_cable_local(2));
  // ROS_INFO("cable state frrom hpp file-> [%2.3f, %2.3f, %2.3f]",cur_state.q_cable(0), cur_state.q_cable(1), cur_state.q_cable(2));
  // ROS_INFO("cable dot state -> [%2.3f, %2.3f, %2.3f]",cur_state.q_dot_cable(0), cur_state.q_dot_cable(1), cur_state.q_dot_cable(2));

  for (int i = 0; i < 3; i++) {
    dxdt.at(0 + i)  = x_dot(i);
    dxdt.at(3 + i)  = v_dot(i);
    dxdt.at(6 + i)  = R_dot(i, 0);
    dxdt.at(9 + i)  = R_dot(i, 1);
    dxdt.at(12 + i) = R_dot(i, 2);
    dxdt.at(15 + i) = omega_dot(i);

    // Cable-suspended load
    dxdt.at(18 + i) = cur_state.q_dot_cable(i);
    dxdt.at(21 + i) = q_dot_dot_cable_local(i);

  }

  for (int i = 0; i < N_INTERNAL_STATES; ++i) {
    if (std::isnan(dxdt.at(i))) {
      dxdt.at(i) = 0;
    }
  }
}

//}

// | ------------------- setters and getters ------------------ |

/* setParams() //{ */

void MultirotorModel::setParams(const MultirotorModel::ModelParams& params) {

  params_ = params;
}

//}

/* getParams() //{ */

MultirotorModel::ModelParams MultirotorModel::getParams(void) {

  return params_;
}

//}

/* setInput() //{ */

void MultirotorModel::setInput(const reference::Actuators& input) {

  for (int i = 0; i < params_.n_motors; i++) {

    double val = input.motors(i);

    if (!std::isfinite(val)) {
      val = 0;
    }

    if (val < 0.0) {
      val = 0.0;
    } else if (val > 1.0) {
      val = 1.0;
    }

    input_(i) = params_.min_rpm + (params_.max_rpm - params_.min_rpm) * val;
  }
}

//}

/* getState() //{ */

const MultirotorModel::State& MultirotorModel::getState(void) const {
  return state_;
}

//}

/* setState() //{ */

void MultirotorModel::setState(const MultirotorModel::State& state) {

  state_.x         = state.x;
  state_.v         = state.v;
  state_.R         = state.R;
  state_.omega     = state.omega;
  state_.motor_rpm = state.motor_rpm;

  // Cable-suspended load
  state_.q_cable        = state.q_cable;
  state_.q_dot_cable    = state.q_dot_cable;

  updateInternalState();
}

//}

/* setStatePos() //{ */

void MultirotorModel::setStatePos(const Eigen::Vector3d& pos, const double heading) {

  _initial_pos_ = pos;
  state_.x      = pos;
  state_.R      = Eigen::AngleAxis(-heading, Eigen::Vector3d(0, 0, 1));
  state_.q_cable     = Eigen::Vector3d(0,0,-1);

  updateInternalState();
}

//}

/* getExternalForce() //{ */

const Eigen::Vector3d& MultirotorModel::getExternalForce(void) const {
  return external_force_;
}

//}

/* setExternalForce() //{ */

void MultirotorModel::setExternalForce(const Eigen::Vector3d& force) {
  external_force_ = force;
}

//}

/* getExternalMoment() //{ */

const Eigen::Vector3d& MultirotorModel::getExternalMoment(void) const {
  return external_moment_;
}

//}

/* setExternalMoment() //{ */

void MultirotorModel::setExternalMoment(const Eigen::Vector3d& moment) {
  external_moment_ = moment;
}

//}

/* getImuAcceleration() //{ */

Eigen::Vector3d MultirotorModel::getImuAcceleration() const {
  return imu_acceleration_;
}

//}


}  // namespace mrs_multirotor_simulator

#endif
