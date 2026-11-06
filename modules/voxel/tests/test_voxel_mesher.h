/**************************************************************************/
/*  test_voxel_mesher.h                                                   */
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

#include "../field/dense_storage.h"
#include "../field/voxel_field.h"
#include "../mesher/voxel_mesher_marching_cubes.h"
#include "../ops/voxel_brush.h"
#include "../voxel_server.h"

#include "core/templates/hash_map.h"
#include "tests/test_macros.h"

namespace TestVoxelMesher {

struct MeshData {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedInt32Array indices;
};

static MeshData read_surface(const Ref<VoxelField> &p_field, int p_surface) {
	const Array arrays = p_field->get_surface_arrays(p_surface);
	MeshData data;
	data.vertices = arrays[Mesh::ARRAY_VERTEX];
	data.normals = arrays[Mesh::ARRAY_NORMAL];
	data.indices = arrays[Mesh::ARRAY_INDEX];
	return data;
}

// Vertices duplicated across a chunk boundary land on bit-identical positions,
// because the interpolation that produced them depends only on the two sample
// values at the ends of the shared edge. Keying on position rather than on index is
// therefore what makes the watertightness check work across chunks.
static uint64_t position_key(const Vector3 &p_position) {
	union {
		float f;
		uint32_t u;
	} x, y, z;
	x.f = p_position.x;
	y.f = p_position.y;
	z.f = p_position.z;
	uint64_t hash = 1469598103934665603ULL;
	for (const uint32_t value : { x.u, y.u, z.u }) {
		hash ^= value;
		hash *= 1099511628211ULL;
	}
	return hash;
}

static Ref<VoxelField> make_sphere_field(float p_radius, float p_voxel_size, const Vector3 &p_centre = Vector3()) {
	Ref<VoxelField> field;
	field.instantiate();
	const float extent = p_radius * 2.0f;
	field->set_bounds(AABB(Vector3(-extent, -extent, -extent), Vector3(extent, extent, extent) * 2.0f));
	field->set_voxel_size(p_voxel_size);

	Ref<VoxelBrushSphere> brush;
	brush.instantiate();
	brush->set_radius(p_radius);
	brush->set_material_id(1);
	Transform3D transform;
	transform.origin = p_centre;
	field->stamp(brush, transform, VoxelField::OP_UNION);
	return field;
}

// A sphere centered exactly on a grid sample makes the distance field tie exactly at
// every symmetry plane, which is a genuinely degenerate input rather than a
// representative one. Offsetting by an awkward fraction of a voxel is what a real
// stroke looks like.
static Vector3 off_grid_centre(float p_voxel_size) {
	return Vector3(0.37f, -0.23f, 0.11f) * p_voxel_size;
}

static int count_boundary_and_nonmanifold_edges(const MeshData &p_mesh, int &r_boundary, int &r_nonmanifold, int &r_total) {
	HashMap<uint64_t, int> edge_uses;
	const int32_t *indices = p_mesh.indices.ptr();
	const Vector3 *vertices = p_mesh.vertices.ptr();

	for (int i = 0; i < p_mesh.indices.size(); i += 3) {
		for (int e = 0; e < 3; e++) {
			const uint64_t a = position_key(vertices[indices[i + e]]);
			const uint64_t b = position_key(vertices[indices[i + (e + 1) % 3]]);
			const uint64_t key = a < b ? (a ^ (b * 31ULL)) : (b ^ (a * 31ULL));
			if (int *count = edge_uses.getptr(key)) {
				(*count)++;
			} else {
				edge_uses.insert(key, 1);
			}
		}
	}

	r_boundary = 0;
	r_nonmanifold = 0;
	r_total = edge_uses.size();
	for (const KeyValue<uint64_t, int> &pair : edge_uses) {
		if (pair.value < 2) {
			r_boundary++;
		} else if (pair.value > 2) {
			r_nonmanifold++;
		}
	}
	return r_boundary + r_nonmanifold;
}

// Same check keyed by vertex index. Position welding can report a closed surface while
// the uploaded index buffer still has cracks; this is what the GPU actually draws.
static int count_index_boundary_edges(const MeshData &p_mesh, int &r_boundary, int &r_nonmanifold, int &r_total) {
	HashMap<uint64_t, int> edge_uses;
	const int32_t *indices = p_mesh.indices.ptr();

	for (int i = 0; i < p_mesh.indices.size(); i += 3) {
		for (int e = 0; e < 3; e++) {
			const int32_t ia = indices[i + e];
			const int32_t ib = indices[i + (e + 1) % 3];
			const uint64_t a = (uint64_t)MIN(ia, ib);
			const uint64_t b = (uint64_t)MAX(ia, ib);
			const uint64_t key = (a << 32) | b;
			if (int *count = edge_uses.getptr(key)) {
				(*count)++;
			} else {
				edge_uses.insert(key, 1);
			}
		}
	}

	r_boundary = 0;
	r_nonmanifold = 0;
	r_total = edge_uses.size();
	for (const KeyValue<uint64_t, int> &pair : edge_uses) {
		if (pair.value < 2) {
			r_boundary++;
		} else if (pair.value > 2) {
			r_nonmanifold++;
		}
	}
	return r_boundary + r_nonmanifold;
}

// This is the test that actually validates mc_tables.h. Any single wrong entry in
// the 256-row triangulation table tears a hole in the surface, and a hole is an edge
// that only one triangle uses. It is also what a mistake in stitching one chunk's
// output to the next produces, so this covers both.
TEST_CASE("[Voxel] Marching cubes produces a closed surface") {
	Ref<VoxelField> field = make_sphere_field(0.2f, 0.01f, off_grid_centre(0.01f));
	REQUIRE(field->get_surface_count() == 1);

	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.indices.size() > 0);
	REQUIRE(mesh.indices.size() % 3 == 0);

