/*
 Copyright (C) 2010-2017 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BrushNode.h"

#include "Error.h"
#include "Exceptions.h"
#include "FloatType.h"
#include "Model/BezierPatch.h"
#include "Model/Brush.h"
#include "Model/BrushFace.h"
#include "Model/BrushFaceHandle.h"
#include "Model/BrushGeometry.h"
#include "Model/EditorContext.h"
#include "Model/EntityNode.h"
#include "Model/GroupNode.h"
#include "Model/LayerNode.h"
#include "Model/LinkedGroupUtils.h"
#include "Model/ModelUtils.h"
#include "Model/PatchNode.h"
#include "Model/PickResult.h"
#include "Model/TagVisitor.h"
#include "Model/TexCoordSystem.h"
#include "Model/Validator.h"
#include "Model/WorldNode.h"
#include "Polyhedron.h"
#include "Polyhedron_Matcher.h"
#include "Renderer/BrushRendererBrushCache.h"

#include "kdl/overload.h"
#include "kdl/result.h"
#include "kdl/string_utils.h"
#include "kdl/vector_utils.h"

#include "vm/intersection.h"
#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/polygon.h"
#include "vm/segment.h"
#include "vm/util.h"
#include "vm/vec.h"
#include "vm/vec_ext.h"

#include <algorithm> // for std::remove
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace TrenchBroom
{
namespace Model
{
const HitType::Type BrushNode::BrushHitType = HitType::freeType();

BrushNode::BrushNode(Brush brush)
  : m_brushRendererBrushCache(std::make_unique<Renderer::BrushRendererBrushCache>())
  , m_brush(std::move(brush))
{
  clearSelectedFaces();
}

BrushNode::~BrushNode() = default;

const EntityNodeBase* BrushNode::entity() const
{
  return visitParent(
           kdl::overload(
             [](const WorldNode* world) -> const EntityNodeBase* { return world; },
             [](const EntityNode* entity) -> const EntityNodeBase* { return entity; },
             [](auto&& thisLambda, const LayerNode* layer) -> const EntityNodeBase* {
               return layer->visitParent(thisLambda).value_or(nullptr);
             },
             [](auto&& thisLambda, const GroupNode* group) -> const EntityNodeBase* {
               return group->visitParent(thisLambda).value_or(nullptr);
             },
             [](auto&& thisLambda, const BrushNode* brush) -> const EntityNodeBase* {
               return brush->visitParent(thisLambda).value_or(nullptr);
             },
             [](auto&& thisLambda, const PatchNode* patch) -> const EntityNodeBase* {
               return patch->visitParent(thisLambda).value_or(nullptr);
             }))
    .value_or(nullptr);
}

EntityNodeBase* BrushNode::entity()
{
  return const_cast<EntityNodeBase*>(const_cast<const BrushNode*>(this)->entity());
}

const Brush& BrushNode::brush() const
{
  return m_brush;
}

Brush BrushNode::setBrush(Brush brush)
{
  const auto nodeChange = NotifyNodeChange{*this};
  const auto boundsChange = NotifyPhysicalBoundsChange{*this};

  using std::swap;
  swap(m_brush, brush);

  updateSelectedFaceCount();
  invalidateIssues();
  invalidateVertexCache();

  return brush;
}

bool BrushNode::hasSelectedFaces() const
{
  return m_selectedFaceCount > 0u;
}

void BrushNode::selectFace(const size_t faceIndex)
{
  m_brush.face(faceIndex).select();
  ++m_selectedFaceCount;
}

void BrushNode::deselectFace(const size_t faceIndex)
{
  m_brush.face(faceIndex).deselect();
  --m_selectedFaceCount;
}

void BrushNode::updateFaceTags(const size_t faceIndex, TagManager& tagManager)
{
  m_brush.face(faceIndex).updateTags(tagManager);
}

void BrushNode::setFaceTexture(const size_t faceIndex, Assets::Texture* texture)
{
  m_brush.face(faceIndex).setTexture(texture);

  invalidateIssues();
  invalidateVertexCache();
}

static bool containsPatch(const Brush& brush, const PatchGrid& grid)
{
  if (!brush.bounds().contains(grid.bounds))
  {
    return false;
  }

  for (const auto& point : grid.points)
  {
    if (!brush.containsPoint(point.position))
    {
      return false;
    }
  }

  return true;
}

bool BrushNode::contains(const Node* node) const
{
  return node->accept(kdl::overload(
    [](const WorldNode*) { return false; },
    [](const LayerNode*) { return false; },
    [&](const GroupNode* group) { return m_brush.contains(group->logicalBounds()); },
    [&](const EntityNode* entity) { return m_brush.contains(entity->logicalBounds()); },
    [&](const BrushNode* brush) { return m_brush.contains(brush->brush()); },
    [&](const PatchNode* patch) { return containsPatch(m_brush, patch->grid()); }));
}

static bool faceIntersectsEdge(
  const BrushFace& face, const vm::vec3& p0, const vm::vec3& p1)
{
  const auto ray = vm::ray3{p0, p1 - p0}; // not normalized
  if (const auto dist = face.intersectWithRay(ray); !vm::is_nan(dist))
  {
    // dist is scaled by inverse of vm::length(p1 - p0)
    return dist >= 0.0 && dist <= 1.0;
  }
  return false;
}

static bool intersectsPatch(const Brush& brush, const PatchGrid& grid)
{
  if (!brush.bounds().intersects(grid.bounds))
  {
    return false;
  }

  // if brush contains any grid point, they intersect (or grid is contained, which we
  // count as intersection)
  for (const auto& point : grid.points)
  {
    if (brush.containsPoint(point.position))
    {
      return true;
    }
  }

  // now check if any quad edge of the given grid intersects with any face
  for (const auto& face : brush.faces())
  {
    // check row edges
    for (size_t row = 0u; row < grid.pointRowCount; ++row)
    {
      for (size_t col = 0u; col < grid.pointColumnCount - 1u; ++col)
      {
        const auto& p0 = grid.point(row, col).position;
        const auto& p1 = grid.point(row, col + 1u).position;
        if (faceIntersectsEdge(face, p0, p1))
        {
          return true;
        }
      }
    }
    // check column edges
    for (size_t col = 0u; col < grid.pointColumnCount; ++col)
    {
      for (size_t row = 0u; row < grid.pointRowCount - 1u; ++row)
      {
        const auto& p0 = grid.point(row, col).position;
        const auto& p1 = grid.point(row + 1u, col).position;
        if (faceIntersectsEdge(face, p0, p1))
        {
          return true;
        }
      }
    }
  }

  return false;
}

bool BrushNode::intersects(const Node* node) const
{
  return node->accept(kdl::overload(
    [](const WorldNode*) { return false; },
    [](const LayerNode*) { return false; },
    [&](const GroupNode* group) { return m_brush.intersects(group->logicalBounds()); },
    [&](const EntityNode* entity) { return m_brush.intersects(entity->logicalBounds()); },
    [&](const BrushNode* brush) { return m_brush.intersects(brush->brush()); },
    [&](const PatchNode* patch) { return intersectsPatch(m_brush, patch->grid()); }));
}

void BrushNode::clearSelectedFaces()
{
  for (BrushFace& face : m_brush.faces())
  {
    if (face.selected())
    {
      face.deselect();
    }
  }
  m_selectedFaceCount = 0u;
}

void BrushNode::updateSelectedFaceCount()
{
  m_selectedFaceCount = 0u;
  for (const BrushFace& face : m_brush.faces())
  {
    if (face.selected())
    {
      ++m_selectedFaceCount;
    }
  }
}

const std::string& BrushNode::doGetName() const
{
  static const std::string name("brush");
  return name;
}

const vm::bbox3& BrushNode::doGetLogicalBounds() const
{
  return m_brush.bounds();
}

const vm::bbox3& BrushNode::doGetPhysicalBounds() const
{
  return logicalBounds();
}

FloatType BrushNode::doGetProjectedArea(const vm::axis::type axis) const
{
  const auto normal = vm::vec3::axis(axis);

  auto result = static_cast<FloatType>(0);
  for (const auto& face : m_brush.faces())
  {
    // only consider one side of the brush -- doesn't matter which one!
    if (vm::dot(face.boundary().normal, normal) > 0.0)
    {
      result += face.projectedArea(axis);
    }
  }

  return result;
}

Node* BrushNode::doClone(
  const vm::bbox3& /* worldBounds */, const SetLinkId setLinkIds) const
{
  auto result = std::make_unique<BrushNode>(m_brush);
  result->cloneLinkId(*this, setLinkIds);
  cloneAttributes(result.get());
  return result.release();
}

