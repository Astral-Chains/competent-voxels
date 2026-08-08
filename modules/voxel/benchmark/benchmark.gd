extends Node

## Fixed synthetic workload for the voxel module.
##
## Every number here is a constant rather than a parameter on purpose. A benchmark
## whose workload can be tuned is a benchmark whose old results cannot be compared
## to its new ones, and the whole point of this file is to produce one baseline that
## a later phase can be diffed against. If the workload has to change, change it
## deliberately and record a fresh baseline.

# A block roughly the size of something held in two hands, at a resolution fine
# enough that a cut reads as a cut rather than as a step.
const BOUNDS := AABB(Vector3(-0.12, -0.12, -0.3), Vector3(0.24, 0.24, 0.6))
const VOXEL_SIZE := 0.005

# Strokes are the unit of work: a player drags a tool, and one drag is one call to
# stamp_path. Sampled at a rate a mouse or a controller actually produces.
const STROKE_COUNT := 24
const POINTS_PER_STROKE := 12
const TOOL_RADIUS := 0.012

const SEED := 0x5D0CE5


class Sample:
	var wall_usec: int
	var stamp_usec: float
	var extract_usec: float
	var triangles: float
	var dirty_chunks: float


func _ready() -> void:
	_report_environment()

	var field := VoxelField.new()
	field.bounds = BOUNDS
	field.voxel_size = VOXEL_SIZE

	var samples: Array[Sample] = []
	samples.append(_run_step("fill", field, _fill_workload))

	var rng := RandomNumberGenerator.new()
	rng.seed = SEED
	for index in STROKE_COUNT:
		var path := _make_stroke(rng)
		samples.append(_run_step("stroke %2d" % (index + 1), field, func(f: VoxelField) -> void:
			var tool_tip := VoxelBrushSphere.new()
			tool_tip.radius = TOOL_RADIUS
			f.stamp_path(tool_tip, path, VoxelField.OP_SUBTRACT)))

	# Collision is the other half of the cost of an edit, and it is easy to forget
	# because nothing renders it. Measure it separately so it cannot hide.
	var collision_start := Time.get_ticks_usec()
	var faces := field.get_collision_faces()
	var collision_usec := Time.get_ticks_usec() - collision_start

	_report(samples, field, faces.size() / 3, collision_usec)
	quit_with_success()


func _fill_workload(p_field: VoxelField) -> void:
	var block := VoxelBrushBox.new()
	block.size = BOUNDS.size * 0.9
	p_field.stamp(block, Transform3D(Basis(), BOUNDS.get_center()), VoxelField.OP_UNION)


## One stroke: a short arc across the surface of the block, at an arbitrary
## orientation, of the kind a hand dragging a tool produces.
func _make_stroke(p_rng: RandomNumberGenerator) -> PackedVector3Array:
	var center := BOUNDS.get_center()
	var start := center + Vector3(
			p_rng.randf_range(-0.1, 0.1),
			p_rng.randf_range(-0.1, 0.1),
			p_rng.randf_range(-0.26, 0.26))
	var direction := Vector3(
			p_rng.randf_range(-1.0, 1.0),
			p_rng.randf_range(-1.0, 1.0),
			p_rng.randf_range(-1.0, 1.0)).normalized()
	var curve := direction.cross(Vector3.UP).normalized() * p_rng.randf_range(-0.03, 0.03)

	var path := PackedVector3Array()
	for step in POINTS_PER_STROKE:
		var t := float(step) / float(POINTS_PER_STROKE - 1)
		path.append(start + direction * (t * 0.09) + curve * sin(t * PI))
	return path


