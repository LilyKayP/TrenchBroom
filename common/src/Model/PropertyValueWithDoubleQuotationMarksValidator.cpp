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

#include "PropertyValueWithDoubleQuotationMarksValidator.h"

#include "Model/BrushNode.h"
#include "Model/Entity.h"
#include "Model/EntityNode.h"
#include "Model/Issue.h"
#include "Model/RemoveEntityPropertiesQuickFix.h"
#include "Model/TransformEntityPropertiesQuickFix.h"

#include <kdl/string_utils.h>

#include <string>
#include <vector>

namespace TrenchBroom {
namespace Model {
class PropertyValueWithDoubleQuotationMarksValidator::PropertyValueWithDoubleQuotationMarksIssue
  : public EntityPropertyIssue {
public:
  static const IssueType Type;

private:
  const std::string m_propertyKey;

public:
  PropertyValueWithDoubleQuotationMarksIssue(EntityNodeBase& node, const std::string& propertyKey)
    : EntityPropertyIssue(node)
    , m_propertyKey(propertyKey) {}

  const std::string& propertyKey() const override { return m_propertyKey; }

private:
  IssueType doGetType() const override { return Type; }

  std::string doGetDescription() const override {
    return "The value of entity property '" + m_propertyKey +
           "' contains double quotation marks. This may cause errors during compilation or in the "
           "game.";
  }
};

const IssueType
  PropertyValueWithDoubleQuotationMarksValidator::PropertyValueWithDoubleQuotationMarksIssue::Type =
    Issue::freeType();

PropertyValueWithDoubleQuotationMarksValidator::PropertyValueWithDoubleQuotationMarksValidator()
  : Validator(PropertyValueWithDoubleQuotationMarksIssue::Type, "Invalid entity property values") {
  addQuickFix(std::make_unique<RemoveEntityPropertiesQuickFix>(
    PropertyValueWithDoubleQuotationMarksIssue::Type));
  addQuickFix(std::make_unique<TransformEntityPropertiesQuickFix>(
    PropertyValueWithDoubleQuotationMarksIssue::Type, "Replace \" with '",
    [](const std::string& key) {
      return key;
    },
    [](const std::string& value) {
      return kdl::str_replace_every(value, "\"", "'");
    }));
}

void PropertyValueWithDoubleQuotationMarksValidator::doValidate(
  EntityNodeBase& node, std::vector<std::unique_ptr<Issue>>& issues) const {
  for (const EntityProperty& property : node.entity().properties()) {
    const std::string& propertyKey = property.key();
    const std::string& propertyValue = property.value();
    if (propertyValue.find('"') != std::string::npos) {
      issues.push_back(
        std::make_unique<PropertyValueWithDoubleQuotationMarksIssue>(node, propertyKey));
    }
  }
}
} // namespace Model
} // namespace TrenchBroom
