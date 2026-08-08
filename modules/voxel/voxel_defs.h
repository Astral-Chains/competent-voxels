/**************************************************************************/
/*  voxel_defs.h                                                          */
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

#include "core/typedefs.h"

// Canonical enums for the module. VoxelServer and VoxelField each mirror these
// for the scripting API and static_assert that the values still line up, which
// is the same arrangement RenderingServer and Mesh use for array formats.

// Storage format of a data channel. Every format is readable as a normalized
// float; VOXEL_FORMAT_UINT8 additionally keeps its raw integer meaning, which
// is what the reserved material ID channel needs.
enum VoxelChannelFormat {
	VOXEL_FORMAT_UINT8,
	VOXEL_FORMAT_UNORM8,
	VOXEL_FORMAT_UNORM16,
	VOXEL_FORMAT_FLOAT32,
	VOXEL_FORMAT_MAX,
};

// How a brush composites into a field. The first three mirror
// CSGShape3D::Operation so the vocabulary is already familiar.
enum VoxelOperation {
	VOXEL_OP_UNION,
	VOXEL_OP_SUBTRACT,
	VOXEL_OP_INTERSECT,
	VOXEL_OP_SMOOTH,
	VOXEL_OP_PAINT,
	VOXEL_OP_MAX,
};
