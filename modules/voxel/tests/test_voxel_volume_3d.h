/**************************************************************************/
/*  test_voxel_volume_3d.h                                                */
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
#include "../scene/voxel_volume_3d.h"

#include "core/object/message_queue.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/material.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "tests/test_macros.h"

namespace TestVoxelVolume3D {

static Ref<VoxelField> make_block() {
	Ref<VoxelField> field;
	field.instantiate();
	field->set_bounds(AABB(Vector3(-0.25, -0.25, -0.25), Vector3(0.5, 0.5, 0.5)));
	field->set_voxel_size(0.02f);

	Ref<VoxelBrushBox> block;
	block.instantiate();
	block->set_size(Vector3(0.4, 0.4, 0.4));
	field->stamp(block, Transform3D(), VoxelField::OP_UNION);
	return field;
}

static Ref<VoxelBrushSphere> make_sphere(float p_radius) {
	Ref<VoxelBrushSphere> brush;
	brush.instantiate();
	brush->set_radius(p_radius);
	return brush;
}

TEST_CASE("[SceneTree][VoxelVolume3D] A volume draws the mesh its field extracts") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	// Nothing is drawn unless the instance is pointed at the field's mesh, and the
	// mesh RID is stable, so this is the one assignment that has to be right.
	CHECK(volume->get_base() == field->get_mesh_rid());
	CHECK(volume->get_base().is_valid());

	// Culling reads this, so a wrong AABB means a volume that vanishes when looked
	// at from the wrong angle rather than an obvious failure.
	CHECK(volume->get_aabb() == field->get_bounds());

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] A stroke of many stamps leaves the volume drawing one mesh") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	const int before = field->get_triangle_count();
	REQUIRE(before > 0);

	// The volume coalesces field changes to the end of the frame, so a stroke of
	// eight stamps has to survive being told eight times that the field moved.
	for (int i = 0; i < 8; i++) {
		Transform3D at;
		at.origin = Vector3(-0.15 + 0.04 * i, 0.0, 0.0);
		field->stamp(make_sphere(0.03f), at, VoxelField::OP_SUBTRACT);
	}
	MessageQueue::get_singleton()->flush();

	CHECK(field->get_triangle_count() != before);
	CHECK(volume->get_base() == field->get_mesh_rid());
	CHECK(volume->get_base().is_valid());

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] Collision geometry reaches the physics server") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	volume->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	Ref<VoxelShape3D> shape = volume->get_collision_shape();
	REQUIRE(shape.is_valid());

	const Vector<Vector3> faces = shape->get_faces();
	REQUIRE_MESSAGE(faces.size() >= 3, "A solid block produced no collision faces.");
	CHECK(faces.size() % 3 == 0);

	// Reading the data back out of the physics server is the difference between
	// "a shape resource exists" and "physics can actually collide with this". The
	// former is what a broken wiring still gives you.
	const Dictionary data = PhysicsServer3D::get_singleton()->shape_get_data(shape->get_rid());
	const PackedVector3Array physics_faces = data["faces"];
	CHECK(physics_faces.size() == faces.size());
	CHECK(data["backface_collision"] == Variant(false));

	// Every face has to be inside the field, or collision is somewhere the volume
	// is not.
	const AABB bounds = field->get_bounds().grow(field->get_voxel_size() * 2.0f);
	bool all_inside = true;
	for (const Vector3 &vertex : physics_faces) {
		if (!bounds.has_point(vertex)) {
			all_inside = false;
			break;
		}
	}
	CHECK_MESSAGE(all_inside, "Collision faces fall outside the field's own bounds.");

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] Collision follows the field as it is carved") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	volume->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	const int before = volume->get_collision_shape()->get_faces().size();
	REQUIRE(before > 0);

	// A hole through the middle of a block adds surface, so the face count has to
	// move. If it does not, collision is a snapshot from when the volume was built.
	field->stamp(make_sphere(0.1f), Transform3D(), VoxelField::OP_SUBTRACT);
	const int after = volume->get_collision_shape()->get_faces().size();
	CHECK_MESSAGE(after != before,
			vformat("Collision still has %d faces after a cut, so it is not tracking the field.", after));

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] Collision is coarser than what is drawn") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	volume->set_collision_voxel_multiplier(4);
	volume->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	const int collision_faces = volume->get_collision_shape()->get_faces().size() / 3;
	const int drawn_triangles = field->get_triangle_count();
	REQUIRE(collision_faces > 0);
	REQUIRE(drawn_triangles > 0);

	// The whole point of the multiplier is that physics is not paying for detail it
	// cannot use. At four voxels per collision cell this should be a large factor,
	// not a rounding difference.
	CHECK_MESSAGE(collision_faces * 4 < drawn_triangles,
			vformat("Collision has %d faces against %d drawn triangles, which is not coarser in any useful sense.",
					collision_faces, drawn_triangles));

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] Two volumes sharing a field keep their own collision resolution") {
	Ref<VoxelField> field = make_block();

	VoxelVolume3D *fine = memnew(VoxelVolume3D);
	fine->set_field(field);
	fine->set_collision_voxel_multiplier(1);
	fine->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(fine);

	VoxelVolume3D *coarse = memnew(VoxelVolume3D);
	coarse->set_field(field);
	coarse->set_collision_voxel_multiplier(8);
	coarse->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(coarse);

	// Whichever built last must not have decided for both. Reading the fine one
	// again afterwards is the check that matters: if the resolution lives on the
	// field rather than on the request, this comes back coarse.
	const int coarse_faces = coarse->get_collision_shape()->get_faces().size();
	const int fine_faces = fine->get_collision_shape()->get_faces().size();
	REQUIRE(coarse_faces > 0);
	REQUIRE(fine_faces > 0);
	CHECK_MESSAGE(coarse_faces < fine_faces,
			vformat("Both volumes ended up with %d and %d faces, so one imposed its resolution on the other.",
					coarse_faces, fine_faces));

	memdelete(coarse);
	memdelete(fine);
}

