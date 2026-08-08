/**************************************************************************/
/*  test_voxel_field.h                                                    */
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

#include "../field/voxel_field.h"
#include "../ops/voxel_brush.h"
#include "../voxel_server.h"

#include "tests/test_macros.h"

namespace TestVoxelField {

// A field small enough to extract in milliseconds and still large enough to span
// several chunks, so that anything that only works inside one chunk fails here.
static Ref<VoxelField> make_field(float p_voxel_size = 0.02f) {
	Ref<VoxelField> field;
	field.instantiate();
	field->set_bounds(AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5)));
	field->set_voxel_size(p_voxel_size);
	return field;
}

static Ref<VoxelBrushSphere> make_sphere(float p_radius, int p_material_id = 1) {
	Ref<VoxelBrushSphere> brush;
	brush.instantiate();
	brush->set_radius(p_radius);
	brush->set_material_id(p_material_id);
	return brush;
}

TEST_CASE("[Voxel] A field frees its server RID with the resource") {
	VoxelServer *server = VoxelServer::get_singleton();
	const int before = server->get_field_count();
	{
		Ref<VoxelField> field = make_field();
		CHECK(server->get_field_count() == before + 1);
		CHECK(field->get_rid().is_valid());
	}
	CHECK(server->get_field_count() == before);
}

TEST_CASE("[Voxel] A new field is empty everywhere") {
	Ref<VoxelField> field = make_field();
	CHECK(field->sample(Vector3()) > 0.0f);
	CHECK(field->get_volume() == doctest::Approx(0.0f));
	CHECK(field->get_triangle_count() == 0);
}

TEST_CASE("[Voxel] Resolution follows bounds and voxel size") {
	Ref<VoxelField> field = make_field(0.05f);
	// 0.5 m of bounds at 0.05 m per voxel is ten cells on every axis.
	CHECK(field->get_cell_resolution() == Vector3i(10, 10, 10));

	field->set_voxel_size(0.025f);
	CHECK(field->get_cell_resolution() == Vector3i(20, 20, 20));
}

TEST_CASE("[Voxel] The material ID channel exists without being asked for") {
	Ref<VoxelField> field = make_field();
	CHECK(field->has_channel("material_id"));
	CHECK(field->find_channel("material_id") == 0);
	CHECK(field->get_channel_count() == 1);
}

TEST_CASE("[Voxel] Declared channels are found by name and not duplicated") {
	Ref<VoxelField> field = make_field();
	const int first = field->add_channel("density", VoxelField::FORMAT_UNORM8);
	CHECK(first == 1);
	CHECK(field->add_channel("density", VoxelField::FORMAT_UNORM8) == first);
	CHECK(field->get_channel_count() == 2);
	CHECK(field->find_channel("nonexistent") == -1);
}

TEST_CASE("[Voxel] Union adds material and subtract removes it") {
	Ref<VoxelField> field = make_field();
	Ref<VoxelBrushSphere> brush = make_sphere(0.15f);

	field->stamp(brush, Transform3D(), VoxelField::OP_UNION);
	CHECK(field->sample(Vector3()) < 0.0f);
	const float filled = field->get_volume();
	CHECK(filled > 0.0f);

	Ref<VoxelBrushSphere> cutter = make_sphere(0.05f);
	field->stamp(cutter, Transform3D(), VoxelField::OP_SUBTRACT);
	CHECK(field->sample(Vector3()) > 0.0f);
	CHECK(field->get_volume() < filled);
}

TEST_CASE("[Voxel] Intersect keeps only the overlap") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);

	Transform3D offset;
	offset.origin = Vector3(0.1, 0, 0);
	field->stamp(make_sphere(0.15f), offset, VoxelField::OP_INTERSECT);

	// The overlap of two spheres offset along X contains the midpoint and excludes
	// the far side of either one.
	CHECK(field->sample(Vector3(0.05, 0, 0)) < 0.0f);
	CHECK(field->sample(Vector3(-0.12, 0, 0)) > 0.0f);
}

