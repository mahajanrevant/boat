#include "sim_physics.h"

#include "util/clock.h"
#include "glog/logging.h"

// Note on vertical distances:
// because the origin is at the CoM, and the keel drags the CoM way down,
// heights will seem too positive, as a more intuitive origin would be at the
// deck.
SimulatorSaoud2013::SimulatorSaoud2013(float _dt)
    : dt(_dt /*s*/),
      ls(1.5 /*m*/),
      lr(.05 /*m*/),
      hs(2. /*m*/),
      hr(0 /*m*/),
      hk(-.3 /*m*/),
      deltas(.8 /*rad*/),
      deltasdot(0 /*rad/s*/),
      deltar(0 /*rad*/),
      deltardot(0 /*rad/s*/),
      rr(1 /*m*/),
      //rs(.5 /*m*/),
      rs(1 /*m*/),
      rhoair(1.225 /*kg/m^3*/),
      rhowater(1000 /*kg/m^3*/),
      Ss(5 /*m^2*/),
      Sr(.05 /*m^2*/),
      Sk(.3 /*m^2*/),
      c0s(.2),
      c1s(.4),
      c0r(c0s),
      c1r(c1s),
      c0k(c0s),
      c1k(c1s),
      m0(40 /*kg*/),
      nabla(m0 / rhowater) {
  // TODO(james):
  // Not really sure what these should initialize to. They refer to the
  // resistance along the hull through the water. Definitely shouldn't be zero.
  c1 = 25 * c1.Ones();
  c3 = c3.Ones();
  c1 = c1.Zero();
  c3 = c3.Zero();
  c2 = c2.Zero();
  c4 = c4.Zero();

//  J0 << ; // TODO(james) ?
  J0 = J0.Zero();
  J0.diagonal() = Vector3d(50, 50, 30);

  MA = MA.Zero(); // TODO(james): Might be able to get away with 0...
  // TODO(james): Calculations for metacenter/center of buoyancy.
  // Note: Center of buoyancy is above origin, b/c origin is at cetner of mass,
  // which is dragged down by keel.
  B_B = Vector3d(0, 0, .5);

  x = x.Zero();
  v = v.Zero();
  vw = vw.Zero();
  vc = vc.Zero();
  RBI = RBI.Identity();
  omega = omega.Zero();
}

TrivialDynamics::TrivialDynamics(float _dt)
    : dt(_dt), rr(-1), rs(.25), rk(0), ls(.5), lr(.1), hs(2), hr(-.5), hk(-.7),
      CoM(0, 0, -1), mass(25), deltas(1.3), deltar(0),
      debug_queue_("sim_debug", true) {
  J << 25, 0, 0,
       0, 25, 0,
       0, 0, 5;
  x = x.Zero();
  v = v.Zero();
  wind = wind.Zero();
  omega = omega.Zero();
  RBI = RBI.Identity();
}

std::pair<Eigen::Matrix<double, 6, 1>, Eigen::Matrix3d> TrivialDynamics::Update(
    double sdot, double rdot) {
  sailbot::msg::SimDebugMsg msg;
  VLOG(1) << "sdot, rdot: " << sdot << ", " << rdot;
  deltas += sdot * dt;
  deltar += rdot * dt;
  deltas = norm_angle(deltas);
  deltar = norm_angle(deltar);
  Vector3d Fs = SailForces();
  Vector3d Fr = RudderForces();
  Vector3d Fk = KeelForces();
  Vector3d Fh = HullForces();
  Vector3d Fnet = Fs + Fr + Fk + Fh;
  Vector3d taus = SailTorques(Fs);
  Vector3d taur = RudderTorques(Fr);
  Vector3d tauk = KeelTorques(Fk);
  Vector3d tauh = HullTorques();
  Vector3d tauright = RightingTorques();
  Vector3d tau = taus + taur + tauk + tauh + tauright;
  tau(1, 0) = 0;
  Fnet = RBI * Fnet;
  // Ignore any coriolis/centripetal junk.
  omega += J.inverse() * tau * dt;
  omega(1, 0) = 0;
  v += Fnet / mass * dt;
  v(2, 0) = 0;

  EigenToProto(Fs, msg.mutable_fs());
  EigenToProto(Fr, msg.mutable_fr());
  EigenToProto(Fk, msg.mutable_fk());
  EigenToProto(Fh, msg.mutable_fh());
  EigenToProto(Fnet, msg.mutable_fnet());

  EigenToProto(taus, msg.mutable_taus());
  EigenToProto(taur, msg.mutable_taur());
  EigenToProto(tauk, msg.mutable_tauk());
  EigenToProto(tauh, msg.mutable_tauh());
  EigenToProto(tauright, msg.mutable_tauright());
  EigenToProto(tau, msg.mutable_taunet());
  debug_queue_.send(&msg);
  VLOG(1) << "Fs " << Fs.transpose() << " Fr " << Fr.transpose() << " Fk "
          << Fk.transpose() << " Fh " << Fh.transpose();
  VLOG(1) << "taus " << SailTorques(Fs).transpose() << " taur "
          << RudderTorques(Fr).transpose() << " tauk "
          << KeelTorques(Fk).transpose() << " tauh "
          << HullTorques().transpose() << " tauright "
          << RightingTorques().transpose();
  VLOG(1) << "tau: " << tau.transpose();
  VLOG(1) << "omega: " << omega.transpose();

  Eigen::Quaterniond rotquat(RBI);
  Eigen::Quaterniond omegaquat;
  omegaquat.w() = 0;
  omegaquat.vec() = RBI * omega * dt;
  VLOG(2) << "omegaquat: " << omegaquat.w() << " " << omegaquat.x() << " "
          << omegaquat.y() << " " << omegaquat.z()
          << " rotquat: " << rotquat.w() << " " << rotquat.x() << " "
          << rotquat.y() << " " << rotquat.z();
  rotquat.coeffs() += (omegaquat * rotquat).coeffs();// * .5 * dt;
  VLOG(2) << "rotquat: " << rotquat.w() << " " << rotquat.x() << " "
          << rotquat.y() << " " << rotquat.z();
  rotquat.normalize();
  RBI = rotquat.toRotationMatrix();
  //VLOG(2) << "RBI-undone:\n" << RBI;
  //RBI = Orthogonalize(RBI);
  VLOG(2) << "RBI:\n" << RBI;
  if (std::isnan(RBI(0, 0))) {
    LOG(FATAL) << "Found NaN";
  }
  x += v * dt;

  Vector6d nu;
  nu << x, v;
  return {nu, RBI};
}

