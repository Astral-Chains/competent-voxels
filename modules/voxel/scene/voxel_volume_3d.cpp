/**************************************************************************/
/*  voxel_volume_3d.cpp                                                   */
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

#include "voxel_volume_3d.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"
#include "scene/resources/material.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/rendering_server.h"

void VoxelVolume3D::set_field(const Ref<VoxelField> &p_field) {
	if (field == p_field) {
		return;
	}
	if (field.is_valid()) {
		field->disconnect_changed(callable_mp(this, &VoxelVolume3D::_field_changed));
	}
	field = p_field;
	if (field.is_valid()) {
		field->connect_changed(callable_mp(this, &VoxelVolume3D::_field_changed));
	}

	if (collision_shape.is_valid()) {
		collision_shape->set_field(field);
	}

	_update_volume();
	update_configuration_warnings();
}

Ref<VoxelField> VoxelVolume3D::get_field() const {
	return field;
}

void VoxelVolume3D::_field_changed() {
	_queue_update();
}

void VoxelVolume3D::_queue_update() {
	if (update_queued) {
		return;
	}
	// A stroke emits one change per stamp, and re-extracting per stamp would pay the
	// full mesh rebuild dozens of times for one frame of visible result. Coalescing
	// to the end of the frame is the difference between a usable tool and a slideshow.
	update_queued = true;
	callable_mp(this, &VoxelVolume3D::_update_volume).call_deferred();
}

void VoxelVolume3D::_update_volume() {
	update_queued = false;

	if (field.is_null()) {
		set_base(RID());
		_free_collision();
		return;
	}

	// The field's mesh RID is stable for the field's lifetime; asking for it is what
	// drives extraction of whatever changed since last time.
	set_base(field->get_mesh_rid());
	_apply_surface_materials();
	update_gizmos();

	if (generate_collision) {
		_update_collision();
	}
}

void VoxelVolume3D::_apply_surface_materials() {
	if (field.is_null()) {
		return;
	}
	const int surfaces = field->get_surface_count();
	for (int i = 0; i < surfaces; i++) {
		const Ref<Material> material = i < surface_materials.size() ? surface_materials[i] : Ref<Material>();
		RenderingServer::get_singleton()->instance_set_surface_override_material(
				get_instance(), i, material.is_valid() ? material->get_rid() : RID());
	}
}

void VoxelVolume3D::set_surface_override_material(int p_surface, const Ref<Material> &p_material) {
	ERR_FAIL_COND(p_surface < 0);
	if (p_surface >= surface_materials.size()) {
		surface_materials.resize(p_surface + 1);
	}
	surface_materials.write[p_surface] = p_material;
	_apply_surface_materials();
}

Ref<Material> VoxelVolume3D::get_surface_override_material(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, surface_materials.size(), Ref<Material>());
	return surface_materials[p_surface];
}

int VoxelVolume3D::get_surface_override_material_count() const {
	return surface_materials.size();
}

void VoxelVolume3D::set_generate_collision(bool p_enabled) {
	if (generate_collision == p_enabled) {
		return;
	}
	generate_collision = p_enabled;
	if (generate_collision) {
		_update_collision();
	} else {
		_free_collision();
	}
	notify_property_list_changed();
}

bool VoxelVolume3D::is_generating_collision() const {
	return generate_collision;
}

void VoxelVolume3D::set_collision_voxel_multiplier(int p_multiplier) {
	const int multiplier = MAX(p_multiplier, 1);
	if (collision_voxel_multiplier == multiplier) {
		return;
	}
	collision_voxel_multiplier = multiplier;
	if (collision_shape.is_valid()) {
		collision_shape->set_voxel_multiplier(collision_voxel_multiplier);
	}
}

int VoxelVolume3D::get_collision_voxel_multiplier() const {
	return collision_voxel_multiplier;
}

void VoxelVolume3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (collision_body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_layer(collision_body, collision_layer);
	}
}

uint32_t VoxelVolume3D::get_collision_layer() const {
	return collision_layer;
}

void VoxelVolume3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (collision_body.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_mask(collision_body, collision_mask);
	}
}

uint32_t VoxelVolume3D::get_collision_mask() const {
	return collision_mask;
}

void VoxelVolume3D::set_lod_screen_error_pixels(float p_pixels) {
	lod_screen_error_pixels = MAX(p_pixels, 0.0f);
}

float VoxelVolume3D::get_lod_screen_error_pixels() const {
	return lod_screen_error_pixels;
}

