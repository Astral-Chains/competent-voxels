/**************************************************************************/
/*  voxel_field.h                                                         */
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

#include "../mesher/voxel_mesher.h"
#include "../voxel_defs.h"

#include "core/io/resource.h"

class VoxelBrush;

// A savable handle to voxel data, holding an RID into VoxelServer exactly as
// Texture2D holds an RID into RenderingServer.
//
// This is the layer nearly all code should use. It owns nothing but the RID, which
// means copying one is cheap, freeing one is deterministic, and the data underneath
// can move from a dense array to sparse bricks to the GPU without any of the
// scripts that touch this class noticing.
class VoxelField : public Resource {
	GDCLASS(VoxelField, Resource);

	RID field;

	// Mirrors of the module's canonical enums, so that scripts can write
	// VoxelField.OP_SUBTRACT rather than reaching for the server.
public:
	enum ChannelFormat {
		FORMAT_UINT8,
		FORMAT_UNORM8,
		FORMAT_UNORM16,
		FORMAT_FLOAT32,
	};

	enum Operation {
		OP_UNION,
		OP_SUBTRACT,
		OP_INTERSECT,
		OP_SMOOTH,
		OP_PAINT,
	};

protected:
	static void _bind_methods();

public:
	virtual RID get_rid() const override { return field; }

	// Wraps a field the server already created, used by extract_island so that an
	// island comes back as a real independent field rather than a one-off mesh.
	static Ref<VoxelField> create_from_rid(RID p_rid);

	void set_bounds(const AABB &p_bounds);
	AABB get_bounds() const;

	void set_voxel_size(float p_voxel_size);
	float get_voxel_size() const;

	void set_mesher(const Ref<VoxelMesher> &p_mesher);
	Ref<VoxelMesher> get_mesher() const;

	Vector3i get_cell_resolution() const;

	int add_channel(const StringName &p_name, ChannelFormat p_format);
	int find_channel(const StringName &p_name) const;
	bool has_channel(const StringName &p_name) const;
	int get_channel_count() const;

	void stamp(const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, Operation p_operation);
	// One call for a whole stroke, so that spacing, dwell, and dirty region union all
	// happen where the brush footprint is known.
	void stamp_path(const Ref<VoxelBrush> &p_brush, const Vector<Vector3> &p_path, Operation p_operation);
	void clear();

	float sample(const Vector3 &p_local_position) const;
	Dictionary raycast(const Vector3 &p_from, const Vector3 &p_direction, float p_max_distance) const;
	float get_volume() const;

	// The extracted mesh is a plain RenderingServer mesh, which is what lets
	// VoxelVolume3D be an ordinary GeometryInstance3D. Callers that want to look at
	// the geometry go through RenderingServer like they would for any other mesh;
	// these two are the convenience wrappers.
	RID get_mesh_rid() const;
	int get_surface_count() const;
	Array get_surface_arrays(int p_surface) const;
	int get_triangle_count() const;

	void set_collision_step(int p_step);
	int get_collision_step() const;
	// A step of -1 means the field's own. Passing one reads at that resolution
	// without changing the field, so a caller with its own idea of how coarse
	// collision should be does not impose it on everything else sharing the field.
	Vector<Vector3> get_collision_faces(int p_step = -1) const;

	TypedArray<Dictionary> find_islands();
	Ref<VoxelField> extract_island(int p_island_index, bool p_remove_from_source = false);

	VoxelField();
	~VoxelField();
};

VARIANT_ENUM_CAST(VoxelField::ChannelFormat);
VARIANT_ENUM_CAST(VoxelField::Operation);
