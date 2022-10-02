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

#include "MissingDefinitionValidator.h"

#include "Model/BrushNode.h"
#include "Model/Entity.h"
#include "Model/EntityNode.h"
#include "Model/Issue.h"
#include "Model/IssueQuickFix.h"
#include "Model/MapFacade.h"

#include <string>
#include <vector>

namespace TrenchBroom {
namespace Model {
class MissingDefinitionValidator::MissingDefinitionIssue : public Issue {
public:
  static const IssueType Type;

public:
  explicit MissingDefinitionIssue(EntityNodeBase& node)
    : Issue(node) {}

private:
  IssueType doGetType() const override { return Type; }

  std::string doGetDescription() const override {
    const auto& entityNode = static_cast<EntityNodeBase&>(node());
    return entityNode.name() + " not found in entity definitions";
  }
};

const IssueType MissingDefinitionValidator::MissingDefinitionIssue::Type = Issue::freeType();

class MissingDefinitionValidator::MissingDefinitionIssueQuickFix : public IssueQuickFix {
public:
  MissingDefinitionIssueQuickFix()
    : IssueQuickFix(MissingDefinitionIssue::Type, "Delete entities") {}

private:
  void doApply(MapFacade* facade, const std::vector<const Issue*>& /* issues */) const override {
    facade->deleteObjects();
  }
};

MissingDefinitionValidator::MissingDefinitionValidator()
  : Validator(MissingDefinitionIssue::Type, "Missing entity definition") {
  addQuickFix(std::make_unique<MissingDefinitionIssueQuickFix>());
}

void MissingDefinitionValidator::doValidate(
  EntityNodeBase& node, std::vector<std::unique_ptr<Issue>>& issues) const {
  if (node.entity().definition() == nullptr)
    issues.push_back(std::make_unique<MissingDefinitionIssue>(node));
}
} // namespace Model
} // namespace TrenchBroom
