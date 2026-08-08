def can_build(env, platform):
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "VoxelBrush",
        "VoxelBrushBox",
        "VoxelBrushCapsule",
        "VoxelBrushMesh",
        "VoxelBrushSphere",
        "VoxelField",
        "VoxelMesher",
        "VoxelMesherMarchingCubes",
        "VoxelServer",
        "VoxelShape3D",
        "VoxelVolume3D",
    ]


def get_doc_path():
    return "doc_classes"


def get_icons_path():
    return "icons"
