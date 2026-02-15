#pragma once

#include <variant>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <asp/time/SystemTime.hpp>

using namespace geode::prelude;

class UpdateTrashEvent : public Event<UpdateTrashEvent, bool()> {};

class Trashcan;

class TrashedItem final {
protected:
    std::variant<Ref<GJGameLevel>, Ref<GJLevelList>> m_levelOrList;
    std::filesystem::path m_path;
    asp::SystemTime m_trashTime;

    friend class Trashcan;

public:
    TrashedItem(auto levelOrList, std::filesystem::path const& path, asp::SystemTime time)
      : m_levelOrList(levelOrList), m_path(path), m_trashTime(time) {}

    std::string getName() const;
    asp::SystemTime getTrashTime() const;

    bool isList() const;
};

class Trashcan final {
protected:
    std::vector<std::shared_ptr<TrashedItem>> m_items;

    void saveMetadata();
    Result<std::shared_ptr<TrashedItem>> loadItem(
        std::filesystem::path const& path,
        std::optional<asp::SystemTime> time
    );

public:
    static Trashcan* get();

    void load();

    std::filesystem::path getTrashDir() const;
    std::string getFreeID(ZStringView name, ZStringView ext);

    std::vector<std::shared_ptr<TrashedItem>> const& getItems() const;

    Result<std::shared_ptr<TrashedItem>> trash(GJGameLevel* level);
    Result<std::shared_ptr<TrashedItem>> trash(GJLevelList* list);

    Result<> untrash(std::shared_ptr<TrashedItem> item);
    Result<> deletePermanently(std::shared_ptr<TrashedItem> item);
    Result<> deleteAllPermanently();
};
