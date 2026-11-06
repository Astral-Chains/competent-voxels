extends Node3D

## Interactive voxel demo.
##
## Mode is chosen with keys, not mouse buttons — mouse-button InputMap and
## editor-embedded right-clicks proved unreliable for telling add from carve.
## Material IDs are packed into vertex colour; a shader picks clay vs green.

const BLOCK_SIZE := Vector3(0.8, 0.35, 0.5)
const VOXEL_SIZE := 0.008
const TOOL_RADIUS := 0.04

@onready var volume: VoxelVolume3D = $VoxelVolume3D
@onready var camera_pivot: Node3D = $CameraPivot
@onready var camera: Camera3D = $CameraPivot/Camera3D
@onready var status: Label = $UI/Status
@onready var mode_label: Label = $UI/Mode

var field: VoxelField
var tool_tip: VoxelBrushSphere
var paint_material: ShaderMaterial
var orbit_yaw := 35.0
var orbit_pitch := -25.0
var last_stroke_point := Vector3(INF, INF, INF)
var painting := false
## true = add (union), false = carve (subtract)
var add_mode := false
var status_accum := 0.0


func _ready() -> void:
	field = VoxelField.new()
	field.bounds = AABB(-BLOCK_SIZE * 0.5 - Vector3.ONE * 0.05, BLOCK_SIZE + Vector3.ONE * 0.1)
	field.voxel_size = VOXEL_SIZE

	var block := VoxelBrushBox.new()
	block.size = BLOCK_SIZE
	block.corner_radius = 0.02
	block.material_id = 1
	field.stamp(block, Transform3D(), VoxelField.OP_UNION)

	var groove := VoxelBrushCapsule.new()
	groove.radius = 0.035
	groove.height = 0.35
	var groove_xform := Transform3D(Basis.from_euler(Vector3(0.0, 0.0, PI * 0.5)), Vector3(0.0, 0.05, 0.0))
	field.stamp(groove, groove_xform, VoxelField.OP_SUBTRACT)

	tool_tip = VoxelBrushSphere.new()
	tool_tip.radius = TOOL_RADIUS

	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode cull_disabled, unshaded, shadows_disabled;

uniform vec3 albedo_a : source_color = vec3(0.72, 0.55, 0.32);
uniform vec3 albedo_b : source_color = vec3(1.0, 0.0, 1.0);

