/**************************************************************************/
/*  voxel_server.cpp                                                      */
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

#include "voxel_server.h"

#include "mesher/voxel_mesher_marching_cubes.h"
#include "ops/voxel_brush.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "main/performance.h"

// The scripting mirrors must not drift from the canonical enums.
static_assert((int)VoxelServer::FORMAT_UINT8 == (int)VOXEL_FORMAT_UINT8);
static_assert((int)VoxelServer::FORMAT_UNORM8 == (int)VOXEL_FORMAT_UNORM8);
static_assert((int)VoxelServer::FORMAT_UNORM16 == (int)VOXEL_FORMAT_UNORM16);
static_assert((int)VoxelServer::FORMAT_FLOAT32 == (int)VOXEL_FORMAT_FLOAT32);
static_assert((int)VoxelServer::OP_UNION == (int)VOXEL_OP_UNION);
static_assert((int)VoxelServer::OP_SUBTRACT == (int)VOXEL_OP_SUBTRACT);
static_assert((int)VoxelServer::OP_INTERSECT == (int)VOXEL_OP_INTERSECT);
static_assert((int)VoxelServer::OP_SMOOTH == (int)VOXEL_OP_SMOOTH);
static_assert((int)VoxelServer::OP_PAINT == (int)VOXEL_OP_PAINT);

VoxelServer *VoxelServer::singleton = nullptr;

VoxelServer *VoxelServer::get_singleton() {
	return singleton;
}

// Samples this far outside a brush still receive a correct positive distance, so
// that the field holds a usable band around every surface instead of a cliff
// between "just written" and "never touched".
static constexpr int STAMP_BAND_VOXELS = 4;

int VoxelServer::get_field_count() const {
	MutexLock lock(mutex);
	return field_owner.get_rid_count();
}

RID VoxelServer::field_create() {
	MutexLock lock(mutex);

	RID rid = field_owner.make_rid();
	Field *field = field_owner.get_or_null(rid);
	ERR_FAIL_NULL_V(field, RID());

	field->mesh.instantiate();
	field->collision_step = GLOBAL_GET("voxel/collision/voxel_multiplier");

	Ref<VoxelMesherMarchingCubes> mesher;
	mesher.instantiate();
	field->mesher = mesher;

	// Channel 0 is the material ID for every field, always. It is added before the
	// storage has a size, so it grows with the first configure.
	field->storage.add_channel(VoxelChannels::get_material_id_name(), VOXEL_FORMAT_UINT8);

	return rid;
}

void VoxelServer::field_free(RID p_field) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_MSG(field, "Attempted to free an invalid voxel field RID.");
	field_owner.free(p_field);
}

bool VoxelServer::field_is_valid(RID p_field) const {
	MutexLock lock(mutex);
	return field_owner.get_or_null(p_field) != nullptr;
}

void VoxelServer::field_set_bounds(RID p_field, const AABB &p_bounds) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	field->pending_bounds = p_bounds.abs();
	field->needs_configure = true;
}

AABB VoxelServer::field_get_bounds(RID p_field) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, AABB());
	return field->pending_bounds;
}

void VoxelServer::field_set_voxel_size(RID p_field, float p_voxel_size) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	ERR_FAIL_COND_MSG(p_voxel_size <= 0.0f, "Voxel size must be greater than zero.");
	field->pending_voxel_size = p_voxel_size;
	field->needs_configure = true;
}

float VoxelServer::field_get_voxel_size(RID p_field) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0.0f);
	return field->pending_voxel_size;
}

Vector3i VoxelServer::field_get_cell_resolution(RID p_field) const {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, Vector3i());
	_configure_if_needed(field);
	return field->storage.get_cell_resolution();
}

Error VoxelServer::_configure_if_needed(Field *p_field) const {
	if (!p_field->needs_configure) {
		return OK;
	}
	p_field->needs_configure = false;

	const Error error = p_field->storage.configure(p_field->pending_bounds, p_field->pending_voxel_size);
	if (error != OK) {
		return error;
	}

	p_field->chunk_output.clear();
	p_field->chunk_output.resize(p_field->storage.get_chunk_count());
	p_field->mesh_dirty = true;
	p_field->collision_dirty = true;
	return OK;
}

int VoxelServer::field_add_channel(RID p_field, const StringName &p_name, ChannelFormat p_format) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, -1);
	_configure_if_needed(field);
	return field->storage.add_channel(p_name, (VoxelChannelFormat)p_format);
}

int VoxelServer::field_find_channel(RID p_field, const StringName &p_name) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, -1);
	return field->storage.find_channel(p_name);
}

int VoxelServer::field_get_channel_count(RID p_field) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0);
	return field->storage.get_channel_count();
}

