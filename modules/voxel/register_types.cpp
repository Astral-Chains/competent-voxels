/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "field/voxel_field.h"
#include "mesher/voxel_mesher.h"
#include "mesher/voxel_mesher_marching_cubes.h"
#include "ops/voxel_brush.h"
#include "ops/voxel_brush_mesh.h"
#include "voxel_server.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/voxel_shape_3d.h"
#include "scene/voxel_volume_3d.h"

static VoxelServer *voxel_server = nullptr;

// Settings rather than constants, because every one of these is a tradeoff a
// project makes differently and none of them should require a recompile. They are
// registered before the server exists, since the server reads them when creating a
// field.
static void register_voxel_project_settings() {
	GLOBAL_DEF(PropertyInfo(Variant::INT, "voxel/threads/worker_count", PROPERTY_HINT_RANGE, "0,64,1",
					   PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_RESTART_IF_CHANGED),
			0);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "voxel/extraction/algorithm", PROPERTY_HINT_ENUM, "Marching Cubes"), 0);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "voxel/collision/voxel_multiplier", PROPERTY_HINT_RANGE, "1,16,1"), 4);

	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "voxel/lod/screen_error_pixels", PROPERTY_HINT_RANGE, "0,16,0.1,suffix:px"), 2.0);

	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "voxel/debug/draw_brick_bounds"), false);
}

void initialize_voxel_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		register_voxel_project_settings();

		GDREGISTER_ABSTRACT_CLASS(VoxelServer);
		voxel_server = memnew(VoxelServer);
		Engine::get_singleton()->add_singleton(Engine::Singleton("VoxelServer", VoxelServer::get_singleton()));
		voxel_server->register_monitors();
		return;
	}

	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VoxelField);

		GDREGISTER_ABSTRACT_CLASS(VoxelMesher);
		GDREGISTER_CLASS(VoxelMesherMarchingCubes);

		GDREGISTER_ABSTRACT_CLASS(VoxelBrush);
		GDREGISTER_CLASS(VoxelBrushSphere);
		GDREGISTER_CLASS(VoxelBrushCapsule);
		GDREGISTER_CLASS(VoxelBrushBox);
		GDREGISTER_CLASS(VoxelBrushMesh);

		GDREGISTER_CLASS(VoxelShape3D);
		GDREGISTER_CLASS(VoxelVolume3D);
		return;
	}
}

void uninitialize_voxel_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	if (voxel_server) {
		voxel_server->unregister_monitors();
		memdelete(voxel_server);
		voxel_server = nullptr;
	}
}
