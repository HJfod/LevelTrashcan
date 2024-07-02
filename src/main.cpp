#include <Geode/DefaultInclude.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/loader/Dirs.hpp>
#include "Trashed.hpp"
#include "TrashcanPopup.hpp"

using namespace geode::prelude;

struct $modify(GameLevelManager) {
	$override
	void deleteLevel(GJGameLevel* level) {
		if (level->m_levelType == GJLevelType::Editor) {
			auto res = Trashed::trash(level);
			if (!res) {
				FLAlertLayer::create(
					"Error Trashing Level",
					fmt::format("Unable to move level to trash: {}", res.unwrapErr()),
					"OK"
				)->show();
			}
			return;
		}
		GameLevelManager::deleteLevel(level);
	}
	$override
	void deleteLevelList(GJLevelList* list) {
		if (list->m_listType == GJLevelType::Editor) {
			auto res = Trashed::trash(list);
			if (!res) {
				FLAlertLayer::create(
					"Error Trashing List",
					fmt::format("Unable to move list to trash: {}", res.unwrapErr()),
					"OK"
				)->show();
			}
			return;
		}
		GameLevelManager::deleteLevelList(list);
	}
};

class $modify(TrashBrowserLayer, LevelBrowserLayer) {
    struct Fields {
        EventListener<EventFilter<UpdateTrashEvent>> listener;
    };

	$override
    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search))
            return false;
        
        if (search->m_searchType == SearchType::MyLevels) {
            if (auto menu = this->getChildByID("my-levels-menu")) {
                auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
                auto trashBtn = CCMenuItemSpriteExtra::create(
                    trashSpr, this, menu_selector(TrashBrowserLayer::onTrashcan)
                );
                menu->addChild(trashBtn);
                menu->updateLayout();

                auto updateTrashSprite = [trashSpr] {
                    std::error_code ec;
                    auto finnsTrashed = !std::filesystem::is_empty(getTrashDir(), ec) && !ec;
                    trashSpr->setOpacity(finnsTrashed ? 255 : 205);
                    trashSpr->setColor(finnsTrashed ? ccWHITE : ccc3(90, 90, 90));
                };
                m_fields->listener.bind([=, this](auto*) {
                    updateTrashSprite();
                    // Reload levels list
                    this->loadPage(m_searchObject);
                    return ListenerResult::Propagate;
                });
                updateTrashSprite();
            }
        }
        return true;
    }
    void onTrashcan(CCObject*) {
        std::error_code ec;
        auto finnsTrashed = !std::filesystem::is_empty(getTrashDir(), ec) && !ec;
        if (finnsTrashed) {
            TrashcanPopup::create()->show();
        }
        else {
            FLAlertLayer::create(
                "Trash is Empty",
                "You have not <co>trashed</c> any levels!",
                "OK"
		    )->show();
        }
    }
};

class $modify(EditLevelLayer) {
	$override
    void confirmDelete(CCObject*) {
        auto alert = FLAlertLayer::create(
            this,
            "Trash level", 
            "Are you sure you want to <cr>trash</c> this level?\n"
            "<cy>You can restore the level or permanently delete it through the Trashcan.</c>",
            "Cancel", "Trash",
            340
        );
        alert->setTag(4);
		alert->m_button2->updateBGImage("GJ_button_06.png");
        alert->show();
    }
};
class $modify(LevelBrowserLayer) {
	$override
    void onDeleteSelected(CCObject* sender) {
		if (m_searchObject->m_searchType == SearchType::MyLevels) {
			size_t count = 0;
			for (auto level : CCArrayExt<GJGameLevel*>(m_levels)) {
				if (level->m_selected) {
					count += 1;
				}
			}
			if (count > 0) {
				auto alert = FLAlertLayer::create(
					this,
					"Trash levels", 
					fmt::format(
						"Are you sure you want to <cr>trash</c> the <cp>{0}</c> selected level{1}?\n"
						"<cy>You can restore the level{1} or permanently delete {2} through the Trashcan.</c>",
						count, (count == 1 ? "" : "s"), (count == 1 ? "it" : "them")
					),
					"Cancel", "Trash",
					340
				);
				alert->setTag(5);
				alert->m_button2->updateBGImage("GJ_button_06.png");
				alert->show();
			}
			else {
				FLAlertLayer::create("Nothing here...", "No levels selected.", "OK")->show();
			}
		}
		else {
			return LevelBrowserLayer::onDeleteSelected(sender);
		}
    }
};
class $modify(MenuLayer) {
	bool init() {
		if (!MenuLayer::init())
			return false;
		
		Trashed::recoverAll();

		return true;
	}
};
