/**************************************************************************/
/*  voxel_volume_3d.h                                                     */
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
#include "voxel_shape_3d.h"

#include "scene/3d/visual_instance_3d.h"

// A voxel field in the scene tree.
//
// This is a GeometryInstance3D whose base is the field's extracted mesh, which means
// every existing Godot facility -- layers, LOD margins, material override, shadow
// casting, the transform gizmo, visibility ranges -- works without this class
// knowing about any of them. It generates its own static body the way CSGShape3D
// does, rather than expecting a StaticBody3D child, so that dropping one into a
// scene needs no assembly.
class VoxelVolume3D : public GeometryInstance3D {
	GDCLASS(VoxelVolume3D, GeometryInstance3D);

	Ref<VoxelField> field;
	Vector<Ref<Material>> surface_materials;

	bool generate_collision = false;
	int collision_voxel_multiplier = 4;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	// Declared now, inert until screen space error LOD selection exists. Keeping the
	// property here from the start means the scenes written against Phase 1 do not
	// need editing when it starts doing something.
	float lod_screen_error_pixels = 2.0f;

	Ref<VoxelShape3D> collision_shape;
	RID collision_body;

	bool update_queued = false;

	void _field_changed();
	void _queue_update();
	void _update_volume();
	void _apply_surface_materials();
	void _update_collision();
	void _free_collision();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_field(const Ref<VoxelField> &p_field);
	Ref<VoxelField> get_field() const;

	// Mirrors MeshInstance3D, so that assigning one material per material ID reads
	// the same as assigning one per mesh surface.
	void set_surface_override_material(int p_surface, const Ref<Material> &p_material);
	Ref<Material> get_surface_override_material(int p_surface) const;
	int get_surface_override_material_count() const;

	void set_generate_collision(bool p_enabled);
	bool is_generating_collision() const;

	void set_collision_voxel_multiplier(int p_multiplier);
	int get_collision_voxel_multiplier() const;

	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	void set_lod_screen_error_pixels(float p_pixels);
	float get_lod_screen_error_pixels() const;

	Ref<VoxelShape3D> get_collision_shape() const;

	virtual AABB get_aabb() const override;
	virtual PackedStringArray get_configuration_warnings() const override;

	VoxelVolume3D();
	~VoxelVolume3D();
};
