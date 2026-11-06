/**************************************************************************/
/*  voxel_mesher_marching_cubes.cpp                                       */
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

#include "voxel_mesher_marching_cubes.h"

#include "../field/dense_storage.h"
#include "mc_tables.h"

#include "core/object/class_db.h"

namespace {

Vector3 sample_gradient(const VoxelDenseStorage &p_storage, const Vector3i &p_sample, int p_step) {
	const Vector3i res = p_storage.get_sample_resolution();
	const float *sdf = p_storage.get_sdf_ptr();

	auto at = [&](int x, int y, int z) {
		return sdf[p_storage.sample_index(
				CLAMP(x, 0, res.x - 1), CLAMP(y, 0, res.y - 1), CLAMP(z, 0, res.z - 1))];
	};

	const Vector3 gradient(
			at(p_sample.x + p_step, p_sample.y, p_sample.z) - at(p_sample.x - p_step, p_sample.y, p_sample.z),
			at(p_sample.x, p_sample.y + p_step, p_sample.z) - at(p_sample.x, p_sample.y - p_step, p_sample.z),
			at(p_sample.x, p_sample.y, p_sample.z + p_step) - at(p_sample.x, p_sample.y, p_sample.z - p_step));

	if (gradient.is_zero_approx()) {
		return Vector3(0, 1, 0);
	}
	return gradient.normalized();
}

_FORCE_INLINE_ uint64_t edge_key(const Vector3i &p_lower_sample, int p_axis) {
	return ((uint64_t)p_lower_sample.x) |
			((uint64_t)p_lower_sample.y << 20) |
			((uint64_t)p_lower_sample.z << 40) |
			((uint64_t)p_axis << 60);
}

} // namespace

