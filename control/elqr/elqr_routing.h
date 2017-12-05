#pragma once
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include "control/util.h"

class ELQRPath {
 public:
  // X state: [x, y, v, theta, thetadot]. Assume that wind is coming from +x axis.
  static constexpr int N = 5;
  static constexpr int M = 2;
  typedef Eigen::Matrix<double, M, 1> MatrixM1d;
  typedef Eigen::Matrix<double, N, 1> MatrixN1d;
  typedef Eigen::Matrix<double, M, M> MatrixMMd;
  typedef Eigen::Matrix<double, N, N> MatrixNNd;
  typedef Eigen::Matrix<double, N, M> MatrixNMd;
  typedef Eigen::Matrix<double, M, N> MatrixMNd;
  typedef MatrixN1d State;
  typedef MatrixM1d Input;

  ELQRPath(State xi, State xl);

  void Run();

  void FooMain();

 private:
  static constexpr double dt = 0.02;
  // Dynamics constants
  const double kRudder = .5; // If U in J, kRudder in rad / (s * J * m/s) = rad / (J*m)
  static constexpr double kVel = 1; // In s^-1
  static constexpr double kTheta = 3.;
  const double kMaxRudder = 1; // Maximum rudder input value
  // Wind Circle constants
  const float kIrons = M_PI / 6;
  const float kBeamSpeed = 1;
  const float kBeam = M_PI / 2;
  const float kBroad = 3. * M_PI / 4.;
  const float kDownSpeed = .5;

  State F(State X, Input U);
  State G(State X, Input U, bool forwards, bool lin = true);
  double CostWork(State X, Input U, State goal, MatrixNNd Q);
  double StepCost(State X, Input U);
  double EndCost(State X);
  double StartCost(State X, Input U);

  void CalcSbar(int t);
  void CalcS(int t);
  State StateFromS(int t);

  // Use this to turn on/off constraining of the start position:
  const bool kConstrainStart = false;
  void BackIter();
  void ForwardsIter();

  double WindCircle(double theta);
  double DeltaWindCircle(double theta); // dWindCircle / dtheta

  double CalcBaseRudder(double theta);
  double DeltaBaseRudder(); // dCalcBaseRudder / dtheta

  // Linearized dynamics/cost variables, updated constantly
  MatrixNNd A_;
  MatrixNMd B_;
  MatrixN1d c_;
  MatrixNNd Abar_;
  MatrixNMd Bbar_;
  MatrixN1d cbar_;
  MatrixNNd Q_;
  MatrixMMd R_;
  MatrixN1d q_;
  MatrixM1d r_;
  MatrixMNd P_;

  // State variables that constantly get updated.
  static constexpr int L = 500; // Number of timesteps
  std::array<State, L+1> x_;
  std::array<std::function<Input(State)>, L> pi_;
  std::array<std::function<Input(State)>, L> pibar_;
  std::array<State, L+1> s_;
  std::array<MatrixNNd, L+1> S_;
  std::array<State, L+1> sbar_;
  std::array<MatrixNNd, L+1> Sbar_;

  // Things that stay constant for each run
  float wind_speed_ = 5.0;
  float nom_heading_ = 1.5;
  const double kKrudder = 1.0;
  State xi_; // Start
  State xl_; // goal
};
