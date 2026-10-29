#pragma once

#include <cmath>
#include <algorithm>
#include "../third_party/openvr/openvr.h"

namespace vr_stab {

struct Vec3 {
	float x, y, z;

	Vec3() : x(0), y(0), z(0) {}
	Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

	Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }

	float dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }

	Vec3 cross(const Vec3 &o) const {
		return Vec3(
			y * o.z - z * o.y,
			z * o.x - x * o.z,
			x * o.y - y * o.x
		);
	}

	float length() const { return std::sqrt(x * x + y * y + z * z); }

	Vec3 normalized() const {
		float len = length();
		if (len < 1e-6f)
			return Vec3(0, 0, 0);
		return Vec3(x / len, y / len, z / len);
	}
};

struct Mat3 {
	// Row-major 3x3 matrix: m[row][col]
	float m[3][3];

	static Mat3 identity() {
		Mat3 res{};
		res.m[0][0] = 1.0f;
		res.m[1][1] = 1.0f;
		res.m[2][2] = 1.0f;
		return res;
	}

	Mat3 transposed() const {
		Mat3 res{};
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				res.m[r][c] = m[c][r];
		return res;
	}

	Mat3 operator*(const Mat3 &o) const {
		Mat3 res{};
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				res.m[r][c] = m[r][0] * o.m[0][c] +
					      m[r][1] * o.m[1][c] +
					      m[r][2] * o.m[2][c];
			}
		}
		return res;
	}

	Vec3 col(int c) const { return Vec3(m[0][c], m[1][c], m[2][c]); }

	void set_cols(const Vec3 &c0, const Vec3 &c1, const Vec3 &c2) {
		m[0][0] = c0.x; m[0][1] = c1.x; m[0][2] = c2.x;
		m[1][0] = c0.y; m[1][1] = c1.y; m[1][2] = c2.y;
		m[2][0] = c0.z; m[2][1] = c1.z; m[2][2] = c2.z;
	}
};

struct Quat {
	float w, x, y, z;

