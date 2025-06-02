/*
bkm_math.h - Math utilities for the Brickate project

This header provides basic vector (vec3) and matrix (mat4) operations
using a column-major layout compatible with OpenGL.

Includes functions for:
  - Angle conversions (degrees <=> radians)
  - vec3 operations: add, subtract, scale, dot, cross, normalize, etc.
  - mat4 operations: identity, translate, scale, rotate (X/Y/Z), multiply
  - Perspective and look-at matrix generation
  - Model matrix creation from position, rotation, and scale
  - Vector transformation by matrix (position/direction)

Designed for minimal dependencies and efficient use in 3D applications.
*/

#ifndef BK_MATH_H
#define BK_MATH_H

#include <math.h>

float bkm_deg(float x) {
	return x * (180.0f / M_PI);
}

float bkm_rad(float x) {
	return x * (M_PI / 180.0f);
}

float bkm_clamp(float x, float min, float max) {
	return x < min ? min : (x > max ? max : x);
}

typedef float vec3[3];

static const vec3 bkm_VEC3_ZERO = {0.0f, 0.0f, 0.0f};

void bkm_add(vec3 a, vec3 b, vec3 dest) {
	dest[0] = a[0] + b[0];
	dest[1] = a[1] + b[1];
	dest[2] = a[2] + b[2];
}

void bkm_vec3_sub(vec3 a, vec3 b, vec3 dest) {
	dest[0] = a[0] - b[0];
	dest[1] = a[1] - b[1];
	dest[2] = a[2] - b[2];
}

void bkm_vec3_scale(vec3 a, float s, vec3 dest) {
	dest[0] = a[0] * s;
	dest[1] = a[1] * s;
	dest[2] = a[2] * s;
}

float bkm_vec3_dot(vec3 a, vec3 b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void bkm_vec3_cross(vec3 a, vec3 b, vec3 dest) {
	dest[0] = a[1] * b[2] - a[2] * b[1];
	dest[1] = a[2] * b[0] - a[0] * b[2];
	dest[2] = a[0] * b[1] - a[1] * b[0];
}

float bkm_vec3_len(vec3 v) {
	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void bkm_vec3_copy(vec3 v, vec3 dest) {
	dest[0] = v[0];
	dest[1] = v[1];
	dest[2] = v[2];
}

void bkm_vec3_set(vec3 dest, float x, float y, float z) {
	dest[0] = x;
	dest[1] = y;
	dest[2] = z;
}

void bkm_vec3_normalize(vec3 v, vec3 dest) {
	float len = bkm_vec3_len(v);
	if (len > 0.0f)
		bkm_vec3_scale(v, 1.0f / len, dest);
	else bkm_vec3_copy(bkm_VEC3_ZERO, dest);
}

typedef float mat4[16];

void bkm_mat4_identity(mat4 dest) {
	for (int i = 0; i < 16; i++) dest[i] = 0.0f;
	dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

void bkm_mat4_translate(vec3 v, mat4 dest) {
	bkm_mat4_identity(m);
	dest[12] = v[0];
	dest[13] = v[1];
	dest[14] = v[2];
}

void bkm_mat4_scale(vec3 v, mat4 dest) {
	bkm_mat4_identity(m);
	dest[0] = v[0];
	dest[5] = v[1];
	dest[10] = v[2];
}

void bkm_mat4_rotate_x(float angle_rad, mat4 dest) {
	bkm_mat4_identity(m);
	float s = sinf(angle_rad);
	float c = cosf(angle_rad);
	dest[5] = c;
	dest[6] = -s;
	dest[9] = s;
	dest[10] = c;
}

void bkm_mat4_rotate_y(float angle_rad, mat4 dest) {
	bkm_mat4_identity(m);
	float s = sinf(angle_rad);
	float c = cosf(angle_rad);
	dest[0] = c;
	dest[2] = s;
	dest[8] = -s;
	dest[10] = c;
}

void bkm_mat4_rotate_z(float angle_rad, mat4 dest) {
	bkm_mat4_identity(m);
	float s = sinf(angle_rad);
	float c = cosf(angle_rad);
	dest[0] = c;
	dest[1] = -s;
	dest[4] = s;
	dest[5] = c;
}

void bkm_mat4_mul(mat4 a, mat4 b, mat4 dest) {
	mat4 res;
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			float sum = 0.0f;
			for (int i = 0; i < 4; i++) {
				sum += a[i * 4 + row] * b[col * 4 + i];
			}
			res[col * 4 + row] = sum;
		}
	}
	for (int i = 0; i < 16; i++) dest[i] = res[i];
}

void mat4_perspective(float fovy, float aspect, float near, float far, mat4 dest) {
	float f = 1.0f / tanf(fovy / 2.0f);
	float nf = 1.0f / (near - far);

	dest[0] = f / aspect;
	dest[1] = 0.0f;
	dest[2] = 0.0f;
	dest[3] = 0.0f;

	dest[4] = 0.0f;
	dest[5] = f;
	dest[6] = 0.0f;
	dest[7] = 0.0f;

	dest[8] = 0.0f;
	dest[9] = 0.0f;
	dest[10] = (far + near) * nf;
	dest[11] = -1.0f;

	dest[12] = 0.0f;
	dest[13] = 0.0f;
	dest[14] = (2.0f * far * near) * nf;
	dest[15] = 0.0f;
}

void bkm_mat4_lookat(vec3 eye, vec3 center, vec3 up, mat4 dest) {
	vec3 f, s, u;
	
	bkm_vec3_sub(center, eye, f);
	bkm_vec3_normalize(f, f);
	
	bkm_vec3_cross(f, up, s);
	bkm_vec3_normalize(s, s);
	
	bkm_vec3_cross(s, f, u);
	bkm_vec3_normalize(u, u);
	
	dest[0] = s[0];
	dest[1] = u[0];
	dest[2] = -f[0];
	dest[3] = 0.0f;
	
	dest[4] = s[1];
	dest[5] = u[1];
	dest[6] = -f[1];
	dest[7] = 0.0f;
	
	dest[8] = s[2];
	dest[9] = u[2];
	dest[10] = -f[2];
	dest[11] = 0.0f;
	
	dest[12] = -bkm_vec3_dot(s, eye);
	dest[13] = -bkm_vec3_dot(u, eye);
	dest[14] = bkm_vec3_dot(f, eye);
	dest[15] = 1.0f;
}

void bkm_mat4_model(vec3 pos, vec3 rot, vec3 scale, mat4 dest) {
	mat4 t, rx, ry, rz, s, rxy, rxyz, trs;
	
	bkm_mat4_translate(pos, t);
	
	bkm_mat4_rotate_x(rot[0], rx);
	bkm_mat4_rotate_y(rot[1], ry);
	bkm_mat4_rotate_z(rot[2], rz);
	
	bkm_mat4_scale(scale, s);
	
	bkm_mat4_mul(ry, rx, rxy);
	bkm_mat4_mul(rz, rxy, rxyz);
	
	bkm_mat4_mul(rxyz, s, trs);
	
	bkm_mat4_mul(t, trs, dest);
}

void bkm_mat4_mulv(mat4 m, vec3 v, float w, mat4 dest) {
	dest[0] = m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12] * w;
	dest[1] = m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13] * w;
	dest[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * w;
}

#endif