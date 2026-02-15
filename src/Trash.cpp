#include "Trash.hpp"
#include <Geode/binding/LocalLevelManager.hpp>
#include <hjfod.gmd-api/include/GMD.hpp>

static std::string convertToKebabCase(std::string const& str) {
	std::string res {};
	char last = '\0';
	for (auto c : str) {
		// Add a dash if the character is in uppercase (camelCase / PascalCase) 
		// or a space (Normal case) or an underscore (snake_case) and the 
		// built result string isn't empty and make sure there's only a 
		// singular dash
		// Don't add a dash if the previous character was also uppercase or a 
		// number (SCREAM1NG L33TCASE should be just scream1ng-l33tcase)
		if ((std::isupper(c) && !(std::isupper(last) || std::isdigit(last))) || std::isspace(c) || c == '_') {
			if (res.size() && res.back() != '-') {
				res.push_back('-');
			}
		}
		// Only preserve alphanumeric characters
		if (std::isalnum(c)) {
			res.push_back(std::tolower(c));
		}
		last = c;
	}
	// If there is a dash at the end (for example because the name ended in a 
	// space) then get rid of that
	if (res.back() == '-') {
		res.pop_back();
	}
	return res;
}
static void checkReservedFilenames(std::string& name) {
    switch (hash(name.c_str())) {
        case hash("con"): case hash("prn"): case hash("aux"): case hash("nul"):
        // This was in https://www.boost.org/doc/libs/1_36_0/libs/filesystem/doc/portability_guide.htm?
        // Never heard of it before though
        case hash("clock$"):
        case hash("com1"): case hash("com2"): case hash("com3"): case hash("com4"):
        case hash("com5"): case hash("com6"): case hash("com7"): case hash("com8"): case hash("com9"):
        case hash("lpt1"): case hash("lpt2"): case hash("lpt3"): case hash("lpt4"):
        case hash("lpt5"): case hash("lpt6"): case hash("lpt7"): case hash("lpt8"): case hash("lpt9"):
        {
            name += "-0";
        }
        break;

        default: {} break;
    }
}

// Recover some of the old formats like BetterSave n stuff
static void recoverOldTrashcan(std::filesystem::path const& dirToRecover, size_t& succeeded, size_t& failed) {
    auto trashDir = Trashcan::get()->getTrashDir();
    (void)file::createDirectoryAll(trashDir);
    for (auto dir : file::readDirectory(dirToRecover).unwrapOrDefault()) {
        std::error_code ec;
        if (std::filesystem::exists(dir / "level.gmd")) {
            std::filesystem::rename(dir / "level.gmd", trashDir / (dir.filename().string() + ".gmd"), ec);
            if (!ec) {
                succeeded += 1;
            }
            else {
                failed += 1;
                log::error("Failed to recover trashed level: {}", ec.message());
            }
        }
        else if (std::filesystem::exists(dir / "list.gmdl")) {
            std::filesystem::rename(dir / "list.gmdl", trashDir / (dir.filename().string() + ".gmdl"), ec);
            if (!ec) {
                succeeded += 1;
            }
            else {
                failed += 1;
                log::error("Failed to recover trashed list: {}", ec.message());
            }
        }
    }
}

std::string TrashedItem::getName() const {
    return std::visit(makeVisitor {
        [](GJGameLevel* level) {
            return level->m_levelName;
        },
        [](GJLevelList* list) {
            return list->m_listName;
        }
    }, m_levelOrList);
}
asp::SystemTime TrashedItem::getTrashTime() const {
    return m_trashTime;
}
bool TrashedItem::isList() const {
    return std::holds_alternative<Ref<GJLevelList>>(m_levelOrList);
}

Trashcan* Trashcan::get() {
    static auto ret = new Trashcan();
    return ret;
}

