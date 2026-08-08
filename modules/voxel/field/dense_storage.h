/**************************************************************************/
/*  dense_storage.h                                                       */
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

#pragma once

#include "channels.h"

#include "core/math/aabb.h"
#include "core/math/vector3i.h"

// Phase 1 storage: one float of signed distance per grid sample, no sparsity.
//
// This exists to be the simplest thing that can hold a field, so that the sparse
// brick pool replacing it has something to be validated against. Negative is
// inside the solid, positive is outside, and the surface is the zero crossing.
class VoxelDenseStorage {
public:
	// Cells per chunk edge. A chunk is the unit of dirty tracking and of
	// parallel extraction. The brick pool keeps this size so that a chunk index
	// stays usable as a brick index.
	static constexpr int CHUNK_SIZE = 16;

	// A guard rail, not a design limit. A dense grid at the design's 0.5 mm
	// target is several gigabytes, so a caller that trips this wants sparse
	// storage and is better told so than left to thrash.
	static constexpr uint64_t MAX_SAMPLES = 96 * 1024 * 1024;

private:
	AABB bounds = AABB(Vector3(), Vector3(1, 1, 1));
	float voxel_size = 0.01f;

	Vector3i sample_resolution;
	Vector3i cell_resolution;
	Vector3i chunk_resolution;

	LocalVector<float> sdf;
	LocalVector<VoxelChannel> channels;

	LocalVector<uint8_t> chunk_dirty;
	LocalVector<uint32_t> dirty_chunks;

	void _mark_chunk_dirty(uint32_t p_chunk_index);

public:
	Error configure(const AABB &p_bounds, float p_voxel_size);
	bool is_configured() const { return !sdf.is_empty(); }

	AABB get_bounds() const { return bounds; }
	float get_voxel_size() const { return voxel_size; }
	Vector3i get_sample_resolution() const { return sample_resolution; }
	Vector3i get_cell_resolution() const { return cell_resolution; }
	Vector3i get_chunk_resolution() const { return chunk_resolution; }
	uint64_t get_sample_count() const { return sdf.size(); }
	uint32_t get_chunk_count() const { return chunk_dirty.size(); }

	// The value a sample holds when nothing has ever been written near it. Finite
	// so that arithmetic stays well behaved, and far enough that it can never be
	// mistaken for part of a surface.
	float get_far_value() const { return bounds.size.length(); }

	_FORCE_INLINE_ int64_t sample_index(int p_x, int p_y, int p_z) const {
		return ((int64_t)p_z * sample_resolution.y + p_y) * sample_resolution.x + p_x;
	}
	_FORCE_INLINE_ Vector3 sample_to_local(int p_x, int p_y, int p_z) const {
		return bounds.position + Vector3(p_x, p_y, p_z) * voxel_size;
	}

	const float *get_sdf_ptr() const { return sdf.ptr(); }
	float *get_sdf_ptrw() { return sdf.ptr(); }
	void fill_sdf(float p_value);

	float sample_local(const Vector3 &p_local) const;
	Vector3 gradient_local(const Vector3 &p_local) const;
	// Marches along the ray until the sign flips, then bisects. Returns false when
	// the ray leaves the bounds without ever entering the solid.
	bool raycast_local(const Vector3 &p_from, const Vector3 &p_dir, float p_max_distance, Vector3 &r_hit, Vector3 &r_normal) const;

	int add_channel(const StringName &p_name, VoxelChannelFormat p_format);
	int find_channel(const StringName &p_name) const;
	int get_channel_count() const { return channels.size(); }
	const VoxelChannel &get_channel(int p_index) const { return channels[p_index]; }
	VoxelChannel &get_channel_mut(int p_index) { return channels[p_index]; }

	// Region marking, in field-local space to match bounds.
	void mark_region_dirty(const AABB &p_local_aabb);
	void mark_all_dirty();
	void clear_dirty();
	const LocalVector<uint32_t> &get_dirty_chunks() const { return dirty_chunks; }
	Vector3i chunk_to_cell_origin(uint32_t p_chunk_index) const;

	uint64_t get_memory_usage() const;
};
