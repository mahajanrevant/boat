#include "elqr_routing.h"
#include <cmath>
#include <iostream>

constexpr int ELQRPath::N;
constexpr int ELQRPath::M;
constexpr int ELQRPath::L;
constexpr double ELQRPath::dt;

ELQRPath::ELQRPath(State xi, State xl) {
  A_.setIdentity();
  B_.setZero();
  c_.setZero();
  Abar_.setIdentity();
  Bbar_.setZero();
  cbar_.setZero();
  Q_.setIdentity();
  R_.setIdentity();
  q_.setIdentity();
  r_.setIdentity();
  P_.setZero();

  xi_ = xi;
  xl_ = xl;

  std::function<Input(State)> zero_fun = [](State) { return Input::Zero(); };
  for (int i = 0; i < L; ++i) {
    x_[i] = xi_;
    pi_[i] = zero_fun;
    pibar_[i] = zero_fun;
    s_[i].setZero();
    S_[i].setZero();
    sbar_[i].setZero();
    Sbar_[i].setZero();
  }
  x_[L] = xi_;
  s_[L].setZero();
  S_[L].setZero();
  sbar_[L].setZero();
  Sbar_[L].setZero();
  //x_[L-1] = xl_; TODO(james): Do I need this?
}

void ELQRPath::Run() {
  for (int i = 0; i < 15; ++i) {
    std::cout << "i: " << i << "\n";
    BackIter();
    ForwardsIter();
  }
  std::cout << "t,x0,x1,x2,x3,thetadot,urudder\n";
  for (int t = 0; t < L; ++t) {
    State x = x_[t];
    double u = pi_[t](x)(0, 0);
    std::cerr << t << "," << x(0, 0) << "," << x(1, 0) << "," << x(2, 0) << ","
              << x(3, 0) << "," << x(4, 0) << "," << CalcBaseRudder(x(3, 0)) + u
              << "\n";
  }
  State x = x_[L];
  std::cerr << L << "," << x(0, 0) << "," << x(1, 0) << "," << x(2, 0) << ","
              << x(3, 0) << "," << x(4, 0) << ",0\n";
}

ELQRPath::State ELQRPath::F(State X, Input U) {
  double v = X(2, 0);
  double theta = X(3, 0);
  double thetadot = X(4, 0);
  double rudder = CalcBaseRudder(theta) + U(0, 0);
  double sail = 1. + std::max(-1., std::min(0., U(1, 0)));
  State xdot;
  xdot(0, 0) = v * std::cos(theta); // xdot
  xdot(1, 0) = v * std::sin(theta); // ydot
  xdot(2, 0) = (WindCircle(theta) * sail - v) * kVel; // vdot
  xdot(3, 0) = thetadot;
  xdot(4, 0) = v * kRudder *
                   std::max(std::min(rudder, kMaxRudder), -kMaxRudder) -
               kTheta * thetadot;
  return xdot;
}