Ref<VoxelShape3D> VoxelVolume3D::get_collision_shape() const {
	return collision_shape;
}

void VoxelVolume3D::_update_collision() {
	if (!is_inside_tree() || field.is_null()) {
		return;
	}

	if (collision_shape.is_null()) {
		collision_shape.instantiate();
		collision_shape->set_voxel_multiplier(collision_voxel_multiplier);
		collision_shape->set_field(field);
	}

	if (!collision_body.is_valid()) {
		PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
		collision_body = physics->body_create();
		physics->body_set_mode(collision_body, PhysicsServer3D::BODY_MODE_STATIC);
		physics->body_set_state(collision_body, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
		physics->body_set_space(collision_body, get_world_3d()->get_space());
		physics->body_set_collision_layer(collision_body, collision_layer);
		physics->body_set_collision_mask(collision_body, collision_mask);
		physics->body_add_shape(collision_body, collision_shape->get_rid());
		physics->body_attach_object_instance_id(collision_body, get_instance_id());
	}
}

void VoxelVolume3D::_free_collision() {
	if (collision_body.is_valid()) {
		PhysicsServer3D::get_singleton()->free_rid(collision_body);
		collision_body = RID();
	}
	collision_shape.unref();
}

AABB VoxelVolume3D::get_aabb() const {
	if (field.is_null()) {
		return AABB();
	}
	return field->get_bounds();
}

PackedStringArray VoxelVolume3D::get_configuration_warnings() const {
	PackedStringArray warnings = GeometryInstance3D::get_configuration_warnings();
	if (field.is_null()) {
		warnings.push_back(RTR("A VoxelVolume3D with no field has nothing to show. Assign a VoxelField to the \"field\" property."));
	}
	return warnings;
}

void VoxelVolume3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update_volume();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_free_collision();
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED: {
			if (collision_body.is_valid()) {
				PhysicsServer3D::get_singleton()->body_set_state(
						collision_body, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
			}
		} break;
	}
}

void VoxelVolume3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_field", "field"), &VoxelVolume3D::set_field);
	ClassDB::bind_method(D_METHOD("get_field"), &VoxelVolume3D::get_field);

	ClassDB::bind_method(D_METHOD("set_surface_override_material", "surface", "material"), &VoxelVolume3D::set_surface_override_material);
	ClassDB::bind_method(D_METHOD("get_surface_override_material", "surface"), &VoxelVolume3D::get_surface_override_material);
	ClassDB::bind_method(D_METHOD("get_surface_override_material_count"), &VoxelVolume3D::get_surface_override_material_count);

	ClassDB::bind_method(D_METHOD("set_generate_collision", "enabled"), &VoxelVolume3D::set_generate_collision);
	ClassDB::bind_method(D_METHOD("is_generating_collision"), &VoxelVolume3D::is_generating_collision);
	ClassDB::bind_method(D_METHOD("set_collision_voxel_multiplier", "multiplier"), &VoxelVolume3D::set_collision_voxel_multiplier);
	ClassDB::bind_method(D_METHOD("get_collision_voxel_multiplier"), &VoxelVolume3D::get_collision_voxel_multiplier);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &VoxelVolume3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &VoxelVolume3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &VoxelVolume3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &VoxelVolume3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_lod_screen_error_pixels", "pixels"), &VoxelVolume3D::set_lod_screen_error_pixels);
	ClassDB::bind_method(D_METHOD("get_lod_screen_error_pixels"), &VoxelVolume3D::get_lod_screen_error_pixels);
	ClassDB::bind_method(D_METHOD("get_collision_shape"), &VoxelVolume3D::get_collision_shape);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "field", PROPERTY_HINT_RESOURCE_TYPE, "VoxelField"), "set_field", "get_field");

	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_generate_collision", "is_generating_collision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_voxel_multiplier", PROPERTY_HINT_RANGE, "1,16,1"), "set_collision_voxel_multiplier", "get_collision_voxel_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	ADD_GROUP("Level of Detail", "lod_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_screen_error_pixels", PROPERTY_HINT_RANGE, "0,16,0.1,suffix:px"), "set_lod_screen_error_pixels", "get_lod_screen_error_pixels");
}

VoxelVolume3D::VoxelVolume3D() {
	collision_voxel_multiplier = GLOBAL_GET("voxel/collision/voxel_multiplier");
	lod_screen_error_pixels = GLOBAL_GET("voxel/lod/screen_error_pixels");
	set_notify_transform(true);
}

VoxelVolume3D::~VoxelVolume3D() {
	_free_collision();
}