	Quat() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}
	Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

	float dot(const Quat &o) const { return w * o.w + x * o.x + y * o.y + z * o.z; }

	Quat normalized() const {
		float n = std::sqrt(w * w + x * x + y * y + z * z);
		if (n < 1e-7f)
			return Quat(1.0f, 0.0f, 0.0f, 0.0f);
		return Quat(w / n, x / n, y / n, z / n);
	}

	static Quat from_mat3(const Mat3 &r) {
		float trace = r.m[0][0] + r.m[1][1] + r.m[2][2];
		Quat q;
		if (trace > 0.0f) {
			float s = std::sqrt(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (r.m[2][1] - r.m[1][2]) / s;
			q.y = (r.m[0][2] - r.m[2][0]) / s;
			q.z = (r.m[1][0] - r.m[0][1]) / s;
		} else if ((r.m[0][0] > r.m[1][1]) && (r.m[0][0] > r.m[2][2])) {
			float s = std::sqrt(1.0f + r.m[0][0] - r.m[1][1] - r.m[2][2]) * 2.0f;
			q.w = (r.m[2][1] - r.m[1][2]) / s;
			q.x = 0.25f * s;
			q.y = (r.m[0][1] + r.m[1][0]) / s;
			q.z = (r.m[0][2] + r.m[2][0]) / s;
		} else if (r.m[1][1] > r.m[2][2]) {
			float s = std::sqrt(1.0f + r.m[1][1] - r.m[0][0] - r.m[2][2]) * 2.0f;
			q.w = (r.m[0][2] - r.m[2][0]) / s;
			q.x = (r.m[0][1] + r.m[1][0]) / s;
			q.y = 0.25f * s;
			q.z = (r.m[1][2] + r.m[2][1]) / s;
		} else {
			float s = std::sqrt(1.0f + r.m[2][2] - r.m[0][0] - r.m[1][1]) * 2.0f;
			q.w = (r.m[1][0] - r.m[0][1]) / s;
			q.x = (r.m[0][2] + r.m[2][0]) / s;
			q.y = (r.m[1][2] + r.m[2][1]) / s;
			q.z = 0.25f * s;
		}
		return q.normalized();
	}

	Mat3 to_mat3() const {
		Mat3 r{};
		float xx = x * x, yy = y * y, zz = z * z;
		float xy = x * y, xz = x * z, yz = y * z;
		float wx = w * x, wy = w * y, wz = w * z;

		r.m[0][0] = 1.0f - 2.0f * (yy + zz);
		r.m[0][1] = 2.0f * (xy - wz);
		r.m[0][2] = 2.0f * (xz + wy);

		r.m[1][0] = 2.0f * (xy + wz);
		r.m[1][1] = 1.0f - 2.0f * (xx + zz);
		r.m[1][2] = 2.0f * (yz - wx);

		r.m[2][0] = 2.0f * (xz - wy);
		r.m[2][1] = 2.0f * (yz + wx);
		r.m[2][2] = 1.0f - 2.0f * (xx + yy);
		return r;
	}

	static Quat slerp(const Quat &a, const Quat &b_in, float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		Quat b = b_in;
		float cos_HalfTheta = a.dot(b);
		if (cos_HalfTheta < 0.0f) {
			b.w = -b.w;
			b.x = -b.x;
			b.y = -b.y;
			b.z = -b.z;
			cos_HalfTheta = -cos_HalfTheta;
		}
		if (cos_HalfTheta > 0.9995f) {
			return Quat(
				a.w + t * (b.w - a.w),
				a.x + t * (b.x - a.x),
				a.y + t * (b.y - a.y),
				a.z + t * (b.z - a.z)
			).normalized();
		}
		cos_HalfTheta = std::min(cos_HalfTheta, 1.0f);
		float halfTheta = std::acos(cos_HalfTheta);
		float sin_HalfTheta = std::sqrt(1.0f - cos_HalfTheta * cos_HalfTheta);
		if (std::fabs(sin_HalfTheta) < 1e-5f) {
			return Quat(
				0.5f * (a.w + b.w),
				0.5f * (a.x + b.x),
				0.5f * (a.y + b.y),
				0.5f * (a.z + b.z)
			).normalized();
		}
		float ratioA = std::sin((1.0f - t) * halfTheta) / sin_HalfTheta;
		float ratioB = std::sin(t * halfTheta) / sin_HalfTheta;
		return Quat(
			a.w * ratioA + b.w * ratioB,
			a.x * ratioA + b.x * ratioB,
			a.y * ratioA + b.y * ratioB,
			a.z * ratioA + b.z * ratioB
		).normalized();
	}

	static float angle_between(const Quat &a, const Quat &b) {
		float d = std::fabs(a.dot(b));
		d = std::clamp(d, 0.0f, 1.0f);
		return 2.0f * std::acos(d);
	}
};

class PoseStabilizer {
public:
	bool initialized = false;
	Quat q_smooth;
	Quat q_horizon_smooth;

	void reset() {
		initialized = false;
		q_smooth = Quat();
		q_horizon_smooth = Quat();
	}