ELQRPath::State ELQRPath::G(State X, Input U, bool forwards, bool lin) {
  typedef Eigen::Matrix<double, M+N, 1> XDim;
  std::function<XDim(double, XDim)> f = [this](double t, XDim x) {
    x.block(0, 0, N, 1) = F(x.block(0, 0, N, 1), x.block(N, 0, M, 1));
    return x;
  };
  XDim Xaug;
  Xaug << X, U;
  sailbot::util::RungeKutta4(f, Xaug, 0, dt * (forwards ? 1 : -1));
  if (lin) {
    double theta = X(3, 0);
    const double v = X(2, 0);
    const double st = std::sin(theta);
    const double ct = std::cos(theta);
    const double base_rudder = CalcBaseRudder(theta);
    // 0.01 to avoid singular jacobian
    double rudder_factor = (std::abs(U(0, 0)) > kMaxRudder) ? 0.003 : 1;
    const double sail = 1. + std::max(-1., std::min(0., U(1, 0)));
    const double dtdotdrudder = rudder_factor * dt * v * kRudder;
    constexpr double eKvt = std::exp(-kVel * dt);
    constexpr double eKtt = std::exp(-kTheta * dt);
    // State order reminder:
    // [x, y, v, theta, thetadot]
    A_ << 1, 0, dt * ct, -dt * v * st, 0,
          0, 1, dt * st, dt * v * ct, 0,
          0, 0, eKvt, (1 - eKvt) * sail * DeltaWindCircle(theta), 0,
          0, 0, 0, 1, dt,
          0, 0, rudder_factor * dt * kRudder * (base_rudder + U(0, 0)), dtdotdrudder * DeltaBaseRudder(), eKtt;
    B_ << 0, 0,
          0, 0,
          0, (1 - eKvt) * WindCircle(theta),
          0, 0,
          dtdotdrudder, 0;
    std::cout << "A: " << A_ << "\nB: " << B_ << std::endl;
    if (A_.hasNaN() || B_.hasNaN()) {
      std::abort();
    }
    if (forwards) {
      c_ << Xaug.block(0, 0, N, 1) - A_ * X - B_ * U;
    } else {
      XDim Xfor;
      Xfor << X, U;
      sailbot::util::RungeKutta4(f, Xfor, 0, dt);
      c_ << -A_ * X - B_ * U + Xfor.block(0, 0, N, 1);
    }
    Abar_ = A_.inverse();
    Bbar_ = -Abar_ * B_;
    cbar_ = -Abar_ * c_;
  }
  return Xaug.block(0, 0, N, 1);
}

// Does quadratic cost for everything but theta, with R=1.
double ELQRPath::CostWork(State X, Input U, State goal,
                          MatrixNNd Q) {
  const double rudder = CalcBaseRudder(X(3, 0)) + U(0, 0);
  const double Rrud =
      (std::abs(X(2, 0)) + 0.1) * (std::abs(rudder) > kMaxRudder ? .1 : 100);
  //State diff = goal - X;
  // TODO(james): Determine whether adding cost to point towards goal is good
  double goalt = nom_heading_;//std::atan2(diff(1, 0), diff(0, 0));
  //double tdiff = sailbot::util::norm_angle(goalt - X(3, 0));
  double QTheta = Q(3, 3);
  Q(3, 3) = 0;
//  double cost =
//      .5 * (diff.transpose() * Q * diff + U.transpose() * R_ * U)(0, 0) +
//      .5 * QTheta * tdiff * tdiff;
  // d(tdiff^2) / dx= d((atan(Y/X) - t)^2) / dx.
  // Assume atan(Y/X)=goalt approx. constant.
  // =d((goalt - t)^2) / dx
  // therefore, identical to other elements.
  Q(3, 3) = QTheta;
  goal(3, 0) = goalt; // foobar

  //Q_.setIdentity(); // foobar
  //Q = Q_ * Q(0, 0); // foobar

  // To compute hessian
  Q_ = Q; // hessian of cost w.r.t. X
  R_(0, 0) = Rrud;
  R_(1, 1) = U(1, 0) > 0 ? 1e4 : (U(1, 0) < -1 ? 1e4 : 0);
  //R_.setIdentity(); //  foobar
  //R_ *= 1e0; // foobar
  q_ = -Q * goal; // jacobian of cost w.r.t. X less Q_ * X
  r_.setZero();
  r_(1, 0) = -R_(1, 1) * (U(1, 0) > 0 ? 0 : -1);
  P_.setZero();
  return 0;
}

double ELQRPath::StepCost(State X, Input U) {
  // On choosing Q:
  // R has a value of 1 and the max value of kRudder is kMaxRudder (1).
  // Therefore, values of u'*R*u will be less than 1.
  // For X/Y:
  // -In meters; distances generally in tens of meters
  // For V:
  // -In meters/sec; generally going to be single-digit values
  // -Shouldn't really be penalized; except in station keeping,
  //   want to move fast.
  // For theta, we only care to the extent that we'd like to be
  // pointed in the right direction at the end.
  // Choosing the goal:
  //  -Choose appropriate X/Y
  //  -Choose V to be a bit more than average boat speed. If we encourage
  //    the boat to be going >0 speed, we can avoid getting stuck in tacks.
  //  -Choose theta to smoothly connect to your next waypoint.
  MatrixNNd Q;
  //Q << 10, 0, 0, 0, 0,
  Q << 1, 0, 0, 0, 0, // foobar
       0, 1, 0, 0, 0,
       0, 0, 1, 0, 0,
       0, 0, 0, 10, 0,
       0, 0, 0, 0, 1;
  return CostWork(X, U, xl_, Q);
}