	int boundary = 0;
	int nonmanifold = 0;
	int total = 0;
	count_boundary_and_nonmanifold_edges(mesh, boundary, nonmanifold, total);

	CHECK_MESSAGE(boundary == 0, vformat("%d of %d surface edges are used by a single triangle, so the surface has holes.", boundary, total));
	CHECK_MESSAGE(nonmanifold == 0, vformat("%d of %d surface edges are used by more than two triangles.", nonmanifold, total));
}

// Splitting the isosurface into one draw call per material leaves each surface open
// at paint boundaries. Welding everything into one surface must keep the manifold
// closed after a multi-material union.
TEST_CASE("[Voxel] Multi-material union keeps a single closed surface") {
	Ref<VoxelField> field;
	field.instantiate();
	field->set_bounds(AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5)));
	field->set_voxel_size(0.01f);

	Ref<VoxelBrushBox> block;
	block.instantiate();
	block->set_size(Vector3(0.3, 0.3, 0.3));
	block->set_material_id(1);
	field->stamp(block, Transform3D(), VoxelField::OP_UNION);

	Ref<VoxelBrushSphere> bump;
	bump.instantiate();
	bump->set_radius(0.06f);
	bump->set_material_id(2);
	Transform3D bump_xform;
	bump_xform.origin = Vector3(0.0f, 0.15f + 0.03f, 0.0f);
	field->stamp(bump, bump_xform, VoxelField::OP_UNION);

	REQUIRE(field->get_surface_count() == 1);
	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.indices.size() > 0);
	int boundary = 0;
	int nonmanifold = 0;
	int total = 0;
	count_boundary_and_nonmanifold_edges(mesh, boundary, nonmanifold, total);
	CHECK_MESSAGE(boundary == 0,
			vformat("Welded multi-material surface has %d boundary edges of %d.", boundary, total));

	int index_boundary = 0;
	int index_nonmanifold = 0;
	int index_total = 0;
	count_index_boundary_edges(mesh, index_boundary, index_nonmanifold, index_total);
	CHECK_MESSAGE(index_boundary == 0,
			vformat("Index buffer still has %d open edges of %d after multi-material weld.", index_boundary, index_total));

	// Paint must cover the bump, not a thin ribbon. Vertex colour comes from the solid
	// side of each edge; a shallow ADD used to keep deepest-corner colour on mat 1 and
	// only tint a hollow-looking ring of mat 2.
	const Array arrays = field->get_surface_arrays(0);
	const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
	const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
	REQUIRE(colors.size() == vertices.size());
	int green_near_bump = 0;
	int tris_near_bump = 0;
	const Vector3 bump_centre = bump_xform.origin;
	for (int i = 0; i < indices.size(); i += 3) {
		const Vector3 centre = (vertices[indices[i]] + vertices[indices[i + 1]] + vertices[indices[i + 2]]) / 3.0f;
		if (centre.distance_to(bump_centre) > 0.08f) {
			continue;
		}
		tris_near_bump++;
		const float avg_mat = (colors[indices[i]].r + colors[indices[i + 1]].r + colors[indices[i + 2]].r) * 255.0f / 3.0f;
		if (avg_mat >= 1.5f) {
			green_near_bump++;
		}
	}
	REQUIRE(tris_near_bump > 0);
	CHECK_MESSAGE(green_near_bump * 5 > tris_near_bump * 4,
			vformat("Only %d of %d triangles near the ADD bump are material 2 — paint looks like a hollow shell.", green_near_bump, tris_near_bump));

	// The cap that faces the camera is the empty hole. Every vertex on those
	// triangles must be material 2; one leftover mat-1 rim vertex plus `flat`
	// interpolation paints the whole front tan.
	int cap_tris = 0;
	int cap_all_green = 0;
	const float cap_height = bump_centre.y + 0.02f;
	for (int i = 0; i < indices.size(); i += 3) {
		const Vector3 a = vertices[indices[i]];
		const Vector3 b = vertices[indices[i + 1]];
		const Vector3 c = vertices[indices[i + 2]];
		const Vector3 centre = (a + b + c) / 3.0f;
		if (centre.y < cap_height) {
			continue;
		}
		if (centre.distance_to(bump_centre) > 0.07f) {
			continue;
		}
		cap_tris++;
		const uint32_t m0 = (uint32_t)Math::round(colors[indices[i]].r * 255.0f);
		const uint32_t m1 = (uint32_t)Math::round(colors[indices[i + 1]].r * 255.0f);
		const uint32_t m2 = (uint32_t)Math::round(colors[indices[i + 2]].r * 255.0f);
		if (m0 >= 2 && m1 >= 2 && m2 >= 2) {
			cap_all_green++;
		}
	}
	REQUIRE(cap_tris > 0);
	CHECK_MESSAGE(cap_all_green == cap_tris,
			vformat("%d of %d camera-facing cap triangles still have a material-1 vertex.", cap_tris - cap_all_green, cap_tris));
}

