/**************************************************************************/
/*  voxel_brush.cpp                                                       */
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

#include "voxel_brush.h"

#include "core/object/class_db.h"

void VoxelBrush::set_strength(float p_strength) {
	strength = CLAMP(p_strength, 0.0f, 1.0f);
	emit_changed();
}

float VoxelBrush::get_strength() const {
	return strength;
}

void VoxelBrush::set_falloff(const Ref<Curve> &p_falloff) {
	falloff = p_falloff;
	emit_changed();
}

Ref<Curve> VoxelBrush::get_falloff() const {
	return falloff;
}

void VoxelBrush::set_material_id(int p_material_id) {
	material_id = CLAMP(p_material_id, 0, 255);
	emit_changed();
}

int VoxelBrush::get_material_id() const {
	return material_id;
}

float VoxelBrush::sample_local(const Vector3 &p_local) const {
	return p_local.length();
}

float VoxelBrush::get_extent() const {
	return 0.0f;
}

float VoxelBrush::get_stroke_spacing() const {
	// Half the footprint keeps a swept stroke continuous without stamping the same
	// region over and over.
	return MAX(get_extent() * 0.5f, CMP_EPSILON);
}

float VoxelBrush::sample_weighted(const Vector3 &p_local) const {
	const float distance = sample_local(p_local);
	float weight = strength;
	if (falloff.is_valid() && falloff->get_point_count() > 0) {
		const float extent = get_extent();
		const float t = extent > 0.0f ? CLAMP(p_local.length() / extent, 0.0f, 1.0f) : 0.0f;
		weight *= falloff->sample_baked(t);
	}
	if (weight >= 1.0f) {
		return distance;
	}
	if (weight <= 0.0f) {
		// Fully faded out, so report a distance that can never win a min or max.
		return Math::abs(distance) + get_extent();
	}
	// Scaling the distance pushes the isosurface outward as the weight drops,
	// which is the same thing a partially applied tool does to real material.
	return distance / weight;
}

void VoxelBrush::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &VoxelBrush::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &VoxelBrush::get_strength);
	ClassDB::bind_method(D_METHOD("set_falloff", "falloff"), &VoxelBrush::set_falloff);
	ClassDB::bind_method(D_METHOD("get_falloff"), &VoxelBrush::get_falloff);
	ClassDB::bind_method(D_METHOD("set_material_id", "material_id"), &VoxelBrush::set_material_id);
	ClassDB::bind_method(D_METHOD("get_material_id"), &VoxelBrush::get_material_id);
	ClassDB::bind_method(D_METHOD("get_extent"), &VoxelBrush::get_extent);
	ClassDB::bind_method(D_METHOD("get_stroke_spacing"), &VoxelBrush::get_stroke_spacing);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "falloff", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_falloff", "get_falloff");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_id", PROPERTY_HINT_RANGE, "0,255,1"), "set_material_id", "get_material_id");
}

void VoxelBrushSphere::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.0f);
	emit_changed();
}

float VoxelBrushSphere::get_radius() const {
	return radius;
}

float VoxelBrushSphere::sample_local(const Vector3 &p_local) const {
	return p_local.length() - radius;
}

float VoxelBrushSphere::get_extent() const {
	return radius;
}

void VoxelBrushSphere::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &VoxelBrushSphere::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &VoxelBrushSphere::get_radius);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,10,0.0001,or_greater,suffix:m"), "set_radius", "get_radius");
}

void VoxelBrushCapsule::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.0f);
	emit_changed();
}

float VoxelBrushCapsule::get_radius() const {
	return radius;
}

void VoxelBrushCapsule::set_height(float p_height) {
	height = MAX(p_height, 0.0f);
	emit_changed();
}

float VoxelBrushCapsule::get_height() const {
	return height;
}

float VoxelBrushCapsule::sample_local(const Vector3 &p_local) const {
	// Aligned to local Y, matching CapsuleShape3D.
	const float half_segment = MAX(height * 0.5f - radius, 0.0f);
	Vector3 closest = p_local;
	closest.y -= CLAMP(p_local.y, -half_segment, half_segment);
	return closest.length() - radius;
}

float VoxelBrushCapsule::get_extent() const {
	return MAX(height * 0.5f, radius);
}

void VoxelBrushCapsule::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &VoxelBrushCapsule::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &VoxelBrushCapsule::get_radius);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &VoxelBrushCapsule::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &VoxelBrushCapsule::get_height);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,10,0.0001,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0,10,0.0001,or_greater,suffix:m"), "set_height", "get_height");
}

void VoxelBrushBox::set_size(const Vector3 &p_size) {
	size = p_size.maxf(0.0f);
	emit_changed();
}

Vector3 VoxelBrushBox::get_size() const {
	return size;
}

void VoxelBrushBox::set_corner_radius(float p_corner_radius) {
	corner_radius = MAX(p_corner_radius, 0.0f);
	emit_changed();
}

float VoxelBrushBox::get_corner_radius() const {
	return corner_radius;
}

float VoxelBrushBox::sample_local(const Vector3 &p_local) const {
	const Vector3 half = size * 0.5f;
	const float r = MIN(corner_radius, MIN(half.x, MIN(half.y, half.z)));
	const Vector3 q = p_local.abs() - (half - Vector3(r, r, r));
	const Vector3 outside = q.maxf(0.0f);
	const float inside = MIN(MAX(q.x, MAX(q.y, q.z)), 0.0f);
	return outside.length() + inside - r;
}

float VoxelBrushBox::get_extent() const {
	return (size * 0.5f).length();
}

void VoxelBrushBox::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &VoxelBrushBox::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &VoxelBrushBox::get_size);
	ClassDB::bind_method(D_METHOD("set_corner_radius", "corner_radius"), &VoxelBrushBox::set_corner_radius);
	ClassDB::bind_method(D_METHOD("get_corner_radius"), &VoxelBrushBox::get_corner_radius);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "corner_radius", PROPERTY_HINT_RANGE, "0,1,0.0001,or_greater,suffix:m"), "set_corner_radius", "get_corner_radius");
}
