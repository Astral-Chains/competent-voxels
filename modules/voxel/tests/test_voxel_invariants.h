/**************************************************************************/
/*  test_voxel_invariants.h                                               */
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
#include "../mesher/voxel_mesher_marching_cubes.h"
#include "../ops/voxel_brush.h"
#include "../ops/voxel_brush_mesh.h"
#include "../voxel_server.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "tests/test_macros.h"

// The seven invariants from the design document, as tests rather than as prose.
// Each one is a property that has to survive every later phase; a comment saying so
// would be forgotten by the time the storage layer is replaced, and a failing test
// will not be.
namespace TestVoxelInvariants {

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

// Invariant 1: nothing in this module knows what the game it was written for is
// about. misc/scripts/voxel_domain_neutrality.py checks the source text; this checks
// the API surface that actually ships, which is the part a user sees, and which a
// GDExtension or a binding generator can reach without touching the source at all.
// Keep the two word lists in step.
TEST_CASE("[Voxel][Invariant] No class or member names its application domain") {
	// Distinctive enough to match as substrings without catching innocent words.
	static const char *forbidden_substrings[] = {
		"wood", "bark", "chisel", "polish", "timber", "lumber", "lathe", "plank",
		"sawdust", "gouge", "whittle", "varnish", "workshop", "workbench"
	};
	// Short enough that they need whole-token matching, or "log" would flag "logic".
	static const char *forbidden_tokens[] = { "log", "grain", "sand", "knot" };

	auto offends = [](const String &p_identifier) {
		const String lower = p_identifier.to_lower();
		for (const char *word : forbidden_substrings) {
			if (lower.contains(word)) {
				return true;
			}
		}
		const Vector<String> tokens = lower.split("_", false);
		for (const String &token : tokens) {
			for (const char *word : forbidden_tokens) {
				if (token == word) {
					return true;
				}
			}
		}
		return false;
	};

	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes);
	int checked = 0;

	for (const StringName &class_name : classes) {
		if (!String(class_name).begins_with("Voxel")) {
			continue;
		}
		checked++;
		CHECK_MESSAGE(!offends(class_name), vformat("Class %s names its application domain.", class_name));

		List<MethodInfo> methods;
		ClassDB::get_method_list(class_name, &methods, true);
		for (const MethodInfo &method : methods) {
			CHECK_MESSAGE(!offends(method.name), vformat("%s::%s names its application domain.", class_name, method.name));
		}

		List<PropertyInfo> properties;
		ClassDB::get_property_list(class_name, &properties, true);
		for (const PropertyInfo &property : properties) {
			CHECK_MESSAGE(!offends(property.name), vformat("%s.%s names its application domain.", class_name, property.name));
		}
	}

	REQUIRE_MESSAGE(checked > 0, "No Voxel classes were registered, so this test proved nothing.");
}

// Invariant 2: a cut face can show a different material than the surface it was cut
// out of, without a second mesh, a second node, or a shader trick.
TEST_CASE("[Voxel][Invariant] Two material IDs produce two mesh surfaces") {
	Ref<VoxelField> field = make_field(0.01f);

	field->stamp(make_sphere(0.15f, 1), Transform3D(), VoxelField::OP_UNION);
	REQUIRE(field->get_surface_count() == 1);

	Transform3D offset;
	offset.origin = Vector3(0.12, 0, 0);
	field->stamp(make_sphere(0.06f, 2), offset, VoxelField::OP_UNION);

	CHECK_MESSAGE(field->get_surface_count() == 2,
			vformat("Expected one surface per material ID present, got %d.", field->get_surface_count()));
}

// Invariant 3: N fields, not one. This is the invariant most likely to be broken by
// a convenient global, so every fixture in this file creates more than one field and
// checks they cannot see each other.
TEST_CASE("[Voxel][Invariant] Fields are independent of one another") {
	VoxelServer *server = VoxelServer::get_singleton();
	const int before = server->get_field_count();

	Ref<VoxelField> a = make_field();
	Ref<VoxelField> b = make_field();
	Ref<VoxelField> c = make_field();
	REQUIRE(server->get_field_count() == before + 3);
	CHECK(a->get_rid() != b->get_rid());
	CHECK(b->get_rid() != c->get_rid());

	a->stamp(make_sphere(0.15f), Transform3D(), VoxelField::OP_UNION);
	b->set_voxel_size(0.05f);

	CHECK(a->get_volume() > 0.0f);
	CHECK(b->get_volume() == doctest::Approx(0.0f));
	CHECK(c->get_volume() == doctest::Approx(0.0f));
	CHECK(a->get_voxel_size() == doctest::Approx(0.02f));
	CHECK(b->get_voxel_size() == doctest::Approx(0.05f));

	b.unref();
	CHECK(server->get_field_count() == before + 2);
	CHECK(a->get_volume() > 0.0f);
}

