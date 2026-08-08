/**************************************************************************/
/*  voxel_mesher_marching_cubes.h                                         */
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

#include "voxel_mesher.h"

// Marching cubes on the CPU.
//
// This is the parity baseline, and it is kept permanently rather than replaced.
// Marching cubes places vertices only on cell edges by linear interpolation, so it
// can never represent a feature sharper than one voxel and will bevel every crease
// no matter the resolution. It is retained because it is simple enough to be
// obviously correct, which makes it the oracle to diff a sharper mesher against.
class VoxelMesherMarchingCubes : public VoxelMesher {
	GDCLASS(VoxelMesherMarchingCubes, VoxelMesher);

protected:
	static void _bind_methods();

public:
	virtual void build(const VoxelDenseStorage &p_storage, const Input &p_input, Output &r_output) const override;
	virtual String get_algorithm_name() const override;
};
