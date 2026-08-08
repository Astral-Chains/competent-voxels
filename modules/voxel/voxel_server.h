/**************************************************************************/
/*  voxel_server.h                                                        */
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

#include "field/dense_storage.h"
#include "mesher/voxel_mesher.h"
#include "voxel_defs.h"

#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/rid_owner.h"
#include "scene/resources/mesh.h"

class VoxelBrush;

// Owner of all voxel data, behind RIDs, in the same way RenderingServer owns
// meshes and textures.
//
// Nothing above this layer holds voxel data; VoxelField holds an RID into here and
// VoxelVolume3D holds a VoxelField. Keeping ownership in one place is what makes
// thread safety a property of a single file rather than a convention, and it is
// what lets the storage underneath be replaced without touching the scene layer.
class VoxelServer : public Object {
	GDCLASS(VoxelServer, Object);

	static VoxelServer *singleton;

public:
	// Mirrors of the module's canonical enums for the scripting API. The
	// static_asserts in voxel_server.cpp keep them honest.
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

	enum Monitor {
		MONITOR_FIELD_COUNT,
		MONITOR_DIRTY_CHUNKS,
		MONITOR_CPU_BYTES,
		MONITOR_STAMP_USEC,
		MONITOR_EXTRACT_USEC,
		MONITOR_TRIANGLE_COUNT,
		MONITOR_MAX,
	};

private:
	struct Field {
		VoxelDenseStorage storage;
		Ref<VoxelMesher> mesher;
		Ref<ArrayMesh> mesh;

		// One extraction result per chunk, retained so that a stamp only re-extracts
		// the chunks it actually touched. The mesh is still rebuilt from all of them,
		// which is the cost that GPU extraction exists to remove.
		LocalVector<VoxelMesher::Output> chunk_output;

		AABB pending_bounds = AABB(Vector3(), Vector3(1, 1, 1));
		float pending_voxel_size = 0.01f;
		bool needs_configure = true;
		bool mesh_dirty = true;
		uint32_t triangle_count = 0;

		// The default resolution collision is extracted at when a caller does not ask
		// for one. collision_faces_step records what the cache was actually built at,
		// so a caller asking for a different resolution gets that resolution instead
		// of silently getting whatever the last caller wanted.
		int collision_step = 4;
		int collision_faces_step = 0;
		bool collision_dirty = true;
		Vector<Vector3> collision_faces;
	};

	// Handed to the worker pool so that each task knows which chunk it owns. Tasks
	// read the storage and write only their own slot of chunk_output, so no task
	// ever needs to take the server lock.
	struct ExtractContext {
		Field *field = nullptr;
		const LocalVector<uint32_t> *chunks = nullptr;
	};

	mutable Mutex mutex;
	mutable RID_Owner<Field, true> field_owner;

	// Last measured values, published to the debugger's Monitors tab. Timings are
	// per operation rather than cumulative, because the question being asked is
	// always "is a stroke still cheap", never "how long has the process run".
	double last_stamp_usec = 0.0;
	double last_extract_usec = 0.0;

	bool monitors_registered = false;

	// Const because it mutates the field it is handed, never the server. Every
	// read path can therefore fault in the storage on first use.
	Error _configure_if_needed(Field *p_field) const;
	void _stamp_once(Field *p_field, const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, VoxelOperation p_operation);
	void _remesh(Field *p_field);
	void _rebuild_mesh(Field *p_field);
	void _rebuild_collision(Field *p_field, int p_step);

	static void _extract_chunk_task(void *p_userdata, uint32_t p_index);

	double _get_monitor_value(Monitor p_monitor) const;

protected:
	static void _bind_methods();

public:
	static VoxelServer *get_singleton();

	void register_monitors();
	void unregister_monitors();

	int get_field_count() const;

	RID field_create();
	void field_free(RID p_field);
	bool field_is_valid(RID p_field) const;

	void field_set_bounds(RID p_field, const AABB &p_bounds);
	AABB field_get_bounds(RID p_field) const;
	void field_set_voxel_size(RID p_field, float p_voxel_size);
	float field_get_voxel_size(RID p_field) const;
	Vector3i field_get_cell_resolution(RID p_field) const;

	int field_add_channel(RID p_field, const StringName &p_name, ChannelFormat p_format);
	int field_find_channel(RID p_field, const StringName &p_name) const;
	int field_get_channel_count(RID p_field) const;

	void field_set_mesher(RID p_field, const Ref<VoxelMesher> &p_mesher);
	Ref<VoxelMesher> field_get_mesher(RID p_field) const;

	void field_stamp(RID p_field, const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, Operation p_operation);
	// One call for a whole stroke. The engine knows the brush footprint, so it
	// computes the spacing itself rather than making every caller reinvent it.
	void field_stamp_path(RID p_field, const Ref<VoxelBrush> &p_brush, const Vector<Vector3> &p_path, Operation p_operation);
	void field_clear(RID p_field);

	float field_sample(RID p_field, const Vector3 &p_local_position) const;
	Dictionary field_raycast(RID p_field, const Vector3 &p_from, const Vector3 &p_direction, float p_max_distance) const;
	float field_get_volume(RID p_field) const;

	RID field_get_mesh(RID p_field);
	int field_get_triangle_count(RID p_field);

	void field_set_collision_step(RID p_field, int p_step);
	int field_get_collision_step(RID p_field) const;
	// A step of -1 means the field's own. Passing one explicitly reads at that
	// resolution without writing anything to the field, which is what lets two
	// collision shapes share a field without fighting over its settings.
	Vector<Vector3> field_get_collision_faces(RID p_field, int p_step = -1);

	// Connectivity. Islands come back as descriptors, and extraction produces a new
	// field RID rather than a mesh, so that a chip knocked off a workpiece and a
	// crate split in half are the same code path.
	TypedArray<Dictionary> field_find_islands(RID p_field);
	RID field_extract_island(RID p_field, int p_island_index, bool p_remove_from_source);

	double get_monitor(Monitor p_monitor) const;

	VoxelServer();
	~VoxelServer();
};

VARIANT_ENUM_CAST(VoxelServer::ChannelFormat);
VARIANT_ENUM_CAST(VoxelServer::Operation);
VARIANT_ENUM_CAST(VoxelServer::Monitor);