// Marching cubes promises a closed surface, not a manifold one. When the field ties
// exactly across a cell, two sheets can meet along one edge, and the sheets are still
// individually closed so nothing downstream breaks. This pins that distinction down
// so a real hole can never be waved away as symmetry.
TEST_CASE("[Voxel] A perfectly symmetric field still produces a surface without holes") {
	Ref<VoxelField> field = make_sphere_field(0.2f, 0.01f);
	REQUIRE(field->get_surface_count() == 1);

	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.indices.size() > 0);

	int boundary = 0;
	int nonmanifold = 0;
	int total = 0;
	count_boundary_and_nonmanifold_edges(mesh, boundary, nonmanifold, total);

	CHECK_MESSAGE(boundary == 0, vformat("%d of %d surface edges are used by a single triangle, so the surface has holes.", boundary, total));
}

TEST_CASE("[Voxel] Marching cubes produces a closed surface within a single chunk") {
	// The same check on a field small enough to be one chunk. If this passes while the
	// multi-chunk case fails, the fault is in how chunks are stitched rather than in
	// the triangulation.
	Ref<VoxelField> field = make_sphere_field(0.05f, 0.02f, off_grid_centre(0.02f));
	REQUIRE(field->get_cell_resolution().x <= VoxelDenseStorage::CHUNK_SIZE);
	REQUIRE(field->get_surface_count() == 1);

	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.indices.size() > 0);

	int boundary = 0;
	int nonmanifold = 0;
	int total = 0;
	const int bad = count_boundary_and_nonmanifold_edges(mesh, boundary, nonmanifold, total);
	CHECK_MESSAGE(bad == 0, vformat("%d boundary and %d non-manifold edges of %d inside a single chunk.", boundary, nonmanifold, total));
}