void VoxelServer::field_set_mesher(RID p_field, const Ref<VoxelMesher> &p_mesher) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	ERR_FAIL_COND_MSG(p_mesher.is_null(), "A field must always have a mesher.");
	field->mesher = p_mesher;
	// The retained per-chunk results came from the previous algorithm, so they are
	// no longer answers to the same question.
	field->storage.mark_all_dirty();
	field->mesh_dirty = true;
	field->collision_dirty = true;
}

Ref<VoxelMesher> VoxelServer::field_get_mesher(RID p_field) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, Ref<VoxelMesher>());
	return field->mesher;
}

void VoxelServer::field_clear(RID p_field) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	_configure_if_needed(field);

	field->storage.fill_sdf(field->storage.get_far_value());
	for (int i = 0; i < field->storage.get_channel_count(); i++) {
		field->storage.get_channel_mut(i).fill_raw(0);
	}
	field->storage.mark_all_dirty();
	field->mesh_dirty = true;
	field->collision_dirty = true;
}

void VoxelServer::_stamp_once(Field *p_field, const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, VoxelOperation p_operation) {
	VoxelDenseStorage &storage = p_field->storage;
	const float voxel_size = storage.get_voxel_size();
	const float extent = p_brush->get_extent();
	const float band = voxel_size * STAMP_BAND_VOXELS;

	// The brush footprint is a box in brush-local space; transforming it gives a
	// conservative field-local bound without asking the brush for anything but a
	// radius.
	const float reach = extent + band;
	AABB influence(Vector3(-reach, -reach, -reach), Vector3(reach, reach, reach) * 2.0f);
	influence = p_transform.xform(influence);

	const Vector3i sample_res = storage.get_sample_resolution();
	const AABB bounds = storage.get_bounds();
	const Vector3 lo = (influence.position - bounds.position) / voxel_size;
	const Vector3 hi = (influence.position + influence.size - bounds.position) / voxel_size;

	const int x0 = CLAMP((int)Math::floor(lo.x), 0, sample_res.x - 1);
	const int y0 = CLAMP((int)Math::floor(lo.y), 0, sample_res.y - 1);
	const int z0 = CLAMP((int)Math::floor(lo.z), 0, sample_res.z - 1);
	const int x1 = CLAMP((int)Math::ceil(hi.x), 0, sample_res.x - 1);
	const int y1 = CLAMP((int)Math::ceil(hi.y), 0, sample_res.y - 1);
	const int z1 = CLAMP((int)Math::ceil(hi.z), 0, sample_res.z - 1);
	if (x0 > x1 || y0 > y1 || z0 > z1) {
		return;
	}

	const Transform3D to_brush = p_transform.affine_inverse();
	float *sdf = storage.get_sdf_ptrw();
	VoxelChannel &material = storage.get_channel_mut((int)VoxelChannels::MATERIAL_ID_CHANNEL);
	const uint32_t brush_material = (uint32_t)p_brush->get_material_id();

	// Smoothing reads its neighbourhood, so it needs the values from before this
	// stamp rather than the ones it is producing.
	LocalVector<float> before;
	if (p_operation == VOXEL_OP_SMOOTH) {
		before.resize(storage.get_sample_count());
		memcpy(before.ptr(), sdf, (size_t)storage.get_sample_count() * sizeof(float));
	}

	for (int z = z0; z <= z1; z++) {
		for (int y = y0; y <= y1; y++) {
			for (int x = x0; x <= x1; x++) {
				const int64_t index = storage.sample_index(x, y, z);
				const Vector3 local = storage.sample_to_local(x, y, z);
				const float distance = p_brush->sample_weighted(to_brush.xform(local));

				switch (p_operation) {
					case VOXEL_OP_UNION: {
						const float old_sdf = sdf[index];
						if (distance < old_sdf) {
							sdf[index] = distance;
							material.set_raw((uint64_t)index, brush_material);
						} else if (distance <= 0.0f) {
							material.set_raw((uint64_t)index, brush_material);
						}
					} break;

					case VOXEL_OP_SUBTRACT: {
						if (-distance > sdf[index]) {
							sdf[index] = -distance;
						}
					} break;

					case VOXEL_OP_INTERSECT: {
						if (distance > sdf[index]) {
							sdf[index] = distance;
						}
					} break;

					case VOXEL_OP_SMOOTH: {
						if (distance > 0.0f) {
							break;
						}
						// Blurring the distance field removes high-frequency detail
						// without moving the surface as a whole.
						const int xa = MAX(x - 1, 0);
						const int xb = MIN(x + 1, sample_res.x - 1);
						const int ya = MAX(y - 1, 0);
						const int yb = MIN(y + 1, sample_res.y - 1);
						const int za = MAX(z - 1, 0);
						const int zb = MIN(z + 1, sample_res.z - 1);
						const float average = (before[storage.sample_index(xa, y, z)] +
													  before[storage.sample_index(xb, y, z)] +
													  before[storage.sample_index(x, ya, z)] +
													  before[storage.sample_index(x, yb, z)] +
													  before[storage.sample_index(x, y, za)] +
													  before[storage.sample_index(x, y, zb)]) /
								6.0f;
						const float weight = extent > 0.0f
								? CLAMP(-distance / extent, 0.0f, 1.0f) * p_brush->get_strength()
								: p_brush->get_strength();
						sdf[index] = Math::lerp(before[index], average, weight);
					} break;

					case VOXEL_OP_PAINT: {
						if (distance <= 0.0f) {
							material.set_raw((uint64_t)index, brush_material);
						}
					} break;

					default:
						break;
				}
			}
		}
	}

	storage.mark_region_dirty(influence);
	p_field->mesh_dirty = true;
	p_field->collision_dirty = true;
}

