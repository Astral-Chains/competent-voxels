/**************************************************************************/
/*  voxel_brush_mesh.cpp                                                  */
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

#include "voxel_brush_mesh.h"

#include "core/math/face3.h"
#include "core/object/class_db.h"

void VoxelBrushMesh::set_mesh(const Ref<Mesh> &p_mesh) {
	if (mesh == p_mesh) {
		return;
	}
	mesh = p_mesh;
	_invalidate();
	emit_changed();
}

Ref<Mesh> VoxelBrushMesh::get_mesh() const {
	return mesh;
}

void VoxelBrushMesh::set_resolution(int p_resolution) {
	const int clamped = CLAMP(p_resolution, 4, 256);
	if (resolution == clamped) {
		return;
	}
	resolution = clamped;
	_invalidate();
	emit_changed();
}

int VoxelBrushMesh::get_resolution() const {
	return resolution;
}

void VoxelBrushMesh::set_padding(float p_padding) {
	const float clamped = CLAMP(p_padding, 0.0f, 2.0f);
	if (Math::is_equal_approx(padding, clamped)) {
		return;
	}
	padding = clamped;
	_invalidate();
	emit_changed();
}

float VoxelBrushMesh::get_padding() const {
	return padding;
}

void VoxelBrushMesh::_invalidate() {
	baked = false;
	baked_sdf.clear();
	baked_bounds = AABB();
	baked_resolution = Vector3i();
	baked_cell_size = 0.0f;
}

float VoxelBrushMesh::_winding_number(const Vector3 &p_point, const PackedVector3Array &p_triangles) {
	// Sum of the solid angles each triangle subtends at the query point, over 4 pi.
	// One for a point enclosed by a closed surface, zero for a point outside, and
	// degrading gracefully rather than leaking when the surface has holes.
	const Vector3 *r = p_triangles.ptr();
	const int count = p_triangles.size();
	double total = 0.0;

	for (int i = 0; i + 2 < count; i += 3) {
		const Vector3 a = r[i + 0] - p_point;
		const Vector3 b = r[i + 1] - p_point;
		const Vector3 c = r[i + 2] - p_point;
		const double la = a.length();
		const double lb = b.length();
		const double lc = c.length();
		if (la < CMP_EPSILON || lb < CMP_EPSILON || lc < CMP_EPSILON) {
			// The point sits on a vertex, so it is on the surface by definition.
			return 1.0f;
		}
		const double numerator = a.dot(b.cross(c));
		const double denominator = la * lb * lc + a.dot(b) * lc + a.dot(c) * lb + b.dot(c) * la;
		total += 2.0 * Math::atan2(numerator, denominator);
	}

	return (float)(total / (4.0 * Math::PI));
}

void VoxelBrushMesh::_bake() const {
	baked = true;
	baked_sdf.clear();

	if (mesh.is_null()) {
		return;
	}
	const Vector<Face3> faces = mesh->get_faces();
	if (faces.is_empty()) {
		return;
	}

	PackedVector3Array triangles;
	triangles.resize(faces.size() * 3);
	{
		Vector3 *w = triangles.ptrw();
		const Face3 *r = faces.ptr();
		for (int i = 0; i < faces.size(); i++) {
			w[i * 3 + 0] = r[i].vertex[0];
			w[i * 3 + 1] = r[i].vertex[1];
			w[i * 3 + 2] = r[i].vertex[2];
		}
	}

	AABB mesh_bounds(triangles[0], Vector3());
	for (int i = 1; i < triangles.size(); i++) {
		mesh_bounds.expand_to(triangles[i]);
	}
	const Vector3 pad = mesh_bounds.size * padding + Vector3(CMP_EPSILON, CMP_EPSILON, CMP_EPSILON);
	mesh_bounds.position -= pad;
	mesh_bounds.size += pad * 2.0f;

	const float longest = MAX(mesh_bounds.size.x, MAX(mesh_bounds.size.y, mesh_bounds.size.z));
	baked_cell_size = longest / (float)resolution;
	baked_resolution = Vector3i(
			MAX(2, (int)Math::ceil(mesh_bounds.size.x / baked_cell_size) + 1),
			MAX(2, (int)Math::ceil(mesh_bounds.size.y / baked_cell_size) + 1),
			MAX(2, (int)Math::ceil(mesh_bounds.size.z / baked_cell_size) + 1));
	baked_bounds = mesh_bounds;

	const uint64_t sample_count = (uint64_t)baked_resolution.x * baked_resolution.y * baked_resolution.z;
	baked_sdf.resize(sample_count);

	const Face3 *face_ptr = faces.ptr();
	const int face_count = faces.size();

	for (int z = 0; z < baked_resolution.z; z++) {
		for (int y = 0; y < baked_resolution.y; y++) {
			for (int x = 0; x < baked_resolution.x; x++) {
				const Vector3 point = baked_bounds.position + Vector3(x, y, z) * baked_cell_size;

				real_t closest = 1e30;
				for (int f = 0; f < face_count; f++) {
					const real_t d = point.distance_squared_to(face_ptr[f].get_closest_point_to(point));
					if (d < closest) {
						closest = d;
					}
				}

				const float distance = (float)Math::sqrt((double)closest);
				// Magnitude, not sign: the winding number is negative inside a mesh whose
				// triangles wind clockwise, which is Godot's convention, and a brush should
				// not care which way round the mesh it was handed happens to be.
				const float winding = Math::abs(_winding_number(point, triangles));
				const int64_t index = ((int64_t)z * baked_resolution.y + y) * baked_resolution.x + x;
				baked_sdf[index] = winding > 0.5f ? -distance : distance;
			}
		}
	}
}

