/**************************************************************************/
/*  channels.cpp                                                          */
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

#include "channels.h"

#include "core/math/math_funcs.h"

uint32_t VoxelChannels::get_format_byte_size(VoxelChannelFormat p_format) {
	switch (p_format) {
		case VOXEL_FORMAT_UINT8:
		case VOXEL_FORMAT_UNORM8:
			return 1;
		case VOXEL_FORMAT_UNORM16:
			return 2;
		case VOXEL_FORMAT_FLOAT32:
			return 4;
		default:
			return 1;
	}
}

uint32_t VoxelChannels::get_format_max_value(VoxelChannelFormat p_format) {
	switch (p_format) {
		case VOXEL_FORMAT_UINT8:
		case VOXEL_FORMAT_UNORM8:
			return 255;
		case VOXEL_FORMAT_UNORM16:
			return 65535;
		default:
			return 1;
	}
}

StringName VoxelChannels::get_material_id_name() {
	return SNAME("material_id");
}

void VoxelChannel::resize(uint64_t p_sample_count) {
	data.resize(p_sample_count * VoxelChannels::get_format_byte_size(format));
	fill_raw(0);
}

void VoxelChannel::set_raw(uint64_t p_index, uint32_t p_value) {
	switch (format) {
		case VOXEL_FORMAT_UINT8:
		case VOXEL_FORMAT_UNORM8: {
			data[p_index] = (uint8_t)MIN(p_value, 255u);
		} break;
		case VOXEL_FORMAT_UNORM16: {
			uint16_t v = (uint16_t)MIN(p_value, 65535u);
			memcpy(&data[p_index * 2], &v, 2);
		} break;
		case VOXEL_FORMAT_FLOAT32: {
			float v = (float)p_value;
			memcpy(&data[p_index * 4], &v, 4);
		} break;
		default:
			break;
	}
}

uint32_t VoxelChannel::get_raw(uint64_t p_index) const {
	switch (format) {
		case VOXEL_FORMAT_UINT8:
		case VOXEL_FORMAT_UNORM8:
			return data[p_index];
		case VOXEL_FORMAT_UNORM16: {
			uint16_t v = 0;
			memcpy(&v, &data[p_index * 2], 2);
			return v;
		}
		case VOXEL_FORMAT_FLOAT32: {
			float v = 0.0f;
			memcpy(&v, &data[p_index * 4], 4);
			return (uint32_t)MAX(v, 0.0f);
		}
		default:
			return 0;
	}
}

void VoxelChannel::set_normalized(uint64_t p_index, float p_value) {
	if (format == VOXEL_FORMAT_FLOAT32) {
		float v = p_value;
		memcpy(&data[p_index * 4], &v, 4);
		return;
	}
	const float max_value = (float)VoxelChannels::get_format_max_value(format);
	set_raw(p_index, (uint32_t)Math::round(CLAMP(p_value, 0.0f, 1.0f) * max_value));
}

float VoxelChannel::get_normalized(uint64_t p_index) const {
	if (format == VOXEL_FORMAT_FLOAT32) {
		float v = 0.0f;
		memcpy(&v, &data[p_index * 4], 4);
		return v;
	}
	return (float)get_raw(p_index) / (float)VoxelChannels::get_format_max_value(format);
}

void VoxelChannel::fill_raw(uint32_t p_value) {
	if (data.is_empty()) {
		return;
	}
	const uint32_t stride = VoxelChannels::get_format_byte_size(format);
	if (p_value == 0) {
		memset(data.ptr(), 0, data.size());
		return;
	}
	const uint64_t count = data.size() / stride;
	for (uint64_t i = 0; i < count; i++) {
		set_raw(i, p_value);
	}
}
