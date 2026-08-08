/**************************************************************************/
/*  voxel_shape_3d.h                                                      */
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

#include "scene/resources/3d/shape_3d.h"

// Collision for a voxel field, as a first-class Shape3D.
//
// Being a Shape3D rather than a helper that spits out a ConcavePolygonShape3D is
// what makes this usable from any CollisionObject3D, editable in the inspector, and
// visible in the collision debug view -- the same reasons GridMap's shapes are
// shapes. Underneath it is a concave polygon shape whose faces are re-extracted
// from the field at a coarser resolution than the rendered surface.
class VoxelShape3D : public Shape3D {
	GDCLASS(VoxelShape3D, Shape3D);

	Ref<VoxelField> field;
	int voxel_multiplier = 4;
	Vector<Vector3> faces;

	void _field_changed();
	void _rebuild_faces();

protected:
	static void _bind_methods();
	virtual void _update_shape() override;

public:
	void set_field(const Ref<VoxelField> &p_field);
	Ref<VoxelField> get_field() const;

	// Cell size multiplier for the collision surface. Nothing in a game can feel the
	// difference between colliding at four voxels and at one, and cooking a concave
	// shape at the finer resolution costs many times more.
	void set_voxel_multiplier(int p_multiplier);
	int get_voxel_multiplier() const;

	Vector<Vector3> get_faces() const;

	virtual Vector<Vector3> get_debug_mesh_lines() const override;
	virtual Ref<ArrayMesh> get_debug_arraymesh_faces(const Color &p_modulate) const override;
	virtual real_t get_enclosing_radius() const override;

	VoxelShape3D();
};