float VoxelBrushMesh::sample_local(const Vector3 &p_local) const {
	if (!baked) {
		_bake();
	}
	if (baked_sdf.is_empty()) {
		return get_extent();
	}

	const Vector3 grid = (p_local - baked_bounds.position) / baked_cell_size;
	const int x0 = CLAMP((int)Math::floor(grid.x), 0, baked_resolution.x - 1);
	const int y0 = CLAMP((int)Math::floor(grid.y), 0, baked_resolution.y - 1);
	const int z0 = CLAMP((int)Math::floor(grid.z), 0, baked_resolution.z - 1);
	const int x1 = MIN(x0 + 1, baked_resolution.x - 1);
	const int y1 = MIN(y0 + 1, baked_resolution.y - 1);
	const int z1 = MIN(z0 + 1, baked_resolution.z - 1);
	const float tx = CLAMP(grid.x - (float)x0, 0.0f, 1.0f);
	const float ty = CLAMP(grid.y - (float)y0, 0.0f, 1.0f);
	const float tz = CLAMP(grid.z - (float)z0, 0.0f, 1.0f);

	auto at = [&](int x, int y, int z) {
		return baked_sdf[((int64_t)z * baked_resolution.y + y) * baked_resolution.x + x];
	};
	const float c00 = Math::lerp(at(x0, y0, z0), at(x1, y0, z0), tx);
	const float c10 = Math::lerp(at(x0, y1, z0), at(x1, y1, z0), tx);
	const float c01 = Math::lerp(at(x0, y0, z1), at(x1, y0, z1), tx);
	const float c11 = Math::lerp(at(x0, y1, z1), at(x1, y1, z1), tx);
	const float interpolated = Math::lerp(Math::lerp(c00, c10, ty), Math::lerp(c01, c11, ty), tz);

	// Outside the baked grid the interpolant would clamp and read as solid, so fall
	// back to the distance to the grid itself, which is correct and always positive.
	if (!baked_bounds.has_point(p_local)) {
		const Vector3 clamped(
				CLAMP(p_local.x, baked_bounds.position.x, baked_bounds.position.x + baked_bounds.size.x),
				CLAMP(p_local.y, baked_bounds.position.y, baked_bounds.position.y + baked_bounds.size.y),
				CLAMP(p_local.z, baked_bounds.position.z, baked_bounds.position.z + baked_bounds.size.z));
		return p_local.distance_to(clamped) + MAX(interpolated, 0.0f);
	}
	return interpolated;
}

float VoxelBrushMesh::get_extent() const {
	if (!baked) {
		_bake();
	}
	if (baked_sdf.is_empty()) {
		return 0.0f;
	}
	return (baked_bounds.size * 0.5f).length();
}

void VoxelBrushMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &VoxelBrushMesh::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &VoxelBrushMesh::get_mesh);
	ClassDB::bind_method(D_METHOD("set_resolution", "resolution"), &VoxelBrushMesh::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &VoxelBrushMesh::get_resolution);
	ClassDB::bind_method(D_METHOD("set_padding", "padding"), &VoxelBrushMesh::set_padding);
	ClassDB::bind_method(D_METHOD("get_padding"), &VoxelBrushMesh::get_padding);
	ClassDB::bind_method(D_METHOD("is_baked"), &VoxelBrushMesh::is_baked);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "4,256,1"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "padding", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_padding", "get_padding");
}
