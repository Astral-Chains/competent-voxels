/**************************************************************************/
/*  voxel_shape_3d.cpp                                                    */
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

#include "voxel_shape_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/mesh.h"
#include "servers/physics_3d/physics_server_3d.h"

void VoxelShape3D::set_field(const Ref<VoxelField> &p_field) {
	if (field == p_field) {
		return;
	}
	if (field.is_valid()) {
		field->disconnect_changed(callable_mp(this, &VoxelShape3D::_field_changed));
	}
	field = p_field;
	if (field.is_valid()) {
		field->connect_changed(callable_mp(this, &VoxelShape3D::_field_changed));
	}
	_rebuild_faces();
}

Ref<VoxelField> VoxelShape3D::get_field() const {
	return field;
}

void VoxelShape3D::set_voxel_multiplier(int p_multiplier) {
	const int multiplier = MAX(p_multiplier, 1);
	if (voxel_multiplier == multiplier) {
		return;
	}
	voxel_multiplier = multiplier;
	_rebuild_faces();
}

int VoxelShape3D::get_voxel_multiplier() const {
	return voxel_multiplier;
}

Vector<Vector3> VoxelShape3D::get_faces() const {
	return faces;
}

void VoxelShape3D::_field_changed() {
	_rebuild_faces();
}

void VoxelShape3D::_rebuild_faces() {
	if (field.is_null()) {
		faces.clear();
	} else {
		// Asked for, not set. Writing the resolution into the field would make the
		// field announce a change, which arrives back here as another rebuild, and
		// would also mean two shapes sharing a field overwrite each other's choice.
		faces = field->get_collision_faces(voxel_multiplier);
	}
	_update_shape();
	emit_changed();
}

void VoxelShape3D::_update_shape() {
	Dictionary data;
	data["faces"] = faces;
	// The extracted surface is closed, so back faces are never the ones a body
	// should be stopped by.
	data["backface_collision"] = false;
	PhysicsServer3D::get_singleton()->shape_set_data(get_shape(), data);

	Shape3D::_update_shape();
}

Vector<Vector3> VoxelShape3D::get_debug_mesh_lines() const {
	Vector<Vector3> lines;
	const int count = faces.size();
	if (count < 3) {
		return lines;
	}

	// One line per triangle edge. Deduplicating shared edges would halve this, and
	// the debug view is not where that matters.
	lines.resize(count * 2);
	Vector3 *w = lines.ptrw();
	const Vector3 *r = faces.ptr();
	int cursor = 0;
	for (int i = 0; i + 2 < count; i += 3) {
		for (int e = 0; e < 3; e++) {
			w[cursor++] = r[i + e];
			w[cursor++] = r[i + (e + 1) % 3];
		}
	}
	lines.resize(cursor);
	return lines;
}

Ref<ArrayMesh> VoxelShape3D::get_debug_arraymesh_faces(const Color &p_modulate) const {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	if (faces.size() < 3) {
		return mesh;
	}

	Vector<Color> colors;
	colors.resize(faces.size());
	Color *w = colors.ptrw();
	for (int i = 0; i < faces.size(); i++) {
		w[i] = p_modulate;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = faces;
	arrays[Mesh::ARRAY_COLOR] = colors;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

real_t VoxelShape3D::get_enclosing_radius() const {
	const Vector3 *r = faces.ptr();
	real_t largest = 0.0;
	for (int i = 0; i < faces.size(); i++) {
		largest = MAX(largest, r[i].length_squared());
	}
	return Math::sqrt(largest);
}

void VoxelShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_field", "field"), &VoxelShape3D::set_field);
	ClassDB::bind_method(D_METHOD("get_field"), &VoxelShape3D::get_field);
	ClassDB::bind_method(D_METHOD("set_voxel_multiplier", "multiplier"), &VoxelShape3D::set_voxel_multiplier);
	ClassDB::bind_method(D_METHOD("get_voxel_multiplier"), &VoxelShape3D::get_voxel_multiplier);
	ClassDB::bind_method(D_METHOD("get_faces"), &VoxelShape3D::get_faces);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "field", PROPERTY_HINT_RESOURCE_TYPE, "VoxelField"), "set_field", "get_field");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "voxel_multiplier", PROPERTY_HINT_RANGE, "1,16,1"), "set_voxel_multiplier", "get_voxel_multiplier");
}

VoxelShape3D::VoxelShape3D() :
		Shape3D(PhysicsServer3D::get_singleton()->shape_create(PhysicsServer3D::SHAPE_CONCAVE_POLYGON)) {
}