Vector3d TrivialDynamics::AeroForces(Vector3d v, const float delta,
                                     const float rho, const float A,
                                     const float mindrag, const float maxdrag,
                                     const float maxlift) {
  // If alpha is negative, then lift follows [0 1; -1 0] * v
  // If alpha is positive, then lift follows [0 -1; 1 0] * v
  float alphasigned = norm_angle(std::atan2(-v(1), -v(0)) - delta);
  if (std::isnan(alphasigned))
    alphasigned = 0;
  const float alpha = std::abs(alphasigned);
  const int sign_alpha = alpha == 0 ? 1 : alphasigned / alpha;
  Matrix3d liftrot;
  liftrot << 0, -1, 0,
             1, 0, 0,
             0, 0, 1;
  liftrot.block(0, 0, 2, 2) *= sign_alpha;
  const float kPeak = .4;
  const float kSlope = maxlift / kPeak;

  const float Cl =
      alpha > kPeak ? kSlope * kPeak * (1 - (alpha - kPeak) / (PI / 2 - kPeak))
                    : kSlope * alpha;
  const float Cd = std::pow(maxdrag / mindrag, alpha * 2 / PI) * mindrag;
  VLOG(3) << alphasigned << " Cl: " << Cl << " Cd: " << Cd;
  const float true_area =
      A * RBI(2, 2);  // Calculate the area that is vertical.
  const float s =
      .5 * rho * true_area *
      v.squaredNorm();  // Calculates the common components to lift/dag.
  if (v.squaredNorm() > .005) v.normalize();
  const Vector3d lift = Cl * s * liftrot * v;
  const Vector3d drag = Cd * s * v;
  return lift + drag;
}

// Really, all the relative winds should account for angular movement...
Vector3d TrivialDynamics::SailForces() {
  VLOG(3) << "Sail Alpha: ";
  Vector3d wa = RBI.transpose() * (wind - v);
  wa(2, 0) = 0;
  return AeroForces(wa, deltas, 1.225 /*rhoair,kg/m^3*/, 6 /*Sail area,m^2*/);
}
Vector3d TrivialDynamics::RudderForces() {
  VLOG(3) << "Rudder Alpha: ";
  // For now, assuming 0 water current.
  Vector3d va = RBI.transpose() * -v;
  va(2, 0) = 0;
  return AeroForces(va, deltar, 1000 /*rhowater,kg/m^3*/, .1 /*Rudder area,m^2*/);
}
Vector3d TrivialDynamics::KeelForces() {
  VLOG(3) << "Keel Alpha: ";
  // For now, assuming 0 water current.
  Vector3d va = RBI.transpose() * -v;
  va(2, 0) = 0;
  return AeroForces(va, 0, 1000 /*rhowater,kg/m^3*/, .3 /*Keel area,m^2*/);
}
Vector3d TrivialDynamics::HullForces() {
  VLOG(3) << "Hull Alpha: ";
  // For now, assuming 0 water current.
  Vector3d va = RBI.transpose() * -v;
  va(2, 0) = 0;
  // For hull, assume no lift, quadratic drag.
  return AeroForces(va, 0, 1000 /*rhowater,kg/m^3*/, .1 /*Hull area,m^2*/, .01, 1,
                    0);
}
Vector3d TrivialDynamics::SailTorques(const Vector3d& f) {
  Vector3d r(rs - ls * std::cos(deltas), -ls * std::sin(deltas), hs);
  VLOG(1) << "sailr: " << r.transpose();
  return r.cross(f);
}
Vector3d TrivialDynamics::RudderTorques(const Vector3d& f) {
  Vector3d r(rr - lr * std::cos(deltar), -lr * std::sin(deltar), hr);
  return r.cross(f);
}
Vector3d TrivialDynamics::KeelTorques(const Vector3d& f) {
  Vector3d r(rk, 0, hk);
  return r.cross(f);
}
Vector3d TrivialDynamics::HullTorques() {
  // Unlike other forces, don't assume aerodynamic characteristics.
  // Just linear damping on each axis.
  Matrix3d c;
  c << 50, 0, 0,
       0, 0, 0,
       0, 0, 150;
  return -c * omega;
}
Vector3d TrivialDynamics::RightingTorques() {
  const float kMaxTorque = mass * 10 * CoM(2, 0); // ~15kg * ~9.8m/s^2 * ~1m lever arm
  return Vector3d(RBI(2, 1) * kMaxTorque, 0, 0);
}