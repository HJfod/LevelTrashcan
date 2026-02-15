#include "TrashcanPopup.hpp"
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <fmt/chrono.h>

static std::string toAgoString(asp::SystemTime const& time) {
    auto const fmtPlural = [](auto count, auto unit) {
        if (count == 1) {
            return fmt::format("{} {} ago", count, unit);
        }
        return fmt::format("{} {}s ago", count, unit);
    };
    auto dur = asp::SystemTime::now().durationSince(time);
    if (dur) {
        if (dur->seconds() < 60) {
            return "Just now";
        }
        if (dur->minutes() < 60) {
            return fmtPlural(dur->minutes(), "minute");
        }
        if (dur->hours() < 24) {
            return fmtPlural(dur->hours(), "hour");
        }
        if (dur->days() < 31) {
            return fmtPlural(dur->days(), "day");
        }
    }
    return time.format("{:%b %d %Y}");
}

class TrashedItemNode : public CCNode {
protected:
    std::shared_ptr<TrashedItem> m_item;

    bool init(std::shared_ptr<TrashedItem> item) {
        if (!CCNode::init())
            return false;
        
        constexpr float SIZE_MULTIPLIER = 1.25f;
        
        this->setContentSize(ccp(300, 30 * SIZE_MULTIPLIER));
        m_item = item;

        auto bg = NineSlice::create("square02b_001.png");
        bg->setColor(ccBLACK);
        bg->setOpacity(90);
        bg->setScale(.5f);
        bg->setContentSize(m_obContentSize / bg->getScale());
        this->addChildAtPosition(bg, Anchor::Center);

        auto title = CCLabelBMFont::create(item->getName().c_str(), "bigFont.fnt");
        title->setScale(.35f * SIZE_MULTIPLIER);
        if (item->isList()) {
            title->setColor({ 0, 255, 0 });
        }
        this->addChildAtPosition(title, Anchor::Left, ccp(5, 7) * SIZE_MULTIPLIER, ccp(0, .5f));

        auto timeIcon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
        timeIcon->setScale(.45f * SIZE_MULTIPLIER);
        this->addChildAtPosition(timeIcon, Anchor::Left, ccp(5, -7) * SIZE_MULTIPLIER, ccp(0, .5f));

        auto time = CCLabelBMFont::create(
            fmt::format("{}", toAgoString(item->getTrashTime())).c_str(),
            "goldFont.fnt"
        );
        time->setScale(.35f * SIZE_MULTIPLIER);
        this->addChildAtPosition(time, Anchor::Left, ccp(20, -7) * SIZE_MULTIPLIER, ccp(0, .5f));

        auto menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(false);
        menu->setContentWidth(100);

        auto restoreSpr = CCSprite::createWithSpriteFrameName("GJ_undoBtn_001.png");
        restoreSpr->setScale(.5f * SIZE_MULTIPLIER);
        auto restoreBtn = CCMenuItemSpriteExtra::create(
            restoreSpr, this, menu_selector(TrashedItemNode::onRestore)
        );
        menu->addChild(restoreBtn);

        auto permDelSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
        permDelSpr->setScale(.4f * SIZE_MULTIPLIER);
        auto permDelBtn = CCMenuItemSpriteExtra::create(
            permDelSpr, this, menu_selector(TrashedItemNode::onPermaDelete)
        );
        menu->addChild(permDelBtn);

        menu->setLayout(
            SimpleRowLayout::create()
                ->setMainAxisAlignment(MainAxisAlignment::End)
                ->setGap(5)
        );
        this->addChildAtPosition(menu, Anchor::Right, ccp(-5, 0) * SIZE_MULTIPLIER, ccp(1, .5f));

        return true;
    }