void VoxelServer::field_stamp(RID p_field, const Ref<VoxelBrush> &p_brush, const Transform3D &p_transform, Operation p_operation) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	ERR_FAIL_COND_MSG(p_brush.is_null(), "Cannot stamp with a null brush.");
	ERR_FAIL_COND(_configure_if_needed(field) != OK);

	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	_stamp_once(field, p_brush, p_transform, (VoxelOperation)p_operation);
	last_stamp_usec = (double)(OS::get_singleton()->get_ticks_usec() - started);
}

void VoxelServer::field_stamp_path(RID p_field, const Ref<VoxelBrush> &p_brush, const Vector<Vector3> &p_path, Operation p_operation) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	ERR_FAIL_COND_MSG(p_brush.is_null(), "Cannot stamp with a null brush.");
	ERR_FAIL_COND(_configure_if_needed(field) != OK);
	if (p_path.is_empty()) {
		return;
	}

	const uint64_t started = OS::get_singleton()->get_ticks_usec();

	// Spacing comes from the brush footprint rather than from the caller, which is
	// the whole reason this method exists: a stroke that scallops is a bug the
	// caller cannot see and the engine can always prevent.
	const float spacing = MAX(p_brush->get_stroke_spacing(), field->storage.get_voxel_size());
	const Vector3 *points = p_path.ptr();

	Transform3D transform;
	transform.origin = points[0];
	_stamp_once(field, p_brush, transform, (VoxelOperation)p_operation);

	for (int i = 1; i < p_path.size(); i++) {
		const Vector3 from = points[i - 1];
		const Vector3 to = points[i];
		const float length = from.distance_to(to);
		if (length <= CMP_EPSILON) {
			continue;
		}
		const int steps = MAX(1, (int)Math::ceil(length / spacing));
		for (int s = 1; s <= steps; s++) {
			transform.origin = from.lerp(to, (float)s / (float)steps);
			_stamp_once(field, p_brush, transform, (VoxelOperation)p_operation);
		}
	}

	last_stamp_usec = (double)(OS::get_singleton()->get_ticks_usec() - started);
}

float VoxelServer::field_sample(RID p_field, const Vector3 &p_local_position) const {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0.0f);
	_configure_if_needed(field);
	return field->storage.sample_local(p_local_position);
}

Dictionary VoxelServer::field_raycast(RID p_field, const Vector3 &p_from, const Vector3 &p_direction, float p_max_distance) const {
	MutexLock lock(mutex);
	Dictionary result;
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, result);
	_configure_if_needed(field);

	Vector3 hit;
	Vector3 normal;
	if (field->storage.raycast_local(p_from, p_direction, p_max_distance, hit, normal)) {
		result["position"] = hit;
		result["normal"] = normal;
	}
	return result;
}

float VoxelServer::field_get_volume(RID p_field) const {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0.0f);
	_configure_if_needed(field);

	const VoxelDenseStorage &storage = field->storage;
	const float *sdf = storage.get_sdf_ptr();
	const uint64_t count = storage.get_sample_count();
	uint64_t solid = 0;
	for (uint64_t i = 0; i < count; i++) {
		if (sdf[i] < 0.0f) {
			solid++;
		}
	}
	const float cell_volume = Math::pow(storage.get_voxel_size(), 3.0f);
	return (float)solid * cell_volume;
}

void VoxelServer::_extract_chunk_task(void *p_userdata, uint32_t p_index) {
	const ExtractContext *context = static_cast<const ExtractContext *>(p_userdata);
	const uint32_t chunk = (*context->chunks)[p_index];

	VoxelMesher::Output &output = context->field->chunk_output[chunk];
	output.surfaces.clear();
	output.triangle_count = 0;

	VoxelMesher::Input input;
	input.cell_origin = context->field->storage.chunk_to_cell_origin(chunk);
	input.cell_size = Vector3i(VoxelDenseStorage::CHUNK_SIZE, VoxelDenseStorage::CHUNK_SIZE, VoxelDenseStorage::CHUNK_SIZE);
	input.step = 1;
	input.with_channels = true;

	context->field->mesher->build(context->field->storage, input, output);
}