bool BrushNode::doCanAddChild(const Node* /* child */) const
{
  return false;
}

bool BrushNode::doCanRemoveChild(const Node* /* child */) const
{
  return false;
}

bool BrushNode::doRemoveIfEmpty() const
{
  return false;
}

bool BrushNode::doShouldAddToSpacialIndex() const
{
  return true;
}

bool BrushNode::doSelectable() const
{
  return true;
}

void BrushNode::doAccept(NodeVisitor& visitor)
{
  visitor.visit(this);
}

void BrushNode::doAccept(ConstNodeVisitor& visitor) const
{
  visitor.visit(this);
}

void BrushNode::doPick(
  const EditorContext& editorContext, const vm::ray3& ray, PickResult& pickResult)
{
  if (editorContext.visible(this))
  {
    if (const auto hit = findFaceHit(ray))
    {
      const auto [distance, faceIndex] = *hit;
      ensure(!vm::is_nan(distance), "nan hit distance");
      const auto hitPoint = vm::point_at_distance(ray, distance);
      pickResult.addHit(
        Hit(BrushHitType, distance, hitPoint, BrushFaceHandle(this, faceIndex)));
    }
  }
}

void BrushNode::doFindNodesContaining(const vm::vec3& point, std::vector<Node*>& result)
{
  if (m_brush.containsPoint(point))
  {
    result.push_back(this);
  }
}

