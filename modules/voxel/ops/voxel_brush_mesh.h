/**************************************************************************/
/*  voxel_brush_mesh.h                                                    */
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

#include "voxel_brush.h"

#include "scene/resources/mesh.h"

// A brush whose shape is baked from an arbitrary Mesh into a signed distance
// grid, sampled trilinearly.
//
// This is here before anything needs it, because mesh to SDF is the entry point for
// turning an imported mesh into a field later, and retrofitting it would mean
// reopening the brush interface. Any tool profile too irregular to describe with a
// sphere, capsule, or box also ends up being a mesh baked once and stamped many
// times, so it earns its place regardless.
class VoxelBrushMesh : public VoxelBrush {
	GDCLASS(VoxelBrushMesh, VoxelBrush);

	Ref<Mesh> mesh;
	int resolution = 32;
	float padding = 0.1f;

	// Baked lazily, because baking is expensive and a brush is usually configured
	// in several steps before it is ever sampled.
	mutable bool baked = false;
	mutable AABB baked_bounds;
	mutable float baked_cell_size = 0.0f;
	mutable Vector3i baked_resolution;
	mutable LocalVector<float> baked_sdf;

	void _invalidate();
	void _bake() const;
	// Sign comes from the generalized winding number rather than ray parity or an
	// outside flood fill, both of which leak through the holes that real meshes have.
	static float _winding_number(const Vector3 &p_point, const PackedVector3Array &p_triangles);

protected:
	static void _bind_methods();

public:
	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const;

	// Samples along the longest axis of the source mesh.
	void set_resolution(int p_resolution);
	int get_resolution() const;

	// Fraction of the mesh size added around it, so that the baked grid holds a
	// band of correct positive distance rather than ending at the surface.
	void set_padding(float p_padding);
	float get_padding() const;

	bool is_baked() const { return baked; }

	virtual float sample_local(const Vector3 &p_local) const override;
	virtual float get_extent() const override;
};