void VoxelServer::_remesh(Field *p_field) {
	const bool has_dirty = !p_field->storage.get_dirty_chunks().is_empty();
	if (!has_dirty && !p_field->mesh_dirty) {
		return;
	}

	// Timed across both halves. Meshing the dirty regions is the interesting half,
	// but concatenating every region's output and uploading it is the expensive one
	// in this phase, and a monitor that reported only the first would say extraction
	// is cheap while a caller watched it miss frames.
	const uint64_t started = OS::get_singleton()->get_ticks_usec();

	if (has_dirty) {
		ExtractContext context;
		context.field = p_field;
		context.chunks = &p_field->storage.get_dirty_chunks();

		// The pool can run unrelated tasks on this thread while we wait, and this thread
		// already holds the server lock. That is safe because the lock is recursive and
		// because extraction tasks never take it, so the group can always be finished by
		// whichever thread gets there first.
		const int elements = (int)context.chunks->size();
		const WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_native_group_task(
				&VoxelServer::_extract_chunk_task, &context, elements, -1, true, SNAME("Voxel Extraction"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);

		p_field->storage.clear_dirty();
	}

	_rebuild_mesh(p_field);

	last_extract_usec = (double)(OS::get_singleton()->get_ticks_usec() - started);
}

void VoxelServer::_rebuild_mesh(Field *p_field) {
	p_field->mesh_dirty = false;
	p_field->mesh->clear_surfaces();
	p_field->triangle_count = 0;

	// Weld every chunk into one index buffer by bit-identical position. Chunks already
	// agree on shared-edge positions; welding turns that into shared indices so the GPU
	// cannot open a rasterization crack into the hollow mesh interior. Vector3 is used
	// as the key (hash + is_same) so 96 bits of float identity are never collapsed into
	// a 64-bit fingerprint that could collide and corrupt seams.
	Vector<Vector3> vertices;
	Vector<Vector3> normals;
	Vector<Color> colors;
	Vector<uint8_t> custom0;
	Vector<int32_t> indices;
	HashMap<Vector3, int32_t> weld;

	for (uint32_t i = 0; i < p_field->chunk_output.size(); i++) {
		p_field->triangle_count += p_field->chunk_output[i].triangle_count;
		for (const KeyValue<uint32_t, VoxelMesher::Surface> &pair : p_field->chunk_output[i].surfaces) {
			const VoxelMesher::Surface &surface = pair.value;
			if (surface.indices.is_empty()) {
				continue;
			}

			const Vector3 *src_vertices = surface.vertices.ptr();
			const Vector3 *src_normals = surface.normals.ptr();
			const Color *src_colors = surface.colors.ptr();
			const uint8_t *src_custom0 = surface.custom0.ptr();
			const bool has_colors = surface.colors.size() == surface.vertices.size();
			const bool has_custom0 = surface.custom0.size() ==
					surface.vertices.size() * (int)VoxelChannels::CUSTOM0_CHANNEL_COUNT;

			LocalVector<int32_t> remap;
			remap.resize(surface.vertices.size());
			for (int v = 0; v < surface.vertices.size(); v++) {
				const Vector3 &key = src_vertices[v];
				if (const int32_t *dst = weld.getptr(key)) {
					remap[v] = *dst;
					if (has_colors && src_colors[v].r > colors[*dst].r) {
						colors.write[*dst] = src_colors[v];
					}
					continue;
				}
				const int32_t dst = vertices.size();
				vertices.push_back(key);
				normals.push_back(src_normals[v]);
				colors.push_back(has_colors ? src_colors[v] : Color(0, 0, 0, 1));
				if (has_custom0) {
					const int custom_base = v * (int)VoxelChannels::CUSTOM0_CHANNEL_COUNT;
					for (uint32_t ch = 0; ch < VoxelChannels::CUSTOM0_CHANNEL_COUNT; ch++) {
						custom0.push_back(src_custom0[custom_base + ch]);
					}
				}
				weld.insert(key, dst);
				remap[v] = dst;
			}

			const int32_t *src_indices = surface.indices.ptr();
			const int base = indices.size();
			indices.resize(base + surface.indices.size());
			int32_t *w = indices.ptrw();
			for (int k = 0; k < surface.indices.size(); k++) {
				w[base + k] = remap[src_indices[k]];
			}
		}
	}

	if (indices.is_empty()) {
		return;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_INDEX] = indices;

	uint64_t flags = 0;
	if (custom0.size() == vertices.size() * (int)VoxelChannels::CUSTOM0_CHANNEL_COUNT) {
		arrays[Mesh::ARRAY_CUSTOM0] = custom0;
		flags |= (uint64_t)Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
	}

	// Deliberately not passing ARRAY_FLAG_COMPRESS_ATTRIBUTES. Getting the
	// uncompressed layout visibly correct comes first; packing is a measured
	// optimization, not a default.
	p_field->mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);
}

