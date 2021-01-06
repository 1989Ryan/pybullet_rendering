// Copyright (c) 2019-2020 INRIA.
// This source code is licensed under the LGPLv3 license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

// project imports
#include <scene/SceneGraph.h>
#include <utils/math.h>

// bullet imports
#include <Importers/ImportURDFDemo/UrdfParser.h>
#include <SharedMemory/SharedMemoryPublic.h>

// std imports
#include <cstring>

/**
 * @brief Convert PyBullet's transform and scale to Affine3f
 *
 * @param pose - pose to update
 * @param frame - transformation
 * @param scale - scale vector
 */
inline Affine3f makePose(const btTransform& frame, const btVector3& scale)
{
    const auto& origin = frame.getOrigin();
    const auto& basis = frame.getBasis();

    btQuaternion quat;
    basis.getRotation(quat);

    Affine3f pose;
    pose.origin = {float(origin.x()), float(origin.y()), float(origin.z())};
    pose.quat = {float(quat.getW()), float(quat.x()), float(quat.y()), float(quat.z())};
    pose.scale = {float(scale.x()), float(scale.y()), float(scale.z())};
    return pose;
}

/**
 * @brief Convert geometry to MeshData
 *
 * @param geometry - geometry containing the mesh data
 * @return std::shared_ptr<scene::MeshData>
 */
inline std::shared_ptr<scene::MeshData> getMeshData(const UrdfGeometry& geometry)
{
    std::vector<float> vertices;
    std::vector<float> uvs;
    std::vector<float> normals;
    std::vector<int> indices;

    const int verticesCount = geometry.m_vertices.size();
    const int uvsCount = geometry.m_uvs.size();
    const int normalsCount = geometry.m_normals.size();
    const int indicesCount = geometry.m_indices.size();

    vertices.reserve(verticesCount * 3);
    uvs.reserve(uvsCount * 2);
    normals.reserve(normalsCount * 3);
    indices.reserve(indicesCount);

    for (int i = 0; i < verticesCount; ++i) {
        const auto& vertex = geometry.m_vertices[i];
        vertices.push_back(float(vertex.x()));
        vertices.push_back(float(vertex.y()));
        vertices.push_back(float(vertex.z()));
    }
    for (int i = 0; i < uvsCount; ++i) {
        const auto& uv = geometry.m_uvs[i];
        uvs.push_back(float(uv.x()));
        uvs.push_back(float(uv.y()));
    }
    for (int i = 0; i < normalsCount; ++i) {
        const auto& normal = geometry.m_normals[i];
        normals.push_back(float(normal.x()));
        normals.push_back(float(normal.y()));
        normals.push_back(float(normal.z()));
    }
    for (int i = 0; i < indicesCount; ++i) {
        indices.push_back(geometry.m_indices[i]);
    }
    return std::make_shared<scene::MeshData>(std::move(vertices), std::move(uvs),
                                             std::move(normals), std::move(indices));
}

/**
 * @brief Convert URDF shape to internal scene::Shape description
 *
 * @param urdfShape - pybullet URDF shape description
 * @param urdfMaterial - pybullet URDF material description
 * @param localInertiaFrame - link inertia frame
 * @param flags - URDF loading options
 * @return scene::Shape
 */