double ELQRPath::EndCost(State X) {
  // On choosing Q:
  // Care more than normal about achieving goal
  MatrixNNd Q;
  Q << 1, 0, 0, 0, 0,
       0, 1, 0, 0, 0,
       0, 0, 1, 0, 0,
       0, 0, 0, 100, 0,
       0, 0, 0, 0, 1;
  return CostWork(X, Input::Zero(), xl_, Q);
}

double ELQRPath::StartCost(State X, Input U) {
  // On choosing Q:
  // Care more than normal about achieving start
  MatrixNNd Q;
  //Q << 10, 0, 0, 0, 0,
  Q << 1e4, 0, 0, 0, 0, // foobar
       0, 1e4, 0, 0, 0,
       0, 0, 1e4, 0, 0,
       0, 0, 0, 1e4, 0,
       0, 0, 0, 0, 1e4;
  return CostWork(X, U, xi_, Q);
}

// t is the index in the various arrays, so it is an integer from 0->L-1
// Calculates Sbar[t+1] and sbar[t+1] assuming all parameters from t are provided.
void ELQRPath::CalcSbar(int t) {
  MatrixNNd SQ = Sbar_[t] + Q_;
  MatrixNNd At = Abar_.transpose();
  MatrixMNd Bt = Bbar_.transpose();
  MatrixMNd Cbar = Bt * SQ * Abar_ + P_ * Abar_;
  MatrixNNd Dbar = At * SQ * Abar_;
  MatrixMMd Ebar =
      Bt * SQ * Bbar_ + R_ + P_ * Bbar_ + Bt * P_.transpose();
  MatrixN1d dbar = At * (sbar_[t] + q_ + SQ * cbar_);
  MatrixM1d ebar =
      r_ + P_ * cbar_ + Bt * (sbar_[t] + q_) + Bt * SQ * cbar_;
  MatrixMMd Einv = Ebar.inverse();
  MatrixMNd Lbar = -Einv * Cbar;
  MatrixM1d Ibar = -Einv * ebar;
  Sbar_[t+1] = Dbar + Cbar.transpose() * Lbar;
  sbar_[t+1] = dbar + Cbar.transpose() * Ibar;
  if (!(Ibar.allFinite() && Lbar.allFinite())) {
    std::abort();
  }
  pibar_[t] = [Lbar,Ibar](State x) { return Lbar * x + Ibar; };
}

// t is the index in the various arrays, so it is an integer from 0->L-1
// Calculates S/s[t] assuming all parameters from t are available (and S/s from
// t+1)
void ELQRPath::CalcS(int t) {
  MatrixNNd At = A_.transpose();
  MatrixMNd Bt = B_.transpose();
  MatrixMNd C = P_ + Bt * S_[t+1] * A_;
  MatrixNMd Ct = C.transpose();
  MatrixNNd D = Q_ + At * S_[t+1] * A_;
  MatrixMMd E = R_ + Bt * S_[t+1] * B_;
  MatrixN1d d = q_ + At * (s_[t+1] + S_[t+1] * c_);
  MatrixM1d e = r_ + Bt * (s_[t+1] + S_[t+1] * c_);
  MatrixMMd Einv = E.inverse();
  MatrixMNd L = -Einv * C;
  MatrixM1d I = -Einv * e;
  S_[t] = D + Ct * L;
  s_[t] = d + Ct * I;
  if (!(I.allFinite() && L.allFinite())) {
    std::abort();
  }
  if (I.norm() < 1e-4) {
    //std::abort();
  }
  pi_[t] = [L, I](State x) { return L * x + I; };
  //std::cout << "L: " << L << " I: " << I.transpose() << std::endl;
}

