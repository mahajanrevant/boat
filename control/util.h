#pragma once
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <functional>
#include "sim/sim_debug.pb.h"

namespace sailbot {
namespace util {

typedef Eigen::Vector3d Vector3d;
typedef Eigen::Matrix3d Matrix3d;

// Transforms a vector x using rotation matrix R and origin.
Eigen::Vector3d Trans(const Vector3d& x, const Matrix3d& R, const Vector3d& o);

Eigen::Matrix3d Skew(const Vector3d& x);

Eigen::Matrix3d Orthogonalize(const Matrix3d&R);

void EigenToProto(const Eigen::Vector3d &ve, sailbot::msg::Vector3f *msg);
void EigenToProtod(const Eigen::Vector3d &ve, sailbot::msg::Vector3d *msg);

Vector3d GetRollPitchYaw(Matrix3d R);

Matrix3d RollPitchYawToMatrix(const Vector3d &angles);

Vector3d GetRollPitchYawFromQuat(Eigen::Quaterniond q);

Eigen::Quaterniond RollPitchYawToQuat(const Vector3d &angles);

double norm_angle(double a);

template <typename T> int Sign(T val) { return (T(0) < val) - (val < T(0)); }

template <typename M>
void RungeKutta4(std::function<M(double, M)> f, M &y, double t0, double h) {
  M k1 = f(t0, y);
  M k2 = f(t0 + h / 2, y + k1 * h / 2);
  M k3 = f(t0 + h / 2, y + k2 * h / 2);
  M k4 = f(t0 + h, y + k3 * h);
  y += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
}

// Takes lat/lon in degrees, returns distance, in m
double GPSDistance(double lat1, double lon1, double lat2, double lon2);

// Takes lat/lon in degrees, returns bearing
double GPSBearing(double lat1, double lon1, double lat2, double lon2);

// Computes the number of meters per degree of lat/lon at a given degree latitude
void GPSLatLonScale(double lat, double *latscale, double *lonscale);

template <typename T> T ToRad(T a) { return a * M_PI / 180.; }
template <typename T> T ToDeg(T a) { return a * 180. / M_PI; }

float Normal(float mean = 0, float std = 1);

template <typename T>
T Clip(T a, T min, T max) {
  return std::max(std::min(a, max), min);
}

// clip a such that it is not closer than min to zero.
// e.g., if a = 0.01 and min = 0.1, then ReverseClip(a, min) = 0.1
template <typename T>
T ReverseClip(T a, T min) {
  return a > 0 ? std::max(a, min) : std::min(a, -min);
}

double atan2(const Eigen::Vector2d &diff);

}  // namespace util
}  // namespace sailbot
