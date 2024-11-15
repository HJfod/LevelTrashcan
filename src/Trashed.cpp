#include "Trashed.hpp"
#include <Geode/loader/Dirs.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <Geode/binding/LocalLevelManager.hpp>
#include <hjfod.gmd-api/include/GMD.hpp>

using namespace geode::prelude;

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

std::filesystem::path getTrashDir() {
    return dirs::getSaveDir() / "trashed-levels";
}

std::string getFreeIDInDir(std::string const& orig, std::filesystem::path const& dir, std::string const& ext) {
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

Trashed::Trashed(std::filesystem::path const& path, GJGameLevel* level) : m_path(path), m_value(level) {}
Trashed::Trashed(std::filesystem::path const& path, GJLevelList* list)  : m_path(path), m_value(list) {}

GJGameLevel* Trashed::asLevel() const {
    if (auto level = std::get_if<Ref<GJGameLevel>>(&m_value)) {
        return *level;
    }
    return nullptr;
}
GJLevelList* Trashed::asList() const {
    if (auto level = std::get_if<Ref<GJLevelList>>(&m_value)) {
        return *level;
    }
    return nullptr;
}

std::string Trashed::getName() const {
    if (asLevel()) {
        return asLevel()->m_levelName;
    }
    else {
        return asList()->m_listName;
    }
}

Trashed::TimePoint Trashed::getTrashTime() const {
    std::error_code ec;
    return std::filesystem::last_write_time(m_path, ec);
}

std::vector<Ref<Trashed>> Trashed::load() {
    std::vector<Ref<Trashed>> trashed;
    for (auto file : file::readDirectory(getTrashDir()).unwrapOrDefault()) {
        if (auto level = gmd::importGmdAsLevel(file)) {
            trashed.push_back(new Trashed(file, *level));
        }
        else if (auto list = gmd::importGmdAsList(file)) {
            trashed.push_back(new Trashed(file, *list));
        }
    }
    return trashed;
}

Result<> Trashed::trash(GJGameLevel* level) {
    (void)file::createDirectoryAll(getTrashDir());
    auto id = getFreeIDInDir(level->m_levelName, getTrashDir(), "gmd");
    auto save = gmd::exportLevelAsGmd(level, getTrashDir() / id);
    if (!save) {
        return Err(save.unwrapErr());
    }
    LocalLevelManager::get()->m_localLevels->removeObject(level);
    UpdateTrashEvent().post();
    return Ok();
}
Result<> Trashed::trash(GJLevelList* list) {
    (void)file::createDirectoryAll(getTrashDir());
    auto id = getFreeIDInDir(list->m_listName, getTrashDir(), "gmdl");
    auto save = gmd::exportListAsGmd(list, getTrashDir() / id);
    if (!save) {
        return Err(save.unwrapErr());
    }
    LocalLevelManager::get()->m_localLists->removeObject(list);
    UpdateTrashEvent().post();
    return Ok();
}
Result<> Trashed::untrash() {
    if (auto level = asLevel()) {
        LocalLevelManager::get()->m_localLevels->insertObject(asLevel(), 0);
    }
    else if (auto list = asList()) {
        LocalLevelManager::get()->m_localLists->insertObject(list, 0);
    }
    std::error_code ec;
    std::filesystem::remove(m_path, ec);
    if (ec) {
        return Err("Unable to delete trashed file: {} (code {})", ec.message(), ec.value());
    }
    UpdateTrashEvent().post();
    return Ok();
}
Result<> Trashed::KABOOM() {
    std::error_code ec;
    std::filesystem::remove(m_path, ec);
    if (ec) {
        return Err("Unable to delete trashed file: {} (code {})", ec.message(), ec.value());
    }
    UpdateTrashEvent().post();
    return Ok();
}

void Trashed::recover(std::filesystem::path const& dirToRecover, size_t& succeeded, size_t& failed) {
    (void)file::createDirectoryAll(getTrashDir());
    for (auto dir : file::readDirectory(dirToRecover).unwrapOrDefault()) {
        std::error_code ec;
        if (std::filesystem::exists(dir / "level.gmd")) {
            std::filesystem::rename(dir / "level.gmd", getTrashDir() / (dir.filename().string() + ".gmd"), ec);
            if (!ec) {
                succeeded += 1;
            }
            else {
                failed += 1;
                log::error("Failed to recover trashed level: {}", ec.message());
            }
        }
        else if (std::filesystem::exists(dir / "list.gmdl")) {
            std::filesystem::rename(dir / "list.gmdl", getTrashDir() / (dir.filename().string() + ".gmdl"), ec);
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
void Trashed::recoverAll() {
    log::info("Recovering trashcan...");
    size_t trashedItems = 0;
    size_t trashedFailed = 0;
    Trashed::recover(dirs::getSaveDir() / "levels" / "trashcan", trashedItems, trashedFailed);
    Trashed::recover(dirs::getSaveDir() / "bettersave.trash", trashedItems, trashedFailed);
    log::info("Recovered {} trashcan items ({} failed)", trashedItems, trashedFailed);
}