// Invariant 4: coordinates are field-local. The field cannot see any world
// transform, so a rotated stamp of a rotationally symmetric brush has to produce
// exactly the same field as an unrotated one.
TEST_CASE("[Voxel][Invariant] Stamping is field-local and orientation independent") {
	Ref<VoxelField> plain = make_field(0.01f);
	Ref<VoxelField> rotated = make_field(0.01f);

	Transform3D identity;
	Transform3D awkward;
	awkward.basis = Basis(Vector3(0.37, 0.61, 0.7).normalized(), 0.9);

	plain->stamp(make_sphere(0.12f), identity, VoxelField::OP_UNION);
	rotated->stamp(make_sphere(0.12f), awkward, VoxelField::OP_UNION);

	CHECK(rotated->get_volume() == doctest::Approx(plain->get_volume()));
	CHECK(rotated->get_triangle_count() == plain->get_triangle_count());

	// And the same stamp twice must land in the same place, with nothing carried over
	// from whichever field happened to be stamped first.
	Ref<VoxelField> again = make_field(0.01f);
	again->stamp(make_sphere(0.12f), awkward, VoxelField::OP_UNION);
	CHECK(again->get_volume() == doctest::Approx(rotated->get_volume()));
}

// Invariant 5: islands come back as fields. A chip knocked off a workpiece and a
// crate split in half are then the same code path, and a detached piece can be cut
// again.
TEST_CASE("[Voxel][Invariant] Island extraction returns a field, not a mesh") {
	Ref<VoxelField> field = make_field(0.01f);

	Transform3D left;
	left.origin = Vector3(-0.12, 0, 0);
	Transform3D right;
	right.origin = Vector3(0.12, 0, 0);
	field->stamp(make_sphere(0.05f), left, VoxelField::OP_UNION);
	field->stamp(make_sphere(0.05f), right, VoxelField::OP_UNION);

	const TypedArray<Dictionary> islands = field->find_islands();
	REQUIRE_MESSAGE(islands.size() == 2, vformat("Two separated blobs should be two islands, found %d.", islands.size()));

	Ref<VoxelField> piece = field->extract_island(0);
	REQUIRE(piece.is_valid());
	CHECK(piece->get_rid().is_valid());
	CHECK(piece->get_rid() != field->get_rid());
	CHECK(piece->get_volume() > 0.0f);

	// The piece is a field, so it can be cut like any other.
	piece->stamp(make_sphere(0.02f), Transform3D(), VoxelField::OP_SUBTRACT);
	CHECK(piece->get_triangle_count() > 0);
}

// Invariant 6: the mesher is an interchangeable implementation behind one interface,
// not a hardcoded algorithm. Swapping the resource has to invalidate everything the
// old one produced.
TEST_CASE("[Voxel][Invariant] The mesher is swappable at runtime") {
	VoxelServer *server = VoxelServer::get_singleton();
	Ref<VoxelField> field = make_field(0.02f);
	field->stamp(make_sphere(0.12f), Transform3D(), VoxelField::OP_UNION);

	const int triangles = field->get_triangle_count();
	REQUIRE(triangles > 0);
	REQUIRE(server->get_monitor(VoxelServer::MONITOR_DIRTY_CHUNKS) == doctest::Approx(0.0));

	Ref<VoxelMesherMarchingCubes> replacement;
	replacement.instantiate();
	CHECK(replacement != field->get_mesher());
	field->set_mesher(replacement);

	// Everything the previous mesher produced is now an answer from a different
	// algorithm, so all of it has to be re-extracted rather than kept.
	CHECK(server->get_monitor(VoxelServer::MONITOR_DIRTY_CHUNKS) > 0.0);
	CHECK(field->get_mesher() == replacement);
	CHECK(field->get_triangle_count() == triangles);

	// A field without a mesher has no way to produce geometry, so refusing the
	// assignment is better than silently going blank.
	ERR_PRINT_OFF;
	field->set_mesher(Ref<VoxelMesher>());
	ERR_PRINT_ON;
	CHECK(field->get_mesher() == replacement);
}

// Invariant 7: VoxelBrushMesh exists from the start. Nothing in the first game needs
// it, and mesh to SDF is the entry point for turning arbitrary geometry into a field
// later, so retrofitting it would mean reopening the brush interface.
TEST_CASE("[Voxel][Invariant] An arbitrary mesh can be used as a brush") {
	Ref<BoxMesh> box;
	box.instantiate();
	box->set_size(Vector3(0.2, 0.2, 0.2));

	Ref<VoxelBrushMesh> brush;
	brush.instantiate();
	brush->set_mesh(box);
	brush->set_resolution(24);
	brush->set_material_id(3);
	CHECK_FALSE(brush->is_baked());

	Ref<VoxelField> field = make_field(0.01f);
	field->stamp(brush, Transform3D(), VoxelField::OP_UNION);
	CHECK(brush->is_baked());

	// A 0.2 m cube is 0.008 cubic meters. A voxelized approximation of it at 0.01 m
	// will not match exactly, and being in the right order of magnitude is the claim
	// being made here.
	const float volume = field->get_volume();
	const bool plausible = volume > 0.004f && volume < 0.014f;
	CHECK_MESSAGE(plausible,
			vformat("A baked 0.2 m cube stamped as a brush produced a volume of %f cubic meters.", volume));
	CHECK(field->sample(Vector3()) < 0.0f);
	CHECK(field->sample(Vector3(0.24, 0.24, 0.24)) > 0.0f);
}

} // namespace TestVoxelInvariants