	// Returns R_reproj = R_raw^T * R_virtual
	// Maps a ray in stabilized virtual camera space into instantaneous HMD space.
	Mat3 update(const vr::HmdMatrix34_t &pose_mat, float dt,
		    float smoothing_pct, float roll_lock_pct, float max_angle_deg) {
		// Extract and orthonormalize 3x3 rotation from OpenVR 3x4 pose
		Vec3 c0(pose_mat.m[0][0], pose_mat.m[1][0], pose_mat.m[2][0]);
		Vec3 c1(pose_mat.m[0][1], pose_mat.m[1][1], pose_mat.m[2][1]);
		Vec3 c2(pose_mat.m[0][2], pose_mat.m[1][2], pose_mat.m[2][2]);

		c0 = c0.normalized();
		c2 = c0.cross(c1).normalized();
		c1 = c2.cross(c0).normalized();

		Mat3 r_raw;
		r_raw.set_cols(c0, c1, c2);
		Quat q_raw = Quat::from_mat3(r_raw);

		dt = std::clamp(dt, 0.0005f, 0.1f);
		float s = std::clamp(smoothing_pct * 0.01f, 0.0f, 1.0f);
		float roll_s = std::clamp(roll_lock_pct * 0.01f, 0.0f, 1.0f);
		float max_rad = std::clamp(max_angle_deg, 0.5f, 35.0f) * (3.1415926535f / 180.0f);

		if (!initialized) {
			q_smooth = q_raw;
			q_horizon_smooth = q_raw;
			initialized = true;
			return Mat3::identity();
		}

		// 1. Pitch/Yaw/Roll velocity-adaptive SLERP filter
		if (s <= 0.001f) {
			q_smooth = q_raw;
		} else {
			float err_rad = Quat::angle_between(q_smooth, q_raw);
			// Base time constant: 0ms at 0%, ~130ms at 65%, ~320ms at 100%
			float tau = 0.002f + 0.32f * (s * s);
			float norm_err = err_rad / std::max(max_rad, 0.01f);
			// Smoothly accelerate tracking when approaching max_angle_deg
			float adaptive_rate = (1.0f / tau) * (1.0f + 10.0f * norm_err * norm_err * norm_err);
			float alpha = 1.0f - std::exp(-dt * adaptive_rate);
			q_smooth = Quat::slerp(q_smooth, q_raw, alpha);
		}

		// 2. Apply extra Roll / Horizon stabilization around forward axis
		Mat3 r_virt = q_smooth.to_mat3();
		if (roll_s > 0.001f) {
			// Slow filter for horizon reference so intentional 45-deg head peeks still work smoothly
			float roll_tau = 0.05f + 0.80f * (roll_s * roll_s);
			float roll_alpha = 1.0f - std::exp(-dt / roll_tau);
			q_horizon_smooth = Quat::slerp(q_horizon_smooth, q_raw, roll_alpha);

			Vec3 z_back = r_virt.col(2).normalized(); // +Z is backward, -Z is forward
			Vec3 f_fwd = z_back * -1.0f;
			Vec3 world_up(0.0f, 1.0f, 0.0f);

			// Only apply world horizon lock when not looking straight up/down
			float up_dot = std::fabs(f_fwd.dot(world_up));
			if (up_dot < 0.95f) {
				Vec3 right_level = f_fwd.cross(world_up).normalized();
				Vec3 up_level = z_back.cross(right_level).normalized();

				// Also blend with heavily smoothed roll reference
				Mat3 r_hor_ref = q_horizon_smooth.to_mat3();
				Vec3 up_ref = r_hor_ref.col(1);
				// Project up_ref onto plane perpendicular to z_back
				Vec3 up_proj = (up_ref - z_back * up_ref.dot(z_back)).normalized();

				// Combine world horizon and slow-filtered roll
				float horizon_weight = roll_s * (1.0f - std::clamp((up_dot - 0.7f) / 0.25f, 0.0f, 1.0f));
				Vec3 target_up = (up_proj * (1.0f - 0.6f * roll_s) + up_level * (0.6f * roll_s)).normalized();

				Vec3 cur_up = r_virt.col(1);
				Vec3 blended_up = (cur_up * (1.0f - horizon_weight) + target_up * horizon_weight).normalized();
				// Re-orthogonalize
				Vec3 final_right = blended_up.cross(z_back).normalized();
				Vec3 final_up = z_back.cross(final_right).normalized();
				r_virt.set_cols(final_right, final_up, z_back);
				q_smooth = Quat::from_mat3(r_virt);
			}
		}

		// 3. Enforce max angular excursion so reprojection stays within overscan bounds
		float final_err = Quat::angle_between(q_smooth, q_raw);
		if (final_err > max_rad && final_err > 1e-5f) {
			float pull = (final_err - max_rad) / final_err;
			q_smooth = Quat::slerp(q_smooth, q_raw, pull);
			r_virt = q_smooth.to_mat3();
		}

		// R_reproj = R_raw^T * R_virt
		return r_raw.transposed() * r_virt;
	}
};

} // namespace vr_stab