void VoxelServer::_rebuild_collision(Field *p_field, int p_step) {
	p_field->collision_dirty = false;
	p_field->collision_faces_step = p_step;
	p_field->collision_faces.clear();

	if (p_field->mesher.is_null() || !p_field->storage.is_configured()) {
		return;
	}

	// Collision is extracted from the same data at a coarser step rather than from a
	// second copy of the field. Nothing in a game can feel the difference between
	// colliding at 2 mm and at 0.5 mm, and cooking a concave shape at the finer
	// resolution would be ruinous.
	VoxelMesher::Input input;
	input.cell_origin = Vector3i();
	input.cell_size = p_field->storage.get_cell_resolution();
	input.step = MAX(p_step, 1);
	input.with_channels = false;

	VoxelMesher::Output output;
	p_field->mesher->build(p_field->storage, input, output);

	int total = 0;
	for (const KeyValue<uint32_t, VoxelMesher::Surface> &pair : output.surfaces) {
		total += pair.value.indices.size();
	}
	p_field->collision_faces.resize(total);
	Vector3 *w = p_field->collision_faces.ptrw();

	int cursor = 0;
	for (const KeyValue<uint32_t, VoxelMesher::Surface> &pair : output.surfaces) {
		const Vector3 *vertices = pair.value.vertices.ptr();
		const int32_t *indices = pair.value.indices.ptr();
		for (int i = 0; i < pair.value.indices.size(); i++) {
			w[cursor++] = vertices[indices[i]];
		}
	}
}

RID VoxelServer::field_get_mesh(RID p_field) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, RID());
	if (_configure_if_needed(field) != OK) {
		return field->mesh->get_rid();
	}
	_remesh(field);
	return field->mesh->get_rid();
}

int VoxelServer::field_get_triangle_count(RID p_field) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0);
	ERR_FAIL_COND_V(_configure_if_needed(field) != OK, 0);
	_remesh(field);
	return (int)field->triangle_count;
}

void VoxelServer::field_set_collision_step(RID p_field, int p_step) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL(field);
	const int step = MAX(p_step, 1);
	if (field->collision_step == step) {
		return;
	}
	field->collision_step = step;
	field->collision_dirty = true;
}

int VoxelServer::field_get_collision_step(RID p_field) const {
	MutexLock lock(mutex);
	const Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, 0);
	return field->collision_step;
}

Vector<Vector3> VoxelServer::field_get_collision_faces(RID p_field, int p_step) {
	MutexLock lock(mutex);
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, Vector<Vector3>());
	ERR_FAIL_COND_V(_configure_if_needed(field) != OK, Vector<Vector3>());

	const int step = p_step > 0 ? p_step : MAX(field->collision_step, 1);
	// Only one resolution is cached, so alternating between two of them re-extracts
	// every time. That is the honest cost of letting a caller choose, and it beats
	// the alternative of one caller's choice silently becoming everyone's.
	if (field->collision_dirty || field->collision_faces_step != step) {
		_rebuild_collision(field, step);
	}
	return field->collision_faces;
}

