/**************************************************************************/
/*  dense_storage.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                            COMPETENT VOXELS                            */
/*           https://github.com/Astral-Chains/competent-voxels            */
/**************************************************************************/
/* Copyright (c) 2026-present Competent Voxels contributors.              */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "dense_storage.h"

#include "core/error/error_macros.h"
#include "core/variant/variant.h"

Error VoxelDenseStorage::configure(const AABB &p_bounds, float p_voxel_size) {
	ERR_FAIL_COND_V_MSG(p_voxel_size <= 0.0f, ERR_INVALID_PARAMETER, "Voxel size must be greater than zero.");
	ERR_FAIL_COND_V_MSG(p_bounds.size.x <= 0.0f || p_bounds.size.y <= 0.0f || p_bounds.size.z <= 0.0f,
			ERR_INVALID_PARAMETER, "Field bounds must have a positive size on every axis.");

	Vector3i cells;
	cells.x = MAX(1, (int)Math::ceil(p_bounds.size.x / p_voxel_size));
	cells.y = MAX(1, (int)Math::ceil(p_bounds.size.y / p_voxel_size));
	cells.z = MAX(1, (int)Math::ceil(p_bounds.size.z / p_voxel_size));

	const Vector3i samples = cells + Vector3i(1, 1, 1);
	const uint64_t sample_count = (uint64_t)samples.x * (uint64_t)samples.y * (uint64_t)samples.z;
	ERR_FAIL_COND_V_MSG(sample_count > MAX_SAMPLES, ERR_OUT_OF_MEMORY,
			vformat("A dense voxel field of %d x %d x %d samples exceeds the dense storage limit of %d. Use a larger voxel size.",
					samples.x, samples.y, samples.z, MAX_SAMPLES));

	bounds = p_bounds;
	voxel_size = p_voxel_size;
	cell_resolution = cells;
	sample_resolution = samples;
	chunk_resolution = Vector3i(
			(cells.x + CHUNK_SIZE - 1) / CHUNK_SIZE,
			(cells.y + CHUNK_SIZE - 1) / CHUNK_SIZE,
			(cells.z + CHUNK_SIZE - 1) / CHUNK_SIZE);

	sdf.resize(sample_count);
	fill_sdf(get_far_value());

	for (uint32_t i = 0; i < channels.size(); i++) {
		channels[i].resize(sample_count);
	}

	chunk_dirty.resize((uint32_t)(chunk_resolution.x * chunk_resolution.y * chunk_resolution.z));
	if (!chunk_dirty.is_empty()) {
		memset(chunk_dirty.ptr(), 0, chunk_dirty.size());
	}
	dirty_chunks.clear();
	mark_all_dirty();

	return OK;
}

void VoxelDenseStorage::fill_sdf(float p_value) {
	float *w = sdf.ptr();
	const uint64_t count = sdf.size();
	for (uint64_t i = 0; i < count; i++) {
		w[i] = p_value;
	}
}

float VoxelDenseStorage::sample_local(const Vector3 &p_local) const {
	if (!is_configured()) {
		return 0.0f;
	}
	const Vector3 grid = (p_local - bounds.position) / voxel_size;
	const int x0 = CLAMP((int)Math::floor(grid.x), 0, sample_resolution.x - 1);
	const int y0 = CLAMP((int)Math::floor(grid.y), 0, sample_resolution.y - 1);
	const int z0 = CLAMP((int)Math::floor(grid.z), 0, sample_resolution.z - 1);
	const int x1 = MIN(x0 + 1, sample_resolution.x - 1);
	const int y1 = MIN(y0 + 1, sample_resolution.y - 1);
	const int z1 = MIN(z0 + 1, sample_resolution.z - 1);
	const float tx = CLAMP(grid.x - (float)x0, 0.0f, 1.0f);
	const float ty = CLAMP(grid.y - (float)y0, 0.0f, 1.0f);
	const float tz = CLAMP(grid.z - (float)z0, 0.0f, 1.0f);

	const float *r = sdf.ptr();
	const float c000 = r[sample_index(x0, y0, z0)];
	const float c100 = r[sample_index(x1, y0, z0)];
	const float c010 = r[sample_index(x0, y1, z0)];
	const float c110 = r[sample_index(x1, y1, z0)];
	const float c001 = r[sample_index(x0, y0, z1)];
	const float c101 = r[sample_index(x1, y0, z1)];
	const float c011 = r[sample_index(x0, y1, z1)];
	const float c111 = r[sample_index(x1, y1, z1)];

	const float c00 = Math::lerp(c000, c100, tx);
	const float c10 = Math::lerp(c010, c110, tx);
	const float c01 = Math::lerp(c001, c101, tx);
	const float c11 = Math::lerp(c011, c111, tx);
	return Math::lerp(Math::lerp(c00, c10, ty), Math::lerp(c01, c11, ty), tz);
}

Vector3 VoxelDenseStorage::gradient_local(const Vector3 &p_local) const {
	const float h = voxel_size;
	const Vector3 g(
			sample_local(p_local + Vector3(h, 0, 0)) - sample_local(p_local - Vector3(h, 0, 0)),
			sample_local(p_local + Vector3(0, h, 0)) - sample_local(p_local - Vector3(0, h, 0)),
			sample_local(p_local + Vector3(0, 0, h)) - sample_local(p_local - Vector3(0, 0, h)));
	return g.normalized();
}

