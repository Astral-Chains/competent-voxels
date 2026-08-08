/**************************************************************************/
/*  voxel_mesher.h                                                        */
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
#include "core/math/vector3i.h"
#include "core/templates/hash_map.h"

class VoxelDenseStorage;

// Turns a region of a field into triangles.
//
// This is a Resource rather than a compile-time choice so that swapping marching
// cubes for dual contouring later is an assignment rather than a rewrite, and so
// that a field can be re-extracted with a different algorithm at runtime to
// compare the two directly.
class VoxelMesher : public Resource {
	GDCLASS(VoxelMesher, Resource);

protected:
	static void _bind_methods();

public:
	// Triangles for a single material ID within a single region.
	struct Surface {
		Vector<Vector3> vertices;
		Vector<Vector3> normals;
		// Channels 1 through 4 packed as RGBA8, read by the shader as CUSTOM0.
		Vector<uint8_t> custom0;
		Vector<int32_t> indices;
	};

	struct Output {
		// Keyed by material ID, so grouping cells by material falls out of
		// extraction instead of needing a separate pass over the result.
		HashMap<uint32_t, Surface> surfaces;
		uint32_t triangle_count = 0;
	};

	struct Input {
		// Region to extract, in base cell coordinates.
		Vector3i cell_origin;
		Vector3i cell_size;
		// Sample stride. Greater than one extracts a coarser surface from the same
		// data, which is how collision runs at a lower resolution than rendering
		// without keeping a second copy of the field.
		int step = 1;
		// Collision extraction has no use for channel data and skips packing it.
		bool with_channels = true;
	};

	// Runs on worker threads for many regions concurrently, so it must read only
	// its arguments and must not mutate the storage it is given.
	virtual void build(const VoxelDenseStorage &p_storage, const Input &p_input, Output &r_output) const = 0;

	virtual String get_algorithm_name() const = 0;
};