Result<std::shared_ptr<TrashedItem>> Trashcan::loadItem(
    std::filesystem::path const& path,
    std::optional<asp::SystemTime> knownTime
) {
    // Load time, using last file write time as fallback
    std::error_code ec;
    auto time = knownTime ? *knownTime : asp::SystemTime::fromUnix(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::filesystem::last_write_time(path, ec).time_since_epoch()
            // I will personally serve Bill Gates a really lousy plate of spaghetti
            GEODE_WINDOWS(- std::chrono::years(369))
        ).count()
    );

    std::shared_ptr<TrashedItem> item;
    if (path.extension() == ".gmd") {
        auto level = gmd::importGmdAsLevel(path);
        if (!level) {
            return Err(
                "Unable to read trashed level {} from metadata: {}",
                path, level.unwrapErr()
            );
        }
        item = std::make_shared<TrashedItem>(level.unwrap(), path, time);
    }
    else if (path.extension() == ".gmdl") {
        auto list = gmd::importGmdAsList(path);
        if (!list) {
            return Err(
                "Unable to read trashed list {} from metadata: {}",
                path, list.unwrapErr()
            );
        }
        item = std::make_shared<TrashedItem>(list.unwrap(), path, time);
    }
    else {
        return Err("Unknown file type in trashed level {} from metadata", path);
    }

    m_items.push_back(item);
    return Ok(item);
}
void Trashcan::load() {
    std::unordered_set<std::string> succesfullyLoadedWithMetadata;

    log::info("Recovering old trashcan mod files...");
    size_t trashedItems = 0;
    size_t trashedFailed = 0;
    recoverOldTrashcan(dirs::getSaveDir() / "levels" / "trashcan", trashedItems, trashedFailed);
    recoverOldTrashcan(dirs::getSaveDir() / "bettersave.trash", trashedItems, trashedFailed);
    log::info("Recovered {} trashcan items ({} failed)", trashedItems, trashedFailed);

    // First load levels by going through metadata.json and loading everything 
    // listed there (new proper format)
    log::info("Loading trashed levels");
    auto meta = file::readJson(this->getTrashDir() / "metadata.json");
    if (meta.isOk()) {
        auto json = std::move(meta).unwrap();
        if (auto trashTimes = json.get("trash-times")) {
            for (auto&& [key, time] : std::move(trashTimes).unwrap()) {
                if (succesfullyLoadedWithMetadata.contains(key)) {
                    log::warn(
                        "File {} was included multiple times in trash metadata? "
                        "That's odd", key
                    );
                    continue;
                }
                auto timeNum = time.asUInt();
                auto load = this->loadItem(this->getTrashDir() / key, timeNum ?
                    std::optional(asp::SystemTime::fromUnix(timeNum.unwrap())) :
                    std::nullopt
                );
                if (!load) {
                    log::error("{}", load.unwrapErr());
                    continue;
                }
                succesfullyLoadedWithMetadata.insert(key);
            }
        }
    }

    // Then fallback check for any levels not covered by metadata.json
    for (auto path : file::readDirectory(this->getTrashDir()).unwrapOrDefault()) {
        auto key = string::pathToString(path.filename());
        if (succesfullyLoadedWithMetadata.contains(key)) {
            continue;
        }
        (void)this->loadItem(path, std::nullopt);
    }

    // Sort items by trash time
    std::sort(
        m_items.begin(), m_items.end(),
        [](std::shared_ptr<TrashedItem> a, std::shared_ptr<TrashedItem> b) {
            return a->m_trashTime > b->m_trashTime;
        }
    );

    // Save any new levels we found
    this->saveMetadata();
}
void Trashcan::saveMetadata() {
    auto trashTimes = matjson::Value::object();
    for (auto const& item : m_items) {
        trashTimes[string::pathToString(item->m_path.filename())] = item->m_trashTime.timeSinceEpoch().seconds();
    }
    (void)file::writeString(this->getTrashDir() / "metadata.json", matjson::makeObject({
        { "trash-times", trashTimes },
    }).dump());
}