TEST_CASE("[SceneTree][VoxelVolume3D] Turning collision off releases it") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	volume->set_field(make_block());
	volume->set_generate_collision(true);
	SceneTree::get_singleton()->get_root()->add_child(volume);
	REQUIRE(volume->get_collision_shape().is_valid());

	volume->set_generate_collision(false);
	CHECK(volume->get_collision_shape().is_null());

	// And back on again, because a volume whose collision can only be enabled once
	// is worse than one that never had it.
	volume->set_generate_collision(true);
	CHECK(volume->get_collision_shape().is_valid());

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] A material set for a surface survives re-extraction") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	Ref<VoxelField> field = make_block();
	volume->set_field(field);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	Ref<StandardMaterial3D> material;
	material.instantiate();
	volume->set_surface_override_material(0, material);
	CHECK(volume->get_surface_override_material(0) == material);

	// Every stamp rebuilds the mesh from nothing in this phase. If the assignment
	// lived on the mesh rather than on the volume it would be gone by now, and a
	// carved object would go untextured the moment it was touched.
	field->stamp(make_sphere(0.08f), Transform3D(), VoxelField::OP_SUBTRACT);
	CHECK(volume->get_surface_override_material(0) == material);

	memdelete(volume);
}

TEST_CASE("[SceneTree][VoxelVolume3D] A volume without a field warns instead of drawing nothing quietly") {
	VoxelVolume3D *volume = memnew(VoxelVolume3D);
	SceneTree::get_singleton()->get_root()->add_child(volume);

	const int without_field = volume->get_configuration_warnings().size();
	REQUIRE(without_field > 0);

	// Counted rather than compared against empty, because the base class is entitled
	// to warn about things of its own that have nothing to do with the field.
	volume->set_field(make_block());
	CHECK(volume->get_configuration_warnings().size() == without_field - 1);

	memdelete(volume);
}

} // namespace TestVoxelVolume3D
