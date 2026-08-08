/**************************************************************************/
/*  voxel_field.cpp                                                       */
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

#include "voxel_field.h"

#include "../ops/voxel_brush.h"
#include "../voxel_server.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

// The scripting mirrors must not drift from the canonical enums.
static_assert((int)VoxelField::FORMAT_UINT8 == (int)VOXEL_FORMAT_UINT8);
static_assert((int)VoxelField::FORMAT_UNORM8 == (int)VOXEL_FORMAT_UNORM8);
static_assert((int)VoxelField::FORMAT_UNORM16 == (int)VOXEL_FORMAT_UNORM16);
static_assert((int)VoxelField::FORMAT_FLOAT32 == (int)VOXEL_FORMAT_FLOAT32);
static_assert((int)VoxelField::OP_UNION == (int)VOXEL_OP_UNION);
static_assert((int)VoxelField::OP_SUBTRACT == (int)VOXEL_OP_SUBTRACT);
static_assert((int)VoxelField::OP_INTERSECT == (int)VOXEL_OP_INTERSECT);
static_assert((int)VoxelField::OP_SMOOTH == (int)VOXEL_OP_SMOOTH);
static_assert((int)VoxelField::OP_PAINT == (int)VOXEL_OP_PAINT);

Ref<VoxelField> VoxelField::create_from_rid(RID p_rid) {
	Ref<VoxelField> result;
	result.instantiate();
	if (result->field.is_valid()) {
		VoxelServer::get_singleton()->field_free(result->field);
	}
	result->field = p_rid;
	return result;
}

void VoxelField::set_bounds(const AABB &p_bounds) {
	VoxelServer::get_singleton()->field_set_bounds(field, p_bounds);
	emit_signal(SNAME("bounds_changed"));
	emit_changed();
}

AABB VoxelField::get_bounds() const {
	return VoxelServer::get_singleton()->field_get_bounds(field);
}

void VoxelField::set_voxel_size(float p_voxel_size) {
	VoxelServer::get_singleton()->field_set_voxel_size(field, p_voxel_size);
	emit_changed();
}

float VoxelField::get_voxel_size() const {
	return VoxelServer::get_singleton()->field_get_voxel_size(field);
}

void VoxelField::set_mesher(const Ref<VoxelMesher> &p_mesher) {
	VoxelServer::get_singleton()->field_set_mesher(field, p_mesher);
	emit_changed();
}

Ref<VoxelMesher> VoxelField::get_mesher() const {
	return VoxelServer::get_singleton()->field_get_mesher(field);
}

Vector3i VoxelField::get_cell_resolution() const {
	return VoxelServer::get_singleton()->field_get_cell_resolution(field);
}

int VoxelField::add_channel(const StringName &p_name, ChannelFormat p_format) {
	const int index = VoxelServer::get_singleton()->field_add_channel(field, p_name, (VoxelServer::ChannelFormat)p_format);
	emit_changed();
	return index;
}

int VoxelField::find_channel(const StringName &p_name) const {
	return VoxelServer::get_singleton()->field_find_channel(field, p_name);
}

bool VoxelField::has_channel(const StringName &p_name) const {
	return find_channel(p_name) >= 0;
}

int VoxelField::get_channel_count() const {
	return VoxelServer::get_singleton()->field_get_channel_count(field);
}

void VoxelField::stamp(const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, Operation p_operation) {
	VoxelServer::get_singleton()->field_stamp(field, p_brush, p_transform, (VoxelServer::Operation)p_operation);
	emit_changed();
}

void VoxelField::stamp_path(const Ref<VoxelBrush> &p_brush, const Vector<Vector3> &p_path, Operation p_operation) {
	VoxelServer::get_singleton()->field_stamp_path(field, p_brush, p_path, (VoxelServer::Operation)p_operation);
	emit_changed();
}

void VoxelField::clear() {
	VoxelServer::get_singleton()->field_clear(field);
	emit_changed();
}

float VoxelField::sample(const Vector3 &p_local_position) const {
	return VoxelServer::get_singleton()->field_sample(field, p_local_position);
}

Dictionary VoxelField::raycast(const Vector3 &p_from, const Vector3 &p_direction, float p_max_distance) const {
	return VoxelServer::get_singleton()->field_raycast(field, p_from, p_direction, p_max_distance);
}

float VoxelField::get_volume() const {
	return VoxelServer::get_singleton()->field_get_volume(field);
}

RID VoxelField::get_mesh_rid() const {
	return VoxelServer::get_singleton()->field_get_mesh(field);
}

int VoxelField::get_surface_count() const {
	const RID mesh = get_mesh_rid();
	if (!mesh.is_valid()) {
		return 0;
	}
	return RenderingServer::get_singleton()->mesh_get_surface_count(mesh);
}

Array VoxelField::get_surface_arrays(int p_surface) const {
	const RID mesh = get_mesh_rid();
	ERR_FAIL_COND_V(!mesh.is_valid(), Array());
	ERR_FAIL_INDEX_V(p_surface, RenderingServer::get_singleton()->mesh_get_surface_count(mesh), Array());
	return RenderingServer::get_singleton()->mesh_surface_get_arrays(mesh, p_surface);
}

int VoxelField::get_triangle_count() const {
	return VoxelServer::get_singleton()->field_get_triangle_count(field);
}

void VoxelField::set_collision_step(int p_step) {
	VoxelServer *server = VoxelServer::get_singleton();
	const int before = server->field_get_collision_step(field);
	server->field_set_collision_step(field, p_step);
	// Only on a real change. Anything listening to a field rebuilds itself when told,
	// and a listener that reads the field back during that rebuild would otherwise
	// announce a change that never happened, forever.
	if (server->field_get_collision_step(field) != before) {
		emit_changed();
	}
}

