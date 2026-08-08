/**************************************************************************/
/*  test_voxel_server.h                                                   */
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

#include "../voxel_server.h"

#include "core/config/engine.h"
#include "tests/test_macros.h"

namespace TestVoxelServer {

TEST_CASE("[Voxel] VoxelServer singleton is registered") {
	VoxelServer *server = VoxelServer::get_singleton();
	REQUIRE(server != nullptr);
	CHECK(Engine::get_singleton()->has_singleton("VoxelServer"));
	CHECK(Engine::get_singleton()->get_singleton_object("VoxelServer") == server);
}

TEST_CASE("[Voxel] VoxelServer owns no fields when idle") {
	VoxelServer *server = VoxelServer::get_singleton();
	REQUIRE(server != nullptr);
	CHECK(server->get_field_count() == 0);
}

} // namespace TestVoxelServer