TEST_CASE("[Voxel] Marching cubes reproduces the volume of the shape it was cut from") {
	const float radius = 0.2f;
	Ref<VoxelField> field = make_sphere_field(radius, 0.008f);
	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.indices.size() > 0);

	// Divergence theorem over the triangle soup. The sign depends on the winding, so
	// the magnitude is the geometry and the sign is the convention.
	const int32_t *indices = mesh.indices.ptr();
	const Vector3 *vertices = mesh.vertices.ptr();
	double signed_volume = 0.0;
	for (int i = 0; i < mesh.indices.size(); i += 3) {
		const Vector3 a = vertices[indices[i + 0]];
		const Vector3 b = vertices[indices[i + 1]];
		const Vector3 c = vertices[indices[i + 2]];
		signed_volume += (double)a.dot(b.cross(c)) / 6.0;
	}

	const double expected = 4.0 / 3.0 * Math::PI * Math::pow((double)radius, 3.0);
	const double measured = Math::abs(signed_volume);
	const bool within_tolerance = measured > expected * 0.97 && measured < expected * 1.03;
	CHECK_MESSAGE(within_tolerance,
			vformat("Extracted volume %f differs from the analytic sphere volume %f by more than three percent.",
					measured, expected));

	// Godot treats clockwise winding as front facing, which for outward-facing
	// triangles makes the divergence theorem come out negative. If this ever flips,
	// every voxel surface in the engine is inside out.
	CHECK_MESSAGE(signed_volume < 0.0, "Surface winding is no longer clockwise when viewed from outside.");
}

TEST_CASE("[Voxel] Surface normals point out of the solid") {
	Ref<VoxelField> field = make_sphere_field(0.2f, 0.01f);
	const MeshData mesh = read_surface(field, 0);
	REQUIRE(mesh.normals.size() == mesh.vertices.size());

	const Vector3 *vertices = mesh.vertices.ptr();
	const Vector3 *normals = mesh.normals.ptr();
	int outward = 0;
	for (int i = 0; i < mesh.vertices.size(); i++) {
		// On a sphere centered at the origin the outward direction is known exactly.
		if (normals[i].dot(vertices[i].normalized()) > 0.9f) {
			outward++;
		}
	}
	CHECK_MESSAGE(outward == mesh.vertices.size(),
			vformat("%d of %d normals do not point away from the center of the sphere.",
					mesh.vertices.size() - outward, mesh.vertices.size()));
}

TEST_CASE("[Voxel] Finer voxels give a finer surface of the same shape") {
	Ref<VoxelField> coarse = make_sphere_field(0.2f, 0.02f);
	Ref<VoxelField> fine = make_sphere_field(0.2f, 0.01f);

	const int coarse_triangles = coarse->get_triangle_count();
	const int fine_triangles = fine->get_triangle_count();
	REQUIRE(coarse_triangles > 0);
	CHECK(fine_triangles > coarse_triangles * 2);
}

TEST_CASE("[Voxel] The mesher reports which algorithm it is") {
	Ref<VoxelMesherMarchingCubes> mesher;
	mesher.instantiate();
	CHECK(mesher->get_algorithm_name() == "Marching Cubes");
}

TEST_CASE("[Voxel] Extraction skips regions nothing has touched") {
	VoxelServer *server = VoxelServer::get_singleton();
	Ref<VoxelField> field = make_sphere_field(0.2f, 0.01f);
	field->get_triangle_count();
	CHECK(server->get_monitor(VoxelServer::MONITOR_DIRTY_CHUNKS) == doctest::Approx(0.0));

	const Vector3i cells = field->get_cell_resolution();
	const Vector3i chunks = (cells + Vector3i(15, 15, 15)) / 16;
	const int total_chunks = chunks.x * chunks.y * chunks.z;
	REQUIRE_MESSAGE(total_chunks > 64, "This field is too small for the dirty set to be a meaningful subset of it.");

	Ref<VoxelBrushSphere> brush;
	brush.instantiate();
	brush->set_radius(0.01f);
	field->stamp(brush, Transform3D(), VoxelField::OP_SUBTRACT);

	// A cut a centimeter across cannot possibly have changed the whole field, and if
	// the dirty set says otherwise then extraction is redoing work every stroke.
	const double dirty = server->get_monitor(VoxelServer::MONITOR_DIRTY_CHUNKS);
	CHECK(dirty > 0.0);
	CHECK_MESSAGE(dirty < total_chunks * 0.1,
			vformat("A one centimeter cut dirtied %d of %d chunks.", (int)dirty, total_chunks));
}

} // namespace TestVoxelMesher