TEST_CASE("[Voxel] Paint changes material without changing the surface") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f, 1), Transform3D(), VoxelField::OP_UNION);
	const float before = field->get_volume();

	field->stamp(make_sphere(0.05f, 7), Transform3D(), VoxelField::OP_PAINT);
	CHECK(field->get_volume() == doctest::Approx(before));
}

TEST_CASE("[Voxel] Raycast finds the surface it passes through") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.1f), Transform3D(), VoxelField::OP_UNION);

	const Dictionary hit = field->raycast(Vector3(-0.24, 0, 0), Vector3(1, 0, 0), 0.5f);
	REQUIRE(hit.has("position"));
	const Vector3 position = hit["position"];
	// Within a voxel of the analytic sphere it was cut from.
	CHECK(Math::abs(position.x + 0.1f) < 0.02f);

	const Dictionary miss = field->raycast(Vector3(-0.24, 0.2, 0), Vector3(1, 0, 0), 0.5f);
	CHECK_FALSE(miss.has("position"));
}

TEST_CASE("[Voxel] A stroke leaves material along its whole length") {
	Ref<VoxelField> field = make_field();
	Ref<VoxelBrushSphere> brush = make_sphere(0.04f);

	PackedVector3Array path;
	path.push_back(Vector3(-0.15, 0, 0));
	path.push_back(Vector3(0.15, 0, 0));
	field->stamp_path(brush, path, VoxelField::OP_UNION);

	// Spacing is the engine's problem, so no point along the stroke may be hollow.
	for (int i = 0; i <= 10; i++) {
		const float x = Math::lerp(-0.15f, 0.15f, (float)i / 10.0f);
		CHECK_MESSAGE(field->sample(Vector3(x, 0, 0)) < 0.0f,
				vformat("Stroke has a gap at x = %f.", x));
	}
}

TEST_CASE("[Voxel] Clearing a field returns it to empty") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);
	REQUIRE(field->get_volume() > 0.0f);

	field->clear();
	CHECK(field->get_volume() == doctest::Approx(0.0f));
	CHECK(field->get_triangle_count() == 0);
}

TEST_CASE("[Voxel] Collision faces are coarser than the rendered surface") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);

	field->set_collision_step(1);
	const int fine = field->get_collision_faces().size();
	field->set_collision_step(4);
	const int coarse = field->get_collision_faces().size();

	REQUIRE(fine > 0);
	REQUIRE(coarse > 0);
	CHECK(coarse < fine);
}

TEST_CASE("[Voxel] A collision resolution can be asked for without imposing it") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);
	field->set_collision_step(4);

	// Two callers wanting different resolutions from one field is the case that
	// breaks if reading collision writes to the field, because whichever asked last
	// would decide for both.
	const int fine = field->get_collision_faces(1).size();
	const int coarse = field->get_collision_faces(4).size();
	REQUIRE(fine > 0);
	CHECK(coarse < fine);
	CHECK(field->get_collision_step() == 4);
	CHECK(field->get_collision_faces().size() == coarse);
}

TEST_CASE("[Voxel] Setting the collision step to what it already is announces nothing") {
	Ref<VoxelField> field = make_field();
	field->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);
	field->set_collision_step(4);

	// A listener that rebuilds itself from the field on a change, and touches the
	// field while rebuilding, recurses forever if the field announces a change that
	// did not happen. That is not a hypothetical: it is what VoxelShape3D does.
	SIGNAL_WATCH(field.ptr(), SNAME("changed"));

	field->set_collision_step(4);
	SIGNAL_CHECK_FALSE("changed");

	field->set_collision_step(2);
	SIGNAL_CHECK("changed", { {} });

	SIGNAL_UNWATCH(field.ptr(), SNAME("changed"));
}

} // namespace TestVoxelField