void fragment() {
	ALBEDO = COLOR.r > 0.0055 ? albedo_b : albedo_a;
}
"""
	paint_material = ShaderMaterial.new()
	paint_material.shader = shader
	paint_material.set_shader_parameter("albedo_a", Color(0.72, 0.55, 0.32))
	paint_material.set_shader_parameter("albedo_b", Color(1.0, 0.0, 1.0))

	volume.field = field
	volume.collision_enabled = true
	volume.collision_voxel_multiplier = 4
	volume.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	volume.set_surface_override_material(0, paint_material)

	_update_camera()
	_drop_ball(Vector3(0.0, 0.55, 0.0))
	_update_mode_label()
	_refresh_status("Hold LMB · 1 carve · 2 add · WASD orbit · Space ball")
	print("VOXEL_DEMO_BUILD=maxpaint surfaces=", field.get_surface_count())


func _process(delta: float) -> void:
	var yaw := Input.get_axis("orbit_left", "orbit_right")
	var pitch := Input.get_axis("orbit_down", "orbit_up")
	if yaw != 0.0 or pitch != 0.0:
		orbit_yaw += yaw * 70.0 * delta
		orbit_pitch = clampf(orbit_pitch + pitch * 70.0 * delta, -80.0, 80.0)
		_update_camera()

	if painting:
		_stroke_at_cursor()

	status_accum += delta
	if status_accum >= 0.25:
		status_accum = 0.0
		_refresh_status()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode == KEY_1:
			add_mode = false
			_update_mode_label()
			get_viewport().set_input_as_handled()
			return
		if event.physical_keycode == KEY_2:
			add_mode = true
			_update_mode_label()
			get_viewport().set_input_as_handled()
			return

	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_LEFT:
			painting = mouse.pressed
			if mouse.pressed:
				last_stroke_point = Vector3(INF, INF, INF)
				_stroke_at_cursor()
			get_viewport().set_input_as_handled()
			return

	if event.is_action_pressed("drop_ball"):
		var hit := _pick_surface()
		var origin := Vector3(0.0, 0.7, 0.0)
		if not hit.is_empty():
			origin = hit.position + hit.normal * 0.25
		_drop_ball(origin)
		get_viewport().set_input_as_handled()


func _stroke_at_cursor() -> void:
	var hit := _pick_surface()
	if hit.is_empty():
		return

	var local: Vector3 = hit.position
	var normal: Vector3 = hit.normal
	if add_mode:
		# Plant the sphere into the hit so new solid samples exist above the old
		# surface. Pushing it out along the normal left a one-voxel air lens whose
		# cap still sampled the tan block.
		local -= normal * (TOOL_RADIUS * 0.35)
		tool_tip.material_id = 2
	else:
		# Bite into the surface.
		local -= normal * (TOOL_RADIUS * 0.2)
		tool_tip.material_id = 1

	if last_stroke_point.distance_to(local) < tool_tip.get_stroke_spacing() * 0.45:
		return
	last_stroke_point = local

	var operation := VoxelField.OP_UNION if add_mode else VoxelField.OP_SUBTRACT
	field.stamp(tool_tip, Transform3D(Basis(), local), operation)
	_update_mode_label()

	var op_name := "ADD" if add_mode else "CARVE"
	var stamp_ms := VoxelServer.get_monitor(VoxelServer.MONITOR_STAMP_USEC) / 1000.0
	var extract_ms := VoxelServer.get_monitor(VoxelServer.MONITOR_EXTRACT_USEC) / 1000.0
	print("%s sample=%.4f stamp=%.2fms extract=%.2fms surfaces=%d" % [
			op_name, field.sample(local), stamp_ms, extract_ms, field.get_surface_count()])


func _pick_surface() -> Dictionary:
	# Prefer the field's own raycast: it hits the visual surface, not the coarser
	# collision mesh, so the brush lands where you are looking.
	var mouse := get_viewport().get_mouse_position()
	var from := camera.project_ray_origin(mouse)
	var dir := camera.project_ray_normal(mouse)
	var local_from: Vector3 = volume.global_transform.affine_inverse() * from
	var local_dir: Vector3 = (volume.global_transform.basis.inverse() * dir).normalized()
	var hit := field.raycast(local_from, local_dir, 20.0)
	if hit.is_empty():
		# Fall back to physics for the rare case the field ray misses.
		var query := PhysicsRayQueryParameters3D.create(from, from + dir * 20.0)
		query.collision_mask = volume.collision_mask
		var physics_hit := get_world_3d().direct_space_state.intersect_ray(query)
		if physics_hit.is_empty():
			return {}
		return {
			"position": volume.global_transform.affine_inverse() * physics_hit.position,
			"normal": (volume.global_transform.basis.inverse() * physics_hit.normal).normalized(),
		}
	return hit


func _drop_ball(origin: Vector3) -> void:
	var body := RigidBody3D.new()
	body.position = origin
	body.mass = 0.4

	var shape := CollisionShape3D.new()
	var sphere := SphereShape3D.new()
	sphere.radius = 0.035
	shape.shape = sphere
	body.add_child(shape)

	var mesh := MeshInstance3D.new()
	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 0.035
	sphere_mesh.height = 0.07
	mesh.mesh = sphere_mesh
	var ball_mat := StandardMaterial3D.new()
	ball_mat.albedo_color = Color(0.2, 0.55, 0.9)
	ball_mat.metallic = 0.2
	mesh.material_override = ball_mat
	body.add_child(mesh)

	add_child(body)


func _update_camera() -> void:
	camera_pivot.rotation_degrees = Vector3(orbit_pitch, orbit_yaw, 0.0)


func _update_mode_label() -> void:
	var surfaces := field.get_surface_count() if field else -1
	var build := "maxpaint surfaces=%d" % surfaces
	if add_mode:
		mode_label.text = "ADD · %s" % build
		mode_label.add_theme_color_override("font_color", Color(0.1, 1.0, 0.9))
	else:
		mode_label.text = "CARVE · %s" % build
		mode_label.add_theme_color_override("font_color", Color(1.0, 0.45, 0.35))


func _refresh_status(message: String = "") -> void:
	var triangles := field.get_triangle_count() if field else 0
	var extract_ms := VoxelServer.get_monitor(VoxelServer.MONITOR_EXTRACT_USEC) / 1000.0
	var stamp_ms := VoxelServer.get_monitor(VoxelServer.MONITOR_STAMP_USEC) / 1000.0
	var line := "tris %d · stamp %.2f ms · extract %.2f ms" % [triangles, stamp_ms, extract_ms]
	if not message.is_empty():
		line = message + "\n" + line
	status.text = line