func _run_step(p_label: String, p_field: VoxelField, p_workload: Callable) -> Sample:
	var start := Time.get_ticks_usec()
	p_workload.call(p_field)

	# The dirty count is a backlog, so it has to be read here: asking for the mesh
	# below empties it. This is the number that says whether an edit stayed local.
	var dirty := VoxelServer.get_monitor(VoxelServer.MONITOR_DIRTY_CHUNKS)

	# Extraction is lazy, so nothing has been meshed until the mesh is asked for.
	# Timing the edit without this would only be timing the write to the samples.
	var triangles := p_field.get_triangle_count()
	var elapsed := Time.get_ticks_usec() - start

	var sample := Sample.new()
	sample.wall_usec = elapsed
	sample.stamp_usec = VoxelServer.get_monitor(VoxelServer.MONITOR_STAMP_USEC)
	sample.extract_usec = VoxelServer.get_monitor(VoxelServer.MONITOR_EXTRACT_USEC)
	sample.triangles = triangles
	sample.dirty_chunks = dirty

	print("  %-10s %8.2f ms   stamp %7.2f ms   extract %7.2f ms   regions %5d   tris %7d" % [
			p_label,
			sample.wall_usec / 1000.0,
			sample.stamp_usec / 1000.0,
			sample.extract_usec / 1000.0,
			int(sample.dirty_chunks),
			int(sample.triangles)])
	return sample


func _report_environment() -> void:
	var cells := (BOUNDS.size / VOXEL_SIZE).round()
	print("Voxel module benchmark")
	print("  engine     %s" % Engine.get_version_info().string)
	print("  bounds     %.2f x %.2f x %.2f m at %.1f mm per voxel" % [
			BOUNDS.size.x, BOUNDS.size.y, BOUNDS.size.z, VOXEL_SIZE * 1000.0])
	print("  resolution %d x %d x %d cells" % [cells.x, cells.y, cells.z])
	print("  workload   1 fill, %d strokes of %d points, %.1f mm tool radius" % [
			STROKE_COUNT, POINTS_PER_STROKE, TOOL_RADIUS * 1000.0])
	print("  threads    %d logical cores" % OS.get_processor_count())
	# An unoptimized dev build is five times slower than an optimized one here, which
	# is more than any change this benchmark exists to measure, so a number without
	# its build config is worse than no number. The executable name carries the whole
	# config, and unlike OS.is_debug_build() it distinguishes dev_build from target,
	# which is the axis that actually matters: an editor build is always "debug".
	print("  build      %s" % OS.get_executable_path().get_file())
	print("")


func _report(p_samples: Array[Sample], p_field: VoxelField, p_collision_faces: int, p_collision_usec: int) -> void:
	# The fill is a whole-volume write and is nothing like a stroke, so averaging it
	# in would flatter or ruin the stroke number depending on the field size. Report
	# it on its own and take statistics over the strokes only.
	var strokes := p_samples.slice(1)
	var times: Array[float] = []
	for sample in strokes:
		times.append(sample.wall_usec / 1000.0)
	times.sort()

	var total := 0.0
	for time in times:
		total += time

	print("")
	print("Summary")
	print("  fill              %8.2f ms" % (p_samples[0].wall_usec / 1000.0))
	print("  stroke mean       %8.2f ms" % (total / times.size()))
	print("  stroke median     %8.2f ms" % times[times.size() / 2])
	print("  stroke worst      %8.2f ms" % times[times.size() - 1])
	print("  collision pass    %8.2f ms for %d faces" % [p_collision_usec / 1000.0, p_collision_faces])
	print("  cpu memory        %8.2f MiB" % (VoxelServer.get_monitor(VoxelServer.MONITOR_CPU_BYTES) / 1048576.0))
	print("  final triangles   %8d across %d surfaces" % [p_field.get_triangle_count(), p_field.get_surface_count()])
	print("  remaining volume  %8.2f %% of the block" % (p_field.get_volume() / (BOUNDS.size.x * BOUNDS.size.y * BOUNDS.size.z) * 100.0))
	print("")
	# One line to record as the baseline, so two runs can be diffed by eye.
	print("BASELINE build=%s stroke_median_ms=%.2f stroke_worst_ms=%.2f fill_ms=%.2f cpu_mib=%.2f triangles=%d" % [
			OS.get_executable_path().get_file(),
			times[times.size() / 2],
			times[times.size() - 1],
			p_samples[0].wall_usec / 1000.0,
			VoxelServer.get_monitor(VoxelServer.MONITOR_CPU_BYTES) / 1048576.0,
			p_field.get_triangle_count()])


func quit_with_success() -> void:
	# Deferred so the print buffer is flushed before the process goes away.
	get_tree().quit.call_deferred(0)
