#pragma once

#include "Trashed.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace geode::prelude;

class TrashcanPopup : public Popup {
protected:
    ScrollLayer* m_scrollingLayer;
    ListenerHandle m_listener;

    bool init();
    void updateList();

    void onInfo(CCObject* sender);
    void onDelete(CCObject* sender);
    void onRestore(CCObject* sender);
    void onDeleteAll(CCObject* sender);

public:
    static TrashcanPopup* create();
};
