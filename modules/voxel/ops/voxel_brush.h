/**************************************************************************/
/*  voxel_brush.h                                                         */
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

#include "core/io/resource.h"
#include "scene/resources/curve.h"

// A shape that can be composited into a field, mirroring the Shape3D hierarchy so
// that it reads as familiar on sight.
//
// Subclasses only have to answer two questions: what is the signed distance to me
// at this point in my own local space, and how far from my origin can I possibly
// matter. Everything else -- spacing along a stroke, dirty region bookkeeping,
// falloff, channel writes -- is handled once, by the server.
class VoxelBrush : public Resource {
	GDCLASS(VoxelBrush, Resource);

	float strength = 1.0f;
	Ref<Curve> falloff;
	int material_id = 1;

protected:
	static void _bind_methods();

public:
	void set_strength(float p_strength);
	float get_strength() const;

	void set_falloff(const Ref<Curve> &p_falloff);
	Ref<Curve> get_falloff() const;

	// Written into the material ID channel wherever this brush adds material, so
	// that a later cut can expose a different material than the outer surface.
	void set_material_id(int p_material_id);
	int get_material_id() const;

	// Signed distance in brush-local space. Negative inside.
	virtual float sample_local(const Vector3 &p_local) const;

	// Distance from the brush origin beyond which sample_local is guaranteed
	// positive. Used to bound the stamped region and to space a stroke.
	virtual float get_extent() const;

	// Evaluated along a stroke: how far apart consecutive stamps may sit before
	// the swept shape starts to scallop.
	virtual float get_stroke_spacing() const;

	// Maps the raw distance through strength and the falloff curve. A falloff of
	// 1 at the center and 0 at the rim pulls the effective surface inward at the
	// edges, which is what makes a soft-edged tool feel soft.
	float sample_weighted(const Vector3 &p_local) const;
};

class VoxelBrushSphere : public VoxelBrush {
	GDCLASS(VoxelBrushSphere, VoxelBrush);

	float radius = 0.05f;

protected:
	static void _bind_methods();

public:
	void set_radius(float p_radius);
	float get_radius() const;

	virtual float sample_local(const Vector3 &p_local) const override;
	virtual float get_extent() const override;
};

class VoxelBrushCapsule : public VoxelBrush {
	GDCLASS(VoxelBrushCapsule, VoxelBrush);

	float radius = 0.05f;
	float height = 0.2f;

protected:
	static void _bind_methods();

public:
	void set_radius(float p_radius);
	float get_radius() const;

	// Total height including both caps, matching CapsuleShape3D.
	void set_height(float p_height);
	float get_height() const;

	virtual float sample_local(const Vector3 &p_local) const override;
	virtual float get_extent() const override;
};

class VoxelBrushBox : public VoxelBrush {
	GDCLASS(VoxelBrushBox, VoxelBrush);

	Vector3 size = Vector3(0.1, 0.1, 0.1);
	float corner_radius = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_corner_radius(float p_corner_radius);
	float get_corner_radius() const;

	virtual float sample_local(const Vector3 &p_local) const override;
	virtual float get_extent() const override;
};