TypedArray<Dictionary> VoxelServer::field_find_islands(RID p_field) {
	MutexLock lock(mutex);
	TypedArray<Dictionary> islands;
	Field *field = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(field, islands);
	ERR_FAIL_COND_V(_configure_if_needed(field) != OK, islands);

	const VoxelDenseStorage &storage = field->storage;
	const Vector3i res = storage.get_sample_resolution();
	const float *sdf = storage.get_sdf_ptr();
	const uint64_t count = storage.get_sample_count();

	// Flood fill over solid samples. This visits the whole grid, which is exactly
	// the cost the brick occupancy summaries exist to remove; on a dense grid there
	// is nothing coarser to descend from.
	LocalVector<int32_t> labels;
	labels.resize(count);
	for (uint64_t i = 0; i < count; i++) {
		labels[i] = sdf[i] < 0.0f ? -1 : -2;
	}

	LocalVector<int64_t> stack;
	int32_t next_label = 0;

	for (int z = 0; z < res.z; z++) {
		for (int y = 0; y < res.y; y++) {
			for (int x = 0; x < res.x; x++) {
				const int64_t seed = storage.sample_index(x, y, z);
				if (labels[seed] != -1) {
					continue;
				}

				const int32_t label = next_label++;
				AABB island_bounds(storage.sample_to_local(x, y, z), Vector3());
				uint64_t sample_count = 0;

				stack.clear();
				stack.push_back(seed);
				labels[seed] = label;

				while (!stack.is_empty()) {
					const int64_t index = stack[stack.size() - 1];
					stack.remove_at(stack.size() - 1);
					sample_count++;

					const int sz = (int)(index / ((int64_t)res.x * res.y));
					const int rem = (int)(index % ((int64_t)res.x * res.y));
					const int sy = rem / res.x;
					const int sx = rem % res.x;
					island_bounds.expand_to(storage.sample_to_local(sx, sy, sz));

					const int neighbors[6][3] = {
						{ sx - 1, sy, sz }, { sx + 1, sy, sz },
						{ sx, sy - 1, sz }, { sx, sy + 1, sz },
						{ sx, sy, sz - 1 }, { sx, sy, sz + 1 }
					};
					for (int n = 0; n < 6; n++) {
						const int nx = neighbors[n][0];
						const int ny = neighbors[n][1];
						const int nz = neighbors[n][2];
						if (nx < 0 || ny < 0 || nz < 0 || nx >= res.x || ny >= res.y || nz >= res.z) {
							continue;
						}
						const int64_t neighbor = storage.sample_index(nx, ny, nz);
						if (labels[neighbor] == -1) {
							labels[neighbor] = label;
							stack.push_back(neighbor);
						}
					}
				}

				// One voxel of margin, so the island bounds contain the surface band and
				// not only the solid samples themselves.
				const float margin = storage.get_voxel_size();
				island_bounds.position -= Vector3(margin, margin, margin);
				island_bounds.size += Vector3(margin, margin, margin) * 2.0f;

				Dictionary island;
				island["index"] = label;
				island["aabb"] = island_bounds;
				island["sample_count"] = (int64_t)sample_count;
				island["volume"] = (float)sample_count * Math::pow(storage.get_voxel_size(), 3.0f);
				islands.push_back(island);
			}
		}
	}

	return islands;
}

RID VoxelServer::field_extract_island(RID p_field, int p_island_index, bool p_remove_from_source) {
	MutexLock lock(mutex);
	Field *source = field_owner.get_or_null(p_field);
	ERR_FAIL_NULL_V(source, RID());
	ERR_FAIL_COND_V(_configure_if_needed(source) != OK, RID());

	const TypedArray<Dictionary> islands = field_find_islands(p_field);
	ERR_FAIL_INDEX_V(p_island_index, islands.size(), RID());
	const Dictionary island = islands[p_island_index];
	const AABB island_bounds = island["aabb"];

	// A new field, not a mesh. If islands come back as fields then knocking a chip
	// off a workpiece and splitting a crate in half are literally the same code.
	const RID result = field_create();
	Field *target = field_owner.get_or_null(result);
	ERR_FAIL_NULL_V(target, RID());
	target->pending_bounds = island_bounds;
	target->pending_voxel_size = source->pending_voxel_size;
	target->needs_configure = true;
	target->mesher = source->mesher;
	ERR_FAIL_COND_V(_configure_if_needed(target) != OK, RID());

	for (int i = 1; i < source->storage.get_channel_count(); i++) {
		const VoxelChannel &channel = source->storage.get_channel(i);
		target->storage.add_channel(channel.name, channel.format);
	}

	const VoxelDenseStorage &source_storage = source->storage;
	VoxelDenseStorage &target_storage = target->storage;
	const Vector3i target_res = target_storage.get_sample_resolution();
	float *target_sdf = target_storage.get_sdf_ptrw();

	for (int z = 0; z < target_res.z; z++) {
		for (int y = 0; y < target_res.y; y++) {
			for (int x = 0; x < target_res.x; x++) {
				const Vector3 local = target_storage.sample_to_local(x, y, z);
				const int64_t target_index = target_storage.sample_index(x, y, z);
				target_sdf[target_index] = source_storage.sample_local(local);

				for (int c = 0; c < target_storage.get_channel_count(); c++) {
					const int source_channel = source_storage.find_channel(target_storage.get_channel(c).name);
					if (source_channel < 0) {
						continue;
					}
					// Nearest sample rather than an interpolated one, because a material
					// ID that has been averaged with its neighbor is not a material ID.
					const Vector3 grid = (local - source_storage.get_bounds().position) / source_storage.get_voxel_size();
					const Vector3i nearest(
							CLAMP((int)Math::round(grid.x), 0, source_storage.get_sample_resolution().x - 1),
							CLAMP((int)Math::round(grid.y), 0, source_storage.get_sample_resolution().y - 1),
							CLAMP((int)Math::round(grid.z), 0, source_storage.get_sample_resolution().z - 1));
					const uint32_t value = source_storage.get_channel(source_channel).get_raw((uint64_t)source_storage.sample_index(nearest.x, nearest.y, nearest.z));
					target_storage.get_channel_mut(c).set_raw((uint64_t)target_index, value);
				}
			}
		}
	}

	target_storage.mark_all_dirty();
	target->mesh_dirty = true;
	target->collision_dirty = true;

	if (p_remove_from_source) {
		const Vector3i source_res = source_storage.get_sample_resolution();
		float *source_sdf = source->storage.get_sdf_ptrw();
		const float far_value = source_storage.get_far_value();
		for (int z = 0; z < source_res.z; z++) {
			for (int y = 0; y < source_res.y; y++) {
				for (int x = 0; x < source_res.x; x++) {
					const Vector3 local = source_storage.sample_to_local(x, y, z);
					if (island_bounds.has_point(local)) {
						source_sdf[source_storage.sample_index(x, y, z)] = far_value;
					}
				}
			}
		}
		source->storage.mark_region_dirty(island_bounds);
		source->mesh_dirty = true;
		source->collision_dirty = true;
	}

	return result;
}