std::optional<std::tuple<FloatType, size_t>> BrushNode::findFaceHit(
  const vm::ray3& ray) const
{
  if (!vm::is_nan(vm::intersect_ray_bbox(ray, logicalBounds())))
  {
    for (size_t i = 0u; i < m_brush.faceCount(); ++i)
    {
      const auto& face = m_brush.face(i);
      const auto distance = face.intersectWithRay(ray);
      if (!vm::is_nan(distance))
      {
        return std::make_tuple(distance, i);
      }
    }
  }
  return std::nullopt;
}

Node* BrushNode::doGetContainer()
{
  return parent();
}

LayerNode* BrushNode::doGetContainingLayer()
{
  return findContainingLayer(this);
}

GroupNode* BrushNode::doGetContainingGroup()
{
  return findContainingGroup(this);
}

void BrushNode::invalidateVertexCache()
{
  m_brushRendererBrushCache->invalidateVertexCache();
}

Renderer::BrushRendererBrushCache& BrushNode::brushRendererBrushCache() const
{
  return *m_brushRendererBrushCache;
}

void BrushNode::initializeTags(TagManager& tagManager)
{
  Taggable::initializeTags(tagManager);
  for (auto& face : m_brush.faces())
  {
    face.initializeTags(tagManager);
  }
}

void BrushNode::clearTags()
{
  for (auto& face : m_brush.faces())
  {
    face.clearTags();
  }
  Taggable::clearTags();
}

void BrushNode::updateTags(TagManager& tagManager)
{
  for (auto& face : m_brush.faces())
  {
    face.updateTags(tagManager);
  }
  Taggable::updateTags(tagManager);
}

bool BrushNode::allFacesHaveAnyTagInMask(TagType::Type tagMask) const
{
  // Possible optimization: Store the shared face tag mask in the brush and updated it
  // when a face changes.

  TagType::Type sharedFaceTags = TagType::AnyType; // set all bits to 1
  for (const auto& face : m_brush.faces())
  {
    sharedFaceTags &= face.tagMask();
  }
  return (sharedFaceTags & tagMask) != 0;
}

bool BrushNode::anyFaceHasAnyTag() const
{
  for (const auto& face : m_brush.faces())
  {
    if (face.hasAnyTag())
    {
      return true;
    }
  }
  return false;
}

bool BrushNode::anyFacesHaveAnyTagInMask(TagType::Type tagMask) const
{
  // Possible optimization: Store the shared face tag mask in the brush and updated it
  // when a face changes.

  for (const auto& face : m_brush.faces())
  {
    if (face.hasTag(tagMask))
    {
      return true;
    }
  }
  return false;
}

void BrushNode::doAcceptTagVisitor(TagVisitor& visitor)
{
  visitor.visit(*this);
}

void BrushNode::doAcceptTagVisitor(ConstTagVisitor& visitor) const
{
  visitor.visit(*this);
}

bool operator==(const BrushNode& lhs, const BrushNode& rhs)
{
  return lhs.brush() == rhs.brush();
}

bool operator!=(const BrushNode& lhs, const BrushNode& rhs)
{
  return !(lhs == rhs);
}
} // namespace Model
} // namespace TrenchBroom