void VoxelMesherMarchingCubes::build(const VoxelDenseStorage &p_storage, const Input &p_input, Output &r_output) const {
	if (!p_storage.is_configured()) {
		return;
	}

	const int step = MAX(p_input.step, 1);
	const Vector3i cell_res = p_storage.get_cell_resolution();
	const float *sdf = p_storage.get_sdf_ptr();

	const int channel_count = p_storage.get_channel_count();
	const int material_channel = channel_count > (int)VoxelChannels::MATERIAL_ID_CHANNEL
			? (int)VoxelChannels::MATERIAL_ID_CHANNEL
			: -1;

	const Vector3i begin = p_input.cell_origin;
	const Vector3i end(
			MIN(p_input.cell_origin.x + p_input.cell_size.x, cell_res.x),
			MIN(p_input.cell_origin.y + p_input.cell_size.y, cell_res.y),
			MIN(p_input.cell_origin.z + p_input.cell_size.z, cell_res.z));

	// One geometric isosurface, one vertex cache, one output surface. A vertex lives
	// on a grid edge; its material is the higher ID of the two endpoints. The solid
	// endpoint of a shallow ADD is still the old block, which painted the camera-
	// facing cap tan and looked like a hole through the new material.
	Surface &surface = r_output.surfaces.insert(0, Surface())->value;
	HashMap<uint64_t, int32_t> edge_vertices;
	Vector<Color> &colors = surface.colors;
	const VoxelChannel *materials = material_channel >= 0 ? &p_storage.get_channel(material_channel) : nullptr;

	for (int z = begin.z; z < end.z; z += step) {
		for (int y = begin.y; y < end.y; y += step) {
			for (int x = begin.x; x < end.x; x += step) {
				Vector3i corner_samples[8];
				float corner_sdf[8];
				int case_index = 0;

				for (int c = 0; c < 8; c++) {
					const Vector3i sample(
							MIN(x + MarchingCubes::CORNER_OFFSET[c][0] * step, cell_res.x),
							MIN(y + MarchingCubes::CORNER_OFFSET[c][1] * step, cell_res.y),
							MIN(z + MarchingCubes::CORNER_OFFSET[c][2] * step, cell_res.z));
					corner_samples[c] = sample;
					corner_sdf[c] = sdf[p_storage.sample_index(sample.x, sample.y, sample.z)];
					if (corner_sdf[c] < 0.0f) {
						case_index |= 1 << c;
					}
				}

				if (case_index == 0 || case_index == 255) {
					continue;
				}

				const int8_t *row = MarchingCubes::TRI_TABLE[case_index];
				for (int t = 0; row[t] >= 0; t += 3) {
					int32_t triangle[3] = { -1, -1, -1 };

					for (int v = 0; v < 3; v++) {
						const int edge = row[t + v];
						const int ca = MarchingCubes::EDGE_CORNERS[edge][0];
						const int cb = MarchingCubes::EDGE_CORNERS[edge][1];
						const Vector3i &sa = corner_samples[ca];
						const Vector3i &sb = corner_samples[cb];

						const Vector3i s_lo(MIN(sa.x, sb.x), MIN(sa.y, sb.y), MIN(sa.z, sb.z));
						const Vector3i s_hi(MAX(sa.x, sb.x), MAX(sa.y, sb.y), MAX(sa.z, sb.z));
						if (s_lo == s_hi) {
							triangle[v] = -1;
							break;
						}
						const int axis = s_lo.x != s_hi.x ? 0 : (s_lo.y != s_hi.y ? 1 : 2);
						const uint64_t key = edge_key(s_lo, axis);

						if (const int32_t *cached = edge_vertices.getptr(key)) {
							triangle[v] = *cached;
							continue;
						}

						const float v_lo = sdf[p_storage.sample_index(s_lo.x, s_lo.y, s_lo.z)];
						const float v_hi = sdf[p_storage.sample_index(s_hi.x, s_hi.y, s_hi.z)];
						const float denominator = v_hi - v_lo;
						const float t_edge = Math::is_zero_approx(denominator)
								? 0.5f
								: CLAMP(-v_lo / denominator, 0.0f, 1.0f);

						const Vector3 p_lo = p_storage.sample_to_local(s_lo.x, s_lo.y, s_lo.z);
						const Vector3 p_hi = p_storage.sample_to_local(s_hi.x, s_hi.y, s_hi.z);
						const Vector3 position = p_lo.lerp(p_hi, t_edge);

						const Vector3 g_lo = sample_gradient(p_storage, s_lo, step);
						const Vector3 g_hi = sample_gradient(p_storage, s_hi, step);
						Vector3 normal = g_lo.lerp(g_hi, t_edge);
						if (normal.is_zero_approx()) {
							normal = g_lo;
						} else {
							normal.normalize();
						}

						uint32_t edge_material = VoxelChannels::MATERIAL_ID_INTERIOR;
						if (materials) {
							const uint32_t mat_lo = materials->get_raw((uint64_t)p_storage.sample_index(s_lo.x, s_lo.y, s_lo.z));
							const uint32_t mat_hi = materials->get_raw((uint64_t)p_storage.sample_index(s_hi.x, s_hi.y, s_hi.z));
							edge_material = MAX(mat_lo, mat_hi);
						}

						const int32_t index = surface.vertices.size();
						surface.vertices.push_back(position);
						surface.normals.push_back(normal);
						colors.push_back(Color(float(edge_material) / 255.0f, 0.0f, 0.0f, 1.0f));

						if (p_input.with_channels) {
							for (uint32_t ch = 0; ch < VoxelChannels::CUSTOM0_CHANNEL_COUNT; ch++) {
								const int channel = (int)(VoxelChannels::CUSTOM0_FIRST_CHANNEL + ch);
								uint8_t packed = 0;
								if (channel < channel_count) {
									const VoxelChannel &data = p_storage.get_channel(channel);
									const float a = data.get_normalized((uint64_t)p_storage.sample_index(s_lo.x, s_lo.y, s_lo.z));
									const float b = data.get_normalized((uint64_t)p_storage.sample_index(s_hi.x, s_hi.y, s_hi.z));
									packed = (uint8_t)CLAMP(Math::round(Math::lerp(a, b, t_edge) * 255.0f), 0.0f, 255.0f);
								}
								surface.custom0.push_back(packed);
							}
						}

						edge_vertices.insert(key, index);
						triangle[v] = index;
					}

					if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0) {
						continue;
					}
					if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2]) {
						continue;
					}

					surface.indices.push_back(triangle[0]);
					surface.indices.push_back(triangle[2]);
					surface.indices.push_back(triangle[1]);
					r_output.triangle_count++;
				}
			}
		}
	}
}

String VoxelMesherMarchingCubes::get_algorithm_name() const {
	return "Marching Cubes";
}

void VoxelMesherMarchingCubes::_bind_methods() {
}
