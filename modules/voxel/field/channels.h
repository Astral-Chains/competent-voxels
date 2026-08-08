/**************************************************************************/
/*  channels.h                                                            */
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

#include "../voxel_defs.h"

#include "core/string/string_name.h"
#include "core/templates/local_vector.h"

// One data channel, sampled on the same grid as the SDF so that a mesh vertex
// sitting on a grid edge can interpolate its value from the edge endpoints.
struct VoxelChannel {
	StringName name;
	VoxelChannelFormat format = VOXEL_FORMAT_UNORM8;
	LocalVector<uint8_t> data;

	void resize(uint64_t p_sample_count);

	void set_normalized(uint64_t p_index, float p_value);
	float get_normalized(uint64_t p_index) const;

	void set_raw(uint64_t p_index, uint32_t p_value);
	uint32_t get_raw(uint64_t p_index) const;

	void fill_raw(uint32_t p_value);
};

namespace VoxelChannels {

uint32_t get_format_byte_size(VoxelChannelFormat p_format);
uint32_t get_format_max_value(VoxelChannelFormat p_format);

// Channel 0 of every field is the material ID. Extraction groups cells by it and
// emits one mesh surface per distinct value present, which is what lets a cut
// face carry a different material from the surface it was cut out of.
constexpr uint32_t MATERIAL_ID_CHANNEL = 0;
constexpr uint32_t MATERIAL_ID_INTERIOR = 0;

StringName get_material_id_name();

// Channels 1 through 4 are packed into ARRAY_CUSTOM0 as RGBA8 for the shader.
constexpr uint32_t CUSTOM0_FIRST_CHANNEL = 1;
constexpr uint32_t CUSTOM0_CHANNEL_COUNT = 4;

} // namespace VoxelChannels