std::filesystem::path Trashcan::getTrashDir() const {
    return dirs::getSaveDir() / "trashed-levels";
}
std::string Trashcan::getFreeID(ZStringView orig, ZStringView ext) {
    const auto dir = this->getTrashDir();

    (void)file::createDirectoryAll(dir);

    // Synthesize an ID for the level by taking the level name in kebab-case 
    // and then adding an incrementing number at the end until there exists 
    // no folder with the same name already
    auto name = convertToKebabCase(orig);
    
    // Prevent names that are too long (some people might use input bypass 
    // to give levels absurdly long names)
    if (name.size() > 20) {
        name = name.substr(0, 20);
    }
    if (name.empty()) {
        name = "unnamed";
    }

    // Check that no one has made a level called CON
    checkReservedFilenames(name);

    auto id = name + "." + ext;
    size_t counter = 0;
    while (std::filesystem::exists(dir / id)) {
        id = fmt::format("{}-{}", name, counter);
        counter += 1;
    }
    return id;
}

std::vector<std::shared_ptr<TrashedItem>> const& Trashcan::getItems() const {
    return m_items;
}

Result<std::shared_ptr<TrashedItem>> Trashcan::trash(GJGameLevel* level) {
    auto path = getTrashDir() / this->getFreeID(level->m_levelName, "gmd");
    auto save = gmd::exportLevelAsGmd(level, path);
    if (!save) {
        return Err(save.unwrapErr());
    }
    auto item = std::make_shared<TrashedItem>(level, path, asp::SystemTime::now());
    m_items.insert(m_items.begin(), item);

    LocalLevelManager::get()->m_localLevels->removeObject(level);
    this->saveMetadata();
    UpdateTrashEvent().send();
    return Ok(item);
}
Result<std::shared_ptr<TrashedItem>> Trashcan::trash(GJLevelList* list) {
    auto path = getTrashDir() / getFreeID(list->m_listName, "gmdl");
    auto save = gmd::exportListAsGmd(list, path);
    if (!save) {
        return Err(save.unwrapErr());
    }
    auto item = std::make_shared<TrashedItem>(list, path, asp::SystemTime::now());
    m_items.insert(m_items.begin(), item);

    LocalLevelManager::get()->m_localLists->removeObject(list);
    this->saveMetadata();
    UpdateTrashEvent().send();
    return Ok(item);
}

Result<> Trashcan::untrash(std::shared_ptr<TrashedItem> item) {
    std::visit(makeVisitor {
        [](GJGameLevel* level) {
            LocalLevelManager::get()->m_localLevels->insertObject(level, 0);
        },
        [](GJLevelList* list) {
            LocalLevelManager::get()->m_localLists->insertObject(list, 0);
        }
    }, item->m_levelOrList);

    std::error_code ec;
    std::filesystem::remove(item->m_path, ec);
    if (ec) {
        return Err("Unable to delete trashed file: {} (code {})", ec.message(), ec.value());
    }
    std::erase(m_items, item);

    UpdateTrashEvent().send();
    this->saveMetadata();
    return Ok();
}
Result<> Trashcan::deletePermanently(std::shared_ptr<TrashedItem> item) {
    std::error_code ec;
    std::filesystem::remove(item->m_path, ec);
    if (ec) {
        return Err("Unable to delete trashed file: {} (code {})", ec.message(), ec.value());
    }
    std::erase(m_items, item);
    this->saveMetadata();
    UpdateTrashEvent().send();
    return Ok();
}
Result<> Trashcan::deleteAllPermanently() {
    std::error_code ec;
    std::filesystem::remove_all(this->getTrashDir(), ec);
    if (ec) {
        return Err("Unable to clear trashcan: {} (code {})", ec.message(), ec.value());
    }
    m_items.clear();
    this->saveMetadata();
    UpdateTrashEvent().send();
    return Ok();
}