int VoxelField::get_collision_step() const {
	return VoxelServer::get_singleton()->field_get_collision_step(field);
}

Vector<Vector3> VoxelField::get_collision_faces(int p_step) const {
	return VoxelServer::get_singleton()->field_get_collision_faces(field, p_step);
}

TypedArray<Dictionary> VoxelField::find_islands() {
	const TypedArray<Dictionary> islands = VoxelServer::get_singleton()->field_find_islands(field);
	// Synchronous on dense storage because there is no coarser level to descend
	// from. The signal exists now so that making it asynchronous later is not an
	// API change for anyone listening.
	emit_signal(SNAME("islands_found"), islands);
	return islands;
}

Ref<VoxelField> VoxelField::extract_island(int p_island_index, bool p_remove_from_source) {
	const RID island = VoxelServer::get_singleton()->field_extract_island(field, p_island_index, p_remove_from_source);
	ERR_FAIL_COND_V(!island.is_valid(), Ref<VoxelField>());
	if (p_remove_from_source) {
		emit_changed();
	}
	return create_from_rid(island);
}

void VoxelField::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bounds", "bounds"), &VoxelField::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &VoxelField::get_bounds);
	ClassDB::bind_method(D_METHOD("set_voxel_size", "voxel_size"), &VoxelField::set_voxel_size);
	ClassDB::bind_method(D_METHOD("get_voxel_size"), &VoxelField::get_voxel_size);
	ClassDB::bind_method(D_METHOD("set_mesher", "mesher"), &VoxelField::set_mesher);
	ClassDB::bind_method(D_METHOD("get_mesher"), &VoxelField::get_mesher);
	ClassDB::bind_method(D_METHOD("get_cell_resolution"), &VoxelField::get_cell_resolution);

	ClassDB::bind_method(D_METHOD("add_channel", "name", "format"), &VoxelField::add_channel);
	ClassDB::bind_method(D_METHOD("find_channel", "name"), &VoxelField::find_channel);
	ClassDB::bind_method(D_METHOD("has_channel", "name"), &VoxelField::has_channel);
	ClassDB::bind_method(D_METHOD("get_channel_count"), &VoxelField::get_channel_count);

	ClassDB::bind_method(D_METHOD("stamp", "brush", "transform", "operation"), &VoxelField::stamp);
	ClassDB::bind_method(D_METHOD("stamp_path", "brush", "path", "operation"), &VoxelField::stamp_path);
	ClassDB::bind_method(D_METHOD("clear"), &VoxelField::clear);

	ClassDB::bind_method(D_METHOD("sample", "local_position"), &VoxelField::sample);
	ClassDB::bind_method(D_METHOD("raycast", "from", "direction", "max_distance"), &VoxelField::raycast);
	ClassDB::bind_method(D_METHOD("get_volume"), &VoxelField::get_volume);

	ClassDB::bind_method(D_METHOD("get_mesh_rid"), &VoxelField::get_mesh_rid);
	ClassDB::bind_method(D_METHOD("get_surface_count"), &VoxelField::get_surface_count);
	ClassDB::bind_method(D_METHOD("get_surface_arrays", "surface"), &VoxelField::get_surface_arrays);
	ClassDB::bind_method(D_METHOD("get_triangle_count"), &VoxelField::get_triangle_count);

	ClassDB::bind_method(D_METHOD("set_collision_step", "step"), &VoxelField::set_collision_step);
	ClassDB::bind_method(D_METHOD("get_collision_step"), &VoxelField::get_collision_step);
	ClassDB::bind_method(D_METHOD("get_collision_faces", "step"), &VoxelField::get_collision_faces, DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("find_islands"), &VoxelField::find_islands);
	ClassDB::bind_method(D_METHOD("extract_island", "island_index", "remove_from_source"), &VoxelField::extract_island, DEFVAL(false));

	ADD_PROPERTY(PropertyInfo(Variant::AABB, "bounds", PROPERTY_HINT_NONE, "suffix:m"), "set_bounds", "get_bounds");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size", PROPERTY_HINT_RANGE, "0.0001,1,0.0001,or_greater,suffix:m"), "set_voxel_size", "get_voxel_size");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesher", PROPERTY_HINT_RESOURCE_TYPE, "VoxelMesher"), "set_mesher", "get_mesher");

	ADD_SIGNAL(MethodInfo("bounds_changed"));
	ADD_SIGNAL(MethodInfo("islands_found", PropertyInfo(Variant::ARRAY, "islands")));

	BIND_ENUM_CONSTANT(FORMAT_UINT8);
	BIND_ENUM_CONSTANT(FORMAT_UNORM8);
	BIND_ENUM_CONSTANT(FORMAT_UNORM16);
	BIND_ENUM_CONSTANT(FORMAT_FLOAT32);

	BIND_ENUM_CONSTANT(OP_UNION);
	BIND_ENUM_CONSTANT(OP_SUBTRACT);
	BIND_ENUM_CONSTANT(OP_INTERSECT);
	BIND_ENUM_CONSTANT(OP_SMOOTH);
	BIND_ENUM_CONSTANT(OP_PAINT);
}

VoxelField::VoxelField() {
	if (VoxelServer::get_singleton()) {
		field = VoxelServer::get_singleton()->field_create();
	}
}

VoxelField::~VoxelField() {
	// The server outlives every field in normal operation, but not during engine
	// shutdown, so this cannot assume it is still there.
	if (field.is_valid() && VoxelServer::get_singleton()) {
		VoxelServer::get_singleton()->field_free(field);
	}
}