bool VoxelDenseStorage::raycast_local(const Vector3 &p_from, const Vector3 &p_dir, float p_max_distance, Vector3 &r_hit, Vector3 &r_normal) const {
	if (!is_configured()) {
		return false;
	}
	const Vector3 dir = p_dir.normalized();
	ERR_FAIL_COND_V(dir.is_zero_approx(), false);

	// Half a voxel keeps the marcher from stepping over a thin feature, which is
	// the only way a sign-change search can miss a surface it passed through.
	const float step = voxel_size * 0.5f;
	float travelled = 0.0f;
	if (sample_local(p_from) <= 0.0f) {
		r_hit = p_from;
		r_normal = gradient_local(p_from);
		return true;
	}

	while (travelled < p_max_distance) {
		const float next = MIN(travelled + step, p_max_distance);
		const Vector3 point = p_from + dir * next;
		const float distance = sample_local(point);

		if (distance <= 0.0f) {
			// Bisect the bracketed interval; ten halvings of a half-voxel step is
			// far below any resolution the field can represent.
			float lo = travelled;
			float hi = next;
			for (int i = 0; i < 10; i++) {
				const float mid = (lo + hi) * 0.5f;
				if (sample_local(p_from + dir * mid) <= 0.0f) {
					hi = mid;
				} else {
					lo = mid;
				}
			}
			r_hit = p_from + dir * hi;
			r_normal = gradient_local(r_hit);
			return true;
		}

		travelled = next;
		if (next >= p_max_distance) {
			break;
		}
	}
	return false;
}

int VoxelDenseStorage::add_channel(const StringName &p_name, VoxelChannelFormat p_format) {
	const int existing = find_channel(p_name);
	if (existing >= 0) {
		return existing;
	}
	VoxelChannel channel;
	channel.name = p_name;
	channel.format = p_format;
	channel.resize(get_sample_count());
	channels.push_back(channel);
	return (int)channels.size() - 1;
}

int VoxelDenseStorage::find_channel(const StringName &p_name) const {
	for (uint32_t i = 0; i < channels.size(); i++) {
		if (channels[i].name == p_name) {
			return (int)i;
		}
	}
	return -1;
}

void VoxelDenseStorage::_mark_chunk_dirty(uint32_t p_chunk_index) {
	if (chunk_dirty[p_chunk_index]) {
		return;
	}
	chunk_dirty[p_chunk_index] = 1;
	dirty_chunks.push_back(p_chunk_index);
}

void VoxelDenseStorage::mark_region_dirty(const AABB &p_local_aabb) {
	if (!is_configured()) {
		return;
	}
	const Vector3 lo = (p_local_aabb.position - bounds.position) / voxel_size;
	const Vector3 hi = (p_local_aabb.position + p_local_aabb.size - bounds.position) / voxel_size;

	// A cell is affected when any of its eight corner samples moved, so the cell
	// range extends one below the sample range.
	const int cx0 = CLAMP((int)Math::floor(lo.x) - 1, 0, chunk_resolution.x * CHUNK_SIZE);
	const int cy0 = CLAMP((int)Math::floor(lo.y) - 1, 0, chunk_resolution.y * CHUNK_SIZE);
	const int cz0 = CLAMP((int)Math::floor(lo.z) - 1, 0, chunk_resolution.z * CHUNK_SIZE);
	const int cx1 = CLAMP((int)Math::ceil(hi.x) + 1, 0, cell_resolution.x);
	const int cy1 = CLAMP((int)Math::ceil(hi.y) + 1, 0, cell_resolution.y);
	const int cz1 = CLAMP((int)Math::ceil(hi.z) + 1, 0, cell_resolution.z);

	if (cx0 >= cx1 || cy0 >= cy1 || cz0 >= cz1) {
		return;
	}

	const int kx0 = cx0 / CHUNK_SIZE;
	const int ky0 = cy0 / CHUNK_SIZE;
	const int kz0 = cz0 / CHUNK_SIZE;
	const int kx1 = MIN((cx1 - 1) / CHUNK_SIZE, chunk_resolution.x - 1);
	const int ky1 = MIN((cy1 - 1) / CHUNK_SIZE, chunk_resolution.y - 1);
	const int kz1 = MIN((cz1 - 1) / CHUNK_SIZE, chunk_resolution.z - 1);

	for (int z = kz0; z <= kz1; z++) {
		for (int y = ky0; y <= ky1; y++) {
			for (int x = kx0; x <= kx1; x++) {
				_mark_chunk_dirty((uint32_t)((z * chunk_resolution.y + y) * chunk_resolution.x + x));
			}
		}
	}
}

void VoxelDenseStorage::mark_all_dirty() {
	dirty_chunks.clear();
	for (uint32_t i = 0; i < chunk_dirty.size(); i++) {
		chunk_dirty[i] = 1;
		dirty_chunks.push_back(i);
	}
}

void VoxelDenseStorage::clear_dirty() {
	for (uint32_t i = 0; i < dirty_chunks.size(); i++) {
		chunk_dirty[dirty_chunks[i]] = 0;
	}
	dirty_chunks.clear();
}

Vector3i VoxelDenseStorage::chunk_to_cell_origin(uint32_t p_chunk_index) const {
	const int per_slice = chunk_resolution.x * chunk_resolution.y;
	const int z = (int)p_chunk_index / per_slice;
	const int rem = (int)p_chunk_index % per_slice;
	const int y = rem / chunk_resolution.x;
	const int x = rem % chunk_resolution.x;
	return Vector3i(x, y, z) * CHUNK_SIZE;
}

uint64_t VoxelDenseStorage::get_memory_usage() const {
	uint64_t total = (uint64_t)sdf.size() * sizeof(float);
	for (uint32_t i = 0; i < channels.size(); i++) {
		total += channels[i].data.size();
	}
	total += chunk_dirty.size();
	total += (uint64_t)dirty_chunks.size() * sizeof(uint32_t);
	return total;
}