inline scene::Shape makeShape(const UrdfShape& urdfShape, const UrdfMaterial& urdfMaterial,
                              const btTransform& localInertiaFrame, int flags,
                              scene::SceneGraph& graph)
{
    using namespace scene;
    auto frame = localInertiaFrame.inverse() * urdfShape.m_linkLocalFrame;

    const auto& fname = urdfMaterial.m_textureFilename;
    const int textureId = fname.empty() ? -1 : graph.registerTexture(Texture{fname});

    const auto& d = urdfMaterial.m_matColor.m_rgbaColor;
    const auto& s = urdfMaterial.m_matColor.m_specularColor;

    auto material =
        std::make_shared<Material>(Color4f{float(d[0]), float(d[1]), float(d[2]), float(d[3])},
                                   Color3f{float(s[0]), float(s[1]), float(s[2])}, textureId);

    const auto& geometry = urdfShape.m_geometry;
    if (URDF_GEOM_BOX == geometry.m_type) {
        const auto pose = makePose(frame, geometry.m_boxSize);
        return Shape{ShapeType::Cube, pose, material};
    }
    else if (URDF_GEOM_SPHERE == geometry.m_type) {
        const auto r = geometry.m_sphereRadius;
        const auto pose = makePose(frame, {r, r, r});
        return Shape{ShapeType::Sphere, pose, material};
    }
    else if (URDF_GEOM_CYLINDER == geometry.m_type) {
        const auto r = geometry.m_capsuleRadius;
        const auto h = geometry.m_capsuleHeight;
        const auto pose = makePose(frame, {r, r, h});
        return Shape{ShapeType::Cylinder, pose, material};
    }
    else if (URDF_GEOM_CAPSULE == geometry.m_type) {
        const auto r = geometry.m_capsuleRadius;
        const auto h = geometry.m_capsuleHeight;
        const auto pose = makePose(frame, {r, r, h});
        return Shape{ShapeType::Capsule, pose, material};
    }
    else if (URDF_GEOM_PLANE == geometry.m_type) {
        const auto n = geometry.m_planeNormal;
        const auto z = btVector3{0.0, 0.0, 1.0};
        if (n.dot(z) < 0.99) {
            const auto axis = n.cross(z);
            const auto quat = btQuaternion(axis, btAsin(axis.length()));
            frame = frame * btTransform(quat);
        }
        const auto pose = makePose(frame, {1.0, 1.0, 1.0});
        return Shape{ShapeType::Plane, pose, material};
    }
    else if (URDF_GEOM_MESH == geometry.m_type) {
        const auto pose = makePose(frame, geometry.m_meshScale);
        const auto mesh = geometry.m_meshFileType == UrdfGeometry::MEMORY_VERTICES
                              ? std::make_shared<Mesh>(getMeshData(geometry))
                              : std::make_shared<Mesh>(geometry.m_meshFileName);
        if (flags & URDF_USE_MATERIAL_COLORS_FROM_MTL)
            material.reset();
        return Shape{ShapeType::Mesh, pose, material, mesh};
    }
    else if (URDF_GEOM_HEIGHTFIELD == geometry.m_type) {
        const auto pose = makePose(frame, {1.0, 1.0, 1.0});
        const auto mesh = std::make_shared<Mesh>(getMeshData(geometry));
        return Shape{ShapeType::Heightfield, pose, material, mesh};
    }

    return Shape{};
}

/**
 * @brief Convert URDF shape to a pybullet's visual shape representation
 *
 * @param urdfShape - pybullet URDF shape description
 * @param urdfMaterial - pybullet URDF material description
 * @param localInertiaFrame - link inertia frame
 * @param bodyUniqueId - body unique id
 * @param linkIndex - link index
 * @return VisualShapeData
 */
inline b3VisualShapeData makeVisualShapeData(const UrdfShape& urdfShape,
                                             const UrdfMaterial& urdfMaterial,
                                             const btTransform& localInertiaFrame, //
                                             int bodyUniqueId, int linkIndex)
{
    b3VisualShapeData shape;

    shape.m_objectUniqueId = bodyUniqueId;
    shape.m_linkIndex = linkIndex;

    const auto& origin = localInertiaFrame.getOrigin();
    shape.m_localVisualFrame[0] = origin.getX();
    shape.m_localVisualFrame[1] = origin.getY();
    shape.m_localVisualFrame[2] = origin.getZ();

    const auto& rotate = localInertiaFrame.getRotation();
    shape.m_localVisualFrame[3] = rotate.getX();
    shape.m_localVisualFrame[4] = rotate.getY();
    shape.m_localVisualFrame[5] = rotate.getZ();
    shape.m_localVisualFrame[6] = rotate.getW();

    shape.m_visualGeometryType = urdfShape.m_geometry.m_type;
    shape.m_dimensions[0] = urdfShape.m_geometry.m_meshScale[0];
    shape.m_dimensions[1] = urdfShape.m_geometry.m_meshScale[0];
    shape.m_dimensions[2] = urdfShape.m_geometry.m_meshScale[0];

    shape.m_rgbaColor[0] = urdfMaterial.m_matColor.m_rgbaColor[0];
    shape.m_rgbaColor[1] = urdfMaterial.m_matColor.m_rgbaColor[1];
    shape.m_rgbaColor[2] = urdfMaterial.m_matColor.m_rgbaColor[2];
    shape.m_rgbaColor[3] = urdfMaterial.m_matColor.m_rgbaColor[3];

    std::strncpy(shape.m_meshAssetFileName, urdfMaterial.m_textureFilename.c_str(),
                 VISUAL_SHAPE_MAX_PATH_LEN);

    shape.m_textureUniqueId = -1;
    shape.m_openglTextureId = -1;
    shape.m_tinyRendererTextureId = -1;

    return shape;
}