ELQRPath::State ELQRPath::StateFromS(int t) {
  return -(S_[t] + Sbar_[t]).inverse() * (s_[t] + sbar_[t]);
}

// Run one forwards/backwards iteration.
void ELQRPath::BackIter() {
  // Initialization--seed values using final cost linearization.
  EndCost(x_.back());
  S_.back() = Q_;
  s_.back() = q_;
  x_[L] = StateFromS(L);

  // Iterate appropriately.
  for (int t = L-1; t >= 0; --t) {
    Input ut = pibar_[t](x_[t+1]);
    std::cout << "t: " << t << " xt+1: " << x_[t+1] << std::endl;
    x_[t] = G(x_[t+1], ut, false);
    // Linearize cost function here.
    if (t > 0) {
      StepCost(x_[t], ut);
    } else {
      StartCost(x_[t], ut);
    }
    CalcS(t);

    x_[t] = StateFromS(t);
    std::cout << "t: " << t << " ut: " << pi_[t](x_[t]) << std::endl;
  }
}

void ELQRPath::ForwardsIter() {
  // Initialize with appropriate values.
  // Help force start position.
  double totalcost = 0;
  Sbar_[0].setZero();
  sbar_[0].setZero();

  // Actually iterate...
  for (int t = 0; t < L; ++t) {
    x_[t+1] = G(x_[t], pi_[t](x_[t]), true);
    // Linearize cost function here.
    if (t > 0) {
      totalcost += StepCost(x_[t], pi_[t](x_[t]));
    } else {
      totalcost += StartCost(x_[t], pi_[t](x_[t]));
    }
    CalcSbar(t);
    x_[t+1] = StateFromS(t+1);
  }
  std::cout << "Cost: " << totalcost << "\n";
}

double ELQRPath::WindCircle(double theta) {
  theta = sailbot::util::norm_angle(theta);
  float atheta = std::abs(theta);
  float factor = 0;
  if (atheta < kIrons) {
    factor = 0;
  } else if (atheta < kBeam) {
    const float x = atheta - kIrons;
    const float b = kBeam - kIrons;
    factor = kBeamSpeed / (b * b) * (2 * b - x) * x;
  } else if (atheta < kBroad) {
    factor = kBeamSpeed;
  } else if (atheta < M_PI) {
    const float x = atheta - kBroad;
    factor = kBeamSpeed + x * (kDownSpeed - kBeamSpeed) / (M_PI - kBroad);
  }
  factor += 0.05;

  return wind_speed_ * factor;
}

double ELQRPath::DeltaWindCircle(double theta) {
  theta = sailbot::util::norm_angle(theta);
  float atheta = std::abs(theta);
  float factor = 0;
  if (atheta < kIrons) {
    factor = 0;
  } else if (atheta < kBeam) {
    const float x = atheta - kIrons;
    const float b = kBeam - kIrons;
    factor = kBeamSpeed / (b * b) * 2 * (b - x);
  } else if (atheta < kBroad) {
    factor = 0;
  } else if (atheta < M_PI) {
    factor = (kDownSpeed - kBeamSpeed) / (M_PI - kBroad);
  }

  return wind_speed_ * factor * (theta > 0 ? 1 : -1);
}

double ELQRPath::CalcBaseRudder(double theta) {
  return -kKrudder * sailbot::util::norm_angle(nom_heading_ - theta);
}

double ELQRPath::DeltaBaseRudder() {
  return kKrudder;
}

void ELQRPath::FooMain() {
  for (float t = 0; t < M_PI * 2; t += 0.01) {
    std::cout << t << ", " << WindCircle(t) << ", " << DeltaWindCircle(t)
              << std::endl;
  }
}

int main() {
  ELQRPath::State start, end;
  start << 0, 0, 0, M_PI / 2., 0;
  end << 0, 20, 3, M_PI / 2., 0;
  ELQRPath tst(start, end);
  tst.Run();
  //tst.FooMain();
}