    void onRestore(CCObject*) {
        createQuickPopup(
            "Delete Permanently",
            fmt::format(
                "Do you want to <cj>restore</c> <cy>{}</c>?\n"
                "This will return it to the top of your created levels list.",
                m_item->getName()
            ),
            "Cancel", "Restore",
            [item = m_item](auto, bool btn2) {
                if (btn2) {
                    auto res = Trashcan::get()->untrash(item);
                    if (res) {
                        Notification::create(
                            fmt::format("Restored {}", item->getName()),
                            NotificationIcon::Success
                        )->show();
                    }
                    else {
                        FLAlertLayer::create("Unable to Restore", res.unwrapErr(), "OK");
                    }
                }
            }
        );
    }
    void onPermaDelete(CCObject*) {
        createQuickPopup(
            "Delete Permanently",
            fmt::format(
                "Are you SURE you want to <cr>permanently delete</c> <cy>{}</c>?\n"
                "<co>THIS ACTION IS IRREVERSIBLE!</c>",
                m_item->getName()
            ),
            "Cancel", "Delete",
            [item = m_item](auto, bool btn2) {
                if (btn2) {
                    auto res = Trashcan::get()->deletePermanently(item);
                    if (res) {
                        Notification::create(fmt::format("Deleted {}", item->getName()))->show();
                    }
                    else {
                        FLAlertLayer::create("Unable to Delete", res.unwrapErr(), "OK");
                    }
                }
            }
        );
    }

public:
    static TrashedItemNode* create(std::shared_ptr<TrashedItem> item) {
        auto ret = new TrashedItemNode();
        if (ret && ret->init(item)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

bool TrashcanPopup::init() {
    if (!Popup::init(350, 270))
        return false;

    this->setTitle("Trashcan");

    auto trashcanSpr = CCSprite::createWithSpriteFrameName("edit_delBtn_001.png");
    trashcanSpr->setScale(.7f);
    m_mainLayer->addChildAtPosition(trashcanSpr, Anchor::Top, ccp(-55, -20));

    constexpr CCSize SCROLL_SIZE = ccp(300, 180);

    auto scrollBG = CCLayerColor::create(ccc4(0, 0, 0, 90));
    scrollBG->setContentSize(SCROLL_SIZE + ccp(15, 15));
    scrollBG->ignoreAnchorPointForPosition(false);
    m_mainLayer->addChildAtPosition(scrollBG, Anchor::Center, ccp(0, 0), ccp(.5f, .5f));

    m_scrollingLayer = ScrollLayer::create(SCROLL_SIZE);
    m_scrollingLayer->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout());
    m_mainLayer->addChildAtPosition(m_scrollingLayer, Anchor::Center, -m_scrollingLayer->getContentSize() / 2);

    auto border = ListBorders::create();
    border->setContentSize(m_scrollingLayer->getContentSize() + ccp(15, 15));
    m_mainLayer->addChildAtPosition(border, Anchor::Center);

    auto deleteAllSpr = CCSprite::createWithSpriteFrameName("GJ_resetBtn_001.png");
    auto deleteAllBtn = CCMenuItemSpriteExtra::create(
        deleteAllSpr, this, menu_selector(TrashcanPopup::onDeleteAll)
    );
    m_buttonMenu->addChildAtPosition(deleteAllBtn, Anchor::BottomLeft, ccp(20, 20));

    m_listener = UpdateTrashEvent().listen([this]() {
        this->updateList();
    });
    this->updateList();
    
    return true;
}

void TrashcanPopup::updateList() {
    m_scrollingLayer->m_contentLayer->removeAllChildren();
    for (auto const& item : Trashcan::get()->getItems()) {
        m_scrollingLayer->m_contentLayer->addChild(TrashedItemNode::create(item));
    }
    m_scrollingLayer->m_contentLayer->updateLayout();
    m_scrollingLayer->moveToTop();

    // This is because updating the LevelBrowserLayer underneath causes it to take touch priority
    handleTouchPriority(this);
}

void TrashcanPopup::onDeleteAll(CCObject*) {
    createQuickPopup(
        "Clear Trashcan",
        fmt::format(
            "Are you sure you want to <co>clear the Trashcan</c>?\n"
            "<cr>This will PERMANENTLY delete ALL {} levels in the trash!</c>",
            Trashcan::get()->getItems().size()
        ),
        "Cancel", "Delete All",
        [this](auto*, bool btn2) {
            if (btn2) {
                auto res = Trashcan::get()->deleteAllPermanently();
                if (!res) {
                    FLAlertLayer::create("Failed to Clear", res.unwrapErr(), "OK")->show();
                }
                this->onClose(nullptr);
            }
        }
    );
}

TrashcanPopup* TrashcanPopup::create() {
    auto ret = new TrashcanPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
