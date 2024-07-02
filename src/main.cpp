#include <Geode/DefaultInclude.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <hjfod.gmd-api/include/GMD.hpp>

using namespace geode::prelude;

struct RecoveryStats final {
	size_t recoveredLevels = 0;
	size_t duplicateLevels = 0;
	size_t failedLevels = 0;

	size_t recoveredLists = 0;
	size_t duplicateLists = 0;
	size_t failedLists = 0;
};

static std::vector<std::string> recoverLevelOrder(std::filesystem::path const& file, std::string const& key) {
	// Try to load metadata, but don't hard-fail if not possible
	auto metaRes = file::readFromJson<matjson::Value>(file);
	if (!metaRes) {
        return std::vector<std::string>();
	}
    return matjson::Serialize<std::vector<std::string>>::from_json(metaRes->as_object()[key]);
}

static RecoveryStats recoverOldBS() {
	auto oldSaveDir = dirs::getSaveDir() / "levels";

    RecoveryStats stats;
    auto llm = LocalLevelManager::get();
	auto recovered = Ref(CCArray::create());

	log::info("Recovering lost levels...");
	for (auto dir : file::readDirectory(oldSaveDir / "created").unwrapOrDefault()) {
		if (std::filesystem::exists(dir / "level.gmd")) {
			auto levelRes = gmd::importGmdAsLevel(dir / "level.gmd");
			if (!levelRes) {
                stats.failedLevels += 1;
				log::error("Unable to recover level '{}': {}", dir.filename(), levelRes.unwrapErr());
                continue;
			}
            auto level = *levelRes;
            for (auto existing : CCArrayExt<GJGameLevel*>(llm->m_localLevels)) {
                if (existing->m_levelString == level->m_levelString) {
                    stats.duplicateLevels += 1;
				    log::warn("Skipping duplicate level '{}' (duplicate of '{}')", level->m_levelName, existing->m_levelName);
                    goto continue_outer_level_loop;
                }
            }
			// BetterSave may have duplicated stuff in the past already
            for (auto existing : CCArrayExt<GJGameLevel*>(recovered)) {
                if (existing->m_levelString == level->m_levelString) {
                    stats.duplicateLevels += 1;
				    log::warn("Skipping duplicate level '{}' (duplicate of already recovered level '{}')", level->m_levelName, existing->m_levelName);
                    goto continue_outer_level_loop;
                }
            }
            level->setID(dir.filename().string());
            recovered->addObject(level);
            stats.recoveredLevels += 1;
		}
        continue_outer_level_loop:;
	}

    auto levelsOrder = recoverLevelOrder(oldSaveDir / "created" / "metadata.json", "level-order");
	auto levels = recovered->data;
	std::sort(
		levels->arr, levels->arr + levels->num,
		[&levelsOrder](CCObject* first, CCObject* second) -> bool {
			return ranges::indexOf(levelsOrder, static_cast<GJGameLevel*>(first)->getID()) < 
				ranges::indexOf(levelsOrder, static_cast<GJGameLevel*>(second)->getID());
		}
	);

	// Add the recovered levels to the start of the list
	recovered->addObjectsFromArray(llm->m_localLevels);
	llm->m_localLevels->removeAllObjects();
	llm->m_localLevels->addObjectsFromArray(recovered);

	log::info("Recovered {} levels ({} duplicates, {} failed)", stats.recoveredLevels, stats.duplicateLevels, stats.failedLevels);

	// Reuse the same array for lists
	recovered->removeAllObjects();

	log::info("Recovering lost lists...");
	for (auto dir : file::readDirectory(oldSaveDir / "lists").unwrapOrDefault()) {
		if (std::filesystem::exists(dir / "list.gmdl")) {
			auto listRes = gmd::importGmdAsList(dir / "list.gmdl");
			if (!listRes) {
                stats.failedLists += 1;
				log::error("Unable to recover list '{}': {}", dir.filename(), listRes.unwrapErr());
                continue;
			}
            auto list = *listRes;
            for (auto existing : CCArrayExt<GJLevelList*>(llm->m_localLists)) {
                if (std::vector<int>(existing->m_levels) == std::vector<int>(list->m_levels)) {
                    stats.duplicateLists += 1;
				    log::warn("Skipping duplicate list '{}' (duplicate of '{}')", list->m_listName, existing->m_listName);
                    goto continue_outer_list_loop;
                }
            }
			// BetterSave may have duplicated stuff in the past already
            for (auto existing : CCArrayExt<GJLevelList*>(recovered)) {
                if (std::vector<int>(existing->m_levels) == std::vector<int>(list->m_levels)) {
                    stats.duplicateLists += 1;
				    log::warn("Skipping duplicate list '{}' (duplicate of already recovered list '{}')", list->m_listName, existing->m_listName);
                    goto continue_outer_list_loop;
                }
            }
            recovered->addObject(list);
            stats.recoveredLists += 1;
		}
        continue_outer_list_loop:;
	}

    auto listsOrder = recoverLevelOrder(oldSaveDir / "lists" / "metadata.json", "list-order");
	auto lists = recovered->data;
	std::sort(
		lists->arr, lists->arr + lists->num,
		[&listsOrder](CCObject* first, CCObject* second) -> bool {
			return ranges::indexOf(listsOrder, static_cast<GJLevelList*>(first)->getID()) < 
				ranges::indexOf(listsOrder, static_cast<GJLevelList*>(second)->getID());
		}
	);

	// Add the recovered levels to the start of the list
	recovered->addObjectsFromArray(llm->m_localLists);
	llm->m_localLists->removeAllObjects();
	llm->m_localLists->addObjectsFromArray(recovered);

	log::info("Recovered {} lists ({} duplicates, {} failed)", stats.recoveredLists, stats.duplicateLists, stats.failedLists);

    (void)file::writeString(oldSaveDir / ".recovered-by-new-bettersave", "");

    return stats;
}

struct $modify(MenuLayer) {
    $override
    bool init() {
        if (!MenuLayer::init())
            return false;
        
	    auto oldSaveDir = dirs::getSaveDir() / "levels";
        if (
            std::filesystem::exists(oldSaveDir) &&
            !std::filesystem::exists(oldSaveDir / ".recovered-by-new-bettersave")
        ) {
            auto stats = recoverOldBS();
            auto alert = FLAlertLayer::create(
                nullptr,
                "Levels Recovered",
                fmt::format(
                    "<cy>BetterSave Recovery</c> has <cp>recovered</c> the following lost levels:\n"
                    "<cg>{}</c> levels (<cr>{}</c> duplicates skipped, <cr>{}</c> failed to recover)\n"
                    "<cj>{}</c> lists (<cr>{}</c> duplicates skipped, <cr>{}</c> failed to recover)\n",
                    stats.recoveredLevels, stats.duplicateLevels, stats.failedLevels,
                    stats.recoveredLists, stats.duplicateLists, stats.failedLists
                ),
                "OK", nullptr, 360
            );
            alert->m_scene = this;
            alert->show();
        }

        return true;
    }
};