double VoxelServer::_get_monitor_value(Monitor p_monitor) const {
	switch (p_monitor) {
		case MONITOR_FIELD_COUNT:
			return (double)field_owner.get_rid_count();

		case MONITOR_DIRTY_CHUNKS: {
			double total = 0.0;
			const LocalVector<RID> owned = field_owner.get_owned_list();
			for (uint32_t i = 0; i < owned.size(); i++) {
				const Field *field = field_owner.get_or_null(owned[i]);
				if (field) {
					total += (double)field->storage.get_dirty_chunks().size();
				}
			}
			return total;
		}

		case MONITOR_CPU_BYTES: {
			double total = 0.0;
			const LocalVector<RID> owned = field_owner.get_owned_list();
			for (uint32_t i = 0; i < owned.size(); i++) {
				const Field *field = field_owner.get_or_null(owned[i]);
				if (field) {
					total += (double)field->storage.get_memory_usage();
				}
			}
			return total;
		}

		case MONITOR_STAMP_USEC:
			return last_stamp_usec;

		case MONITOR_EXTRACT_USEC:
			return last_extract_usec;

		case MONITOR_TRIANGLE_COUNT: {
			double total = 0.0;
			const LocalVector<RID> owned = field_owner.get_owned_list();
			for (uint32_t i = 0; i < owned.size(); i++) {
				const Field *field = field_owner.get_or_null(owned[i]);
				if (field) {
					total += (double)field->triangle_count;
				}
			}
			return total;
		}

		default:
			return 0.0;
	}
}

double VoxelServer::get_monitor(Monitor p_monitor) const {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_monitor, MONITOR_MAX, 0.0);
	return _get_monitor_value(p_monitor);
}

void VoxelServer::register_monitors() {
	Performance *performance = Performance::get_singleton();
	if (!performance || monitors_registered) {
		return;
	}
	monitors_registered = true;

	// These exist because the previous attempt at this system shipped a 110 ms
	// stamp that nobody noticed. A number that graphs in the debugger is the
	// difference between measuring and guessing.
	struct Entry {
		const char *id;
		Monitor monitor;
	};
	static const Entry entries[] = {
		{ "voxel/chunks_dirty", MONITOR_DIRTY_CHUNKS },
		{ "voxel/cpu_bytes", MONITOR_CPU_BYTES },
		{ "voxel/stamp_usec", MONITOR_STAMP_USEC },
		{ "voxel/extract_usec", MONITOR_EXTRACT_USEC },
		{ "voxel/triangles", MONITOR_TRIANGLE_COUNT },
	};

	for (const Entry &entry : entries) {
		Vector<Variant> args;
		args.push_back((int)entry.monitor);
		performance->add_custom_monitor(entry.id, callable_mp(this, &VoxelServer::get_monitor), args);
	}
}

void VoxelServer::unregister_monitors() {
	Performance *performance = Performance::get_singleton();
	if (!performance || !monitors_registered) {
		return;
	}
	monitors_registered = false;

	static const char *ids[] = {
		"voxel/chunks_dirty",
		"voxel/cpu_bytes",
		"voxel/stamp_usec",
		"voxel/extract_usec",
		"voxel/triangles",
	};
	for (const char *id : ids) {
		if (performance->has_custom_monitor(id)) {
			performance->remove_custom_monitor(id);
		}
	}
}

void VoxelServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_field_count"), &VoxelServer::get_field_count);

	ClassDB::bind_method(D_METHOD("field_create"), &VoxelServer::field_create);
	ClassDB::bind_method(D_METHOD("field_free", "field"), &VoxelServer::field_free);
	ClassDB::bind_method(D_METHOD("field_is_valid", "field"), &VoxelServer::field_is_valid);

	ClassDB::bind_method(D_METHOD("field_set_bounds", "field", "bounds"), &VoxelServer::field_set_bounds);
	ClassDB::bind_method(D_METHOD("field_get_bounds", "field"), &VoxelServer::field_get_bounds);
	ClassDB::bind_method(D_METHOD("field_set_voxel_size", "field", "voxel_size"), &VoxelServer::field_set_voxel_size);
	ClassDB::bind_method(D_METHOD("field_get_voxel_size", "field"), &VoxelServer::field_get_voxel_size);
	ClassDB::bind_method(D_METHOD("field_get_cell_resolution", "field"), &VoxelServer::field_get_cell_resolution);

	ClassDB::bind_method(D_METHOD("field_add_channel", "field", "name", "format"), &VoxelServer::field_add_channel);
	ClassDB::bind_method(D_METHOD("field_find_channel", "field", "name"), &VoxelServer::field_find_channel);
	ClassDB::bind_method(D_METHOD("field_get_channel_count", "field"), &VoxelServer::field_get_channel_count);

	ClassDB::bind_method(D_METHOD("field_set_mesher", "field", "mesher"), &VoxelServer::field_set_mesher);
	ClassDB::bind_method(D_METHOD("field_get_mesher", "field"), &VoxelServer::field_get_mesher);

	ClassDB::bind_method(D_METHOD("field_stamp", "field", "brush", "transform", "operation"), &VoxelServer::field_stamp);
	ClassDB::bind_method(D_METHOD("field_stamp_path", "field", "brush", "path", "operation"), &VoxelServer::field_stamp_path);
	ClassDB::bind_method(D_METHOD("field_clear", "field"), &VoxelServer::field_clear);

	ClassDB::bind_method(D_METHOD("field_sample", "field", "local_position"), &VoxelServer::field_sample);
	ClassDB::bind_method(D_METHOD("field_raycast", "field", "from", "direction", "max_distance"), &VoxelServer::field_raycast);
	ClassDB::bind_method(D_METHOD("field_get_volume", "field"), &VoxelServer::field_get_volume);

	ClassDB::bind_method(D_METHOD("field_get_mesh", "field"), &VoxelServer::field_get_mesh);
	ClassDB::bind_method(D_METHOD("field_get_triangle_count", "field"), &VoxelServer::field_get_triangle_count);

	ClassDB::bind_method(D_METHOD("field_set_collision_step", "field", "step"), &VoxelServer::field_set_collision_step);
	ClassDB::bind_method(D_METHOD("field_get_collision_step", "field"), &VoxelServer::field_get_collision_step);
	ClassDB::bind_method(D_METHOD("field_get_collision_faces", "field", "step"), &VoxelServer::field_get_collision_faces, DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("field_find_islands", "field"), &VoxelServer::field_find_islands);
	ClassDB::bind_method(D_METHOD("field_extract_island", "field", "island_index", "remove_from_source"), &VoxelServer::field_extract_island, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("get_monitor", "monitor"), &VoxelServer::get_monitor);

	BIND_ENUM_CONSTANT(FORMAT_UINT8);
	BIND_ENUM_CONSTANT(FORMAT_UNORM8);
	BIND_ENUM_CONSTANT(FORMAT_UNORM16);
	BIND_ENUM_CONSTANT(FORMAT_FLOAT32);

	BIND_ENUM_CONSTANT(OP_UNION);
	BIND_ENUM_CONSTANT(OP_SUBTRACT);
	BIND_ENUM_CONSTANT(OP_INTERSECT);
	BIND_ENUM_CONSTANT(OP_SMOOTH);
	BIND_ENUM_CONSTANT(OP_PAINT);

	BIND_ENUM_CONSTANT(MONITOR_FIELD_COUNT);
	BIND_ENUM_CONSTANT(MONITOR_DIRTY_CHUNKS);
	BIND_ENUM_CONSTANT(MONITOR_CPU_BYTES);
	BIND_ENUM_CONSTANT(MONITOR_STAMP_USEC);
	BIND_ENUM_CONSTANT(MONITOR_EXTRACT_USEC);
	BIND_ENUM_CONSTANT(MONITOR_TRIANGLE_COUNT);
	BIND_ENUM_CONSTANT(MONITOR_MAX);
}

VoxelServer::VoxelServer() {
	ERR_FAIL_COND(singleton != nullptr);
	singleton = this;
}

VoxelServer::~VoxelServer() {
	unregister_monitors();

	// A field that outlives the server is a leak that would otherwise be silent.
	const LocalVector<RID> owned = field_owner.get_owned_list();
	for (uint32_t i = 0; i < owned.size(); i++) {
		field_owner.free(owned[i]);
	}

	singleton = nullptr;
}
