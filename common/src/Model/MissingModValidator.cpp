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

#include "MissingModValidator.h"

#include "IO/Path.h"
#include "Model/Entity.h"
#include "Model/EntityNodeBase.h"
#include "Model/EntityProperties.h"
#include "Model/Game.h"
#include "Model/Issue.h"
#include "Model/IssueQuickFix.h"
#include "Model/MapFacade.h"
#include "Model/PushSelection.h"

#include <kdl/memory_utils.h>
#include <kdl/vector_utils.h>

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace TrenchBroom {
namespace Model {
class MissingModValidator::MissingModIssue : public Issue {
public:
  static const IssueType Type;

private:
  std::string m_mod;
  std::string m_message;

public:
  MissingModIssue(EntityNodeBase& node, const std::string& mod, const std::string& message)
    : Issue(node)
    , m_mod(mod)
    , m_message(message) {}

private:
  IssueType doGetType() const override { return Type; }

  std::string doGetDescription() const override {
    return "Mod '" + m_mod + "' could not be used: " + m_message;
  }

public:
  const std::string& mod() const { return m_mod; }
};

const IssueType MissingModValidator::MissingModIssue::Type = Issue::freeType();

class MissingModValidator::MissingModIssueQuickFix : public IssueQuickFix {
public:
  MissingModIssueQuickFix()
    : IssueQuickFix(MissingModIssue::Type, "Remove mod") {}

private:
  void doApply(MapFacade* facade, const std::vector<const Issue*>& issues) const override {
    const PushSelection pushSelection(facade);

    // If nothing is selected, property changes will affect only world.
    facade->deselectAll();

    const std::vector<std::string> oldMods = facade->mods();
    const std::vector<std::string> newMods = removeMissingMods(oldMods, issues);
    facade->setMods(newMods);
  }

  std::vector<std::string> removeMissingMods(
    std::vector<std::string> mods, const std::vector<const Issue*>& issues) const {
    for (const Issue* issue : issues) {
      if (issue->type() == MissingModIssue::Type) {
        const MissingModIssue* modIssue = static_cast<const MissingModIssue*>(issue);
        const std::string& missingMod = modIssue->mod();
        mods = kdl::vec_erase(std::move(mods), missingMod);
      }
    }
    return mods;
  }
};

MissingModValidator::MissingModValidator(std::weak_ptr<Game> game)
  : Validator(MissingModIssue::Type, "Missing mod directory")
  , m_game(std::move(game)) {
  addQuickFix(std::make_unique<MissingModIssueQuickFix>());
}

void MissingModValidator::doValidate(
  EntityNodeBase& node, std::vector<std::unique_ptr<Issue>>& issues) const {
  if (node.entity().classname() != EntityPropertyValues::WorldspawnClassname) {
    return;
  }

  if (kdl::mem_expired(m_game)) {
    return;
  }

  auto game = kdl::mem_lock(m_game);
  const std::vector<std::string> mods = game->extractEnabledMods(node.entity());

  if (mods == m_lastMods) {
    return;
  }

  const auto additionalSearchPaths = IO::Path::asPaths(mods);
  const auto errors = game->checkAdditionalSearchPaths(additionalSearchPaths);

  for (const auto& [searchPath, message] : errors) {
    issues.push_back(std::make_unique<MissingModIssue>(node, searchPath.asString(), message));
  }

  m_lastMods = mods;
}
} // namespace Model
} // namespace TrenchBroom
