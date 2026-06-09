#include "ll/api/mod/NativeMod.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/form/CustomForm.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "fmt/format.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <vector>
#include <string>

namespace ikft {

using json = nlohmann::json;

json getDb() {
    std::ifstream f("plugins/IK-FloatingText/data.json");
    if (!f.is_open()) return json::object();
    json j;
    try {
        f >> j;
    } catch (...) {
        j = json::object();
    }
    return j;
}

void saveDb(json const& j) {
    std::filesystem::create_directories("plugins/IK-FloatingText");
    std::ofstream f("plugins/IK-FloatingText/data.json");
    f << j.dump(4);
}

void sendMainMenu(Player& player);
void sendCreateForm(Player& player);
void sendManageListForm(Player& player);
void sendOperationMenu(Player& player, std::string const& ftId);
void sendEditForm(Player& player, std::string const& ftId);
void sendAddLineForm(Player& player, std::string const& ftId);

void sendMainMenu(Player& player) {
    ll::form::SimpleForm form("悬浮字管理主菜单", "请选择操作：");

    form.appendButton("➕ 创建悬浮字")
        .appendButton("⚙️ 管理悬浮字")
        .sendTo(player, [](Player& p, int selected, ll::form::FormCancelReason) {
            if (selected == -1) return;
            if (selected == 0) {
                sendCreateForm(p);
            } else if (selected == 1) {
                sendManageListForm(p);
            }
        });
}

void sendCreateForm(Player& player) {
    ll::form::CustomForm form("创建悬浮字");

    auto pos = player.getPosition();
    int dimId = player.getDimensionId().id;

    form.appendInput("ft_id", "悬浮字唯一ID (留空自动生成)", "", "")
        .appendInput("ft_text", "第一行文本内容", "", "")
        .appendInput("ft_x", "X 坐标", "", std::to_string(pos.x))
        .appendInput("ft_y", "Y 坐标", "", std::to_string(pos.y))
        .appendInput("ft_z", "Z 坐标", "", std::to_string(pos.z))
        .appendInput("ft_dim", "当前维度", "", std::to_string(dimId))
        .appendInput("ft_spacing", "行距", "", "0.3");

    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendMainMenu(p);
            return;
        }

        auto& data = *result;
        std::string id = std::get<std::string>(data.at("ft_id"));
        std::string text = std::get<std::string>(data.at("ft_text"));
        double x = std::stod(std::get<std::string>(data.at("ft_x")));
        double y = std::stod(std::get<std::string>(data.at("ft_y")));
        double z = std::stod(std::get<std::string>(data.at("ft_z")));
        int dim = std::stoi(std::get<std::string>(data.at("ft_dim")));
        double spacing = std::stod(std::get<std::string>(data.at("ft_spacing")));

        if (id.empty()) {
            id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        }

        json db = getDb();
        if (db.contains(id)) {
            p.sendMessage("§c创建失败：该悬浮字 ID 已存在");
            sendMainMenu(p);
            return;
        }

        Vec3 spawnPos{x, y, z};
        auto* blockSource = &p.getDimension().getBlockSourceFromMainChunkSource();
        auto* level = p.getLevel();

        ActorDefinitionIdentifier def("minecraft:armor_stand");
        Actor* entity = level->addEntity(*blockSource, def, spawnPos);

        if (entity) {
            entity->setNameTag(text);
            entity->setNameTagVisible(true);
            entity->setInvisible(true);

            json ftData;
            ftData["x"] = x;
            ftData["y"] = y;
            ftData["z"] = z;
            ftData["dimid"] = dim;
            ftData["spacing"] = spacing;

            json lineObj;
            lineObj["text"] = text;
            lineObj["uuid"] = entity->getOrCreateUniqueID().rawID;
            ftData["lines"].push_back(lineObj);

            db[id] = ftData;
            saveDb(db);

            p.sendMessage("§a悬浮字创建成功");
        } else {
            p.sendMessage("§c创建失败：实体生成异常");
        }
    });
}

void sendManageListForm(Player& player) {
    json db = getDb();
    ll::form::SimpleForm form("管理悬浮字", "");

    if (db.empty()) {
        form.setContent("当前没有任何保存的悬浮字。");
        form.appendButton("返回主菜单");
        form.sendTo(player, [](Player& p, int, ll::form::FormCancelReason) {
            sendMainMenu(p);
        });
        return;
    }

    form.setContent("请选择要管理的悬浮字：");
    std::vector<std::string> keys;
    for (auto& el : db.items()) {
        keys.push_back(el.key());
        std::string firstLine = el.value()["lines"][0]["text"].get<std::string>();
        form.appendButton(fmt::format("[ID: {}]\n{}", el.key(), firstLine));
    }

    form.sendTo(player, [keys](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected == -1) {
            sendMainMenu(p);
            return;
        }
        sendOperationMenu(p, keys[selected]);
    });
}

void sendOperationMenu(Player& player, std::string const& ftId) {
    json db = getDb();
    if (!db.contains(ftId)) {
        sendManageListForm(player);
        return;
    }

    auto data = db[ftId];
    int lineCount = data["lines"].size();

    ll::form::SimpleForm form("悬浮字操作菜单", fmt::format("ID: {}\n行数: {}", ftId, lineCount));

    form.appendButton("转向/传送到悬浮字")
        .appendButton("编辑悬浮字内容")
        .appendButton("添加新行")
        .appendButton("🔥 删除悬浮字")
        .sendTo(player, [ftId, data](Player& p, int selected, ll::form::FormCancelReason) {
            if (selected == -1) {
                sendManageListForm(p);
                return;
            }

            if (selected == 0) {
                Vec3 pos{data["x"].get<double>(), data["y"].get<double>(), data["z"].get<double>()};
                int dimId = data["dimid"].get<int>();
                p.teleport(pos, dimId);
                p.sendMessage("§a已传送至悬浮字位置");
            } else if (selected == 1) {
                sendEditForm(p, ftId);
            } else if (selected == 2) {
                sendAddLineForm(p, ftId);
            } else if (selected == 3) {
                json currentDb = getDb();
                if (!currentDb.contains(ftId)) return;

                auto* level = p.getLevel();
                for (auto const& line : currentDb[ftId]["lines"]) {
                    int64_t uuidRaw = line["uuid"].get<int64_t>();
                    ActorUniqueID actorId(uuidRaw);
                    Actor* entity = level->fetchEntity(actorId, false);
                    if (entity) {
                        entity->remove();
                    }
                }
                currentDb.erase(ftId);
                saveDb(currentDb);
                p.sendMessage("§a悬浮字已彻底删除");
            }
        });
}

void sendEditForm(Player& player, std::string const& ftId) {
    json db = getDb();
    if (!db.contains(ftId)) return;

    ll::form::CustomForm form("编辑悬浮字内容");
    auto lines = db[ftId]["lines"];

    for (size_t i = 0; i < lines.size(); i++) {
        std::string currentText = lines[i]["text"].get<std::string>();
        form.appendInput(fmt::format("line_{}", i), fmt::format("第 {} 行", i + 1), "", currentText);
    }

    form.sendTo(player, [ftId, lines](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendOperationMenu(p, ftId);
            return;
        }

        json currentDb = getDb();
        if (!currentDb.contains(ftId)) return;

        auto& data = *result;
        auto* level = p.getLevel();

        for (size_t i = 0; i < lines.size(); i++) {
            std::string key = fmt::format("line_{}", i);
            std::string newText = std::get<std::string>(data.at(key));
            currentDb[ftId]["lines"][i]["text"] = newText;

            int64_t uuidRaw = lines[i]["uuid"].get<int64_t>();
            ActorUniqueID actorId(uuidRaw);
            Actor* entity = level->fetchEntity(actorId, false);
            if (entity) {
                entity->setNameTag(newText);
            }
        }

        saveDb(currentDb);
        p.sendMessage("§a悬浮字内容更新成功");
    });
}

void sendAddLineForm(Player& player, std::string const& ftId) {
    ll::form::CustomForm form("添加新行");
    form.appendInput("new_line", "请输入新一行的文本", "", "");

    form.sendTo(player, [ftId](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendOperationMenu(p, ftId);
            return;
        }

        auto& formData = *result;
        std::string text = std::get<std::string>(formData.at("new_line"));
        if (text.empty()) text = " ";

        json db = getDb();
        if (!db.contains(ftId)) return;

        auto data = db[ftId];
        double baseX = data["x"].get<double>();
        double baseY = data["y"].get<double>();
        double baseZ = data["z"].get<double>();
        double spacing = data["spacing"].get<double>();
        int lineCount = data["lines"].size();

        double newY = baseY - (lineCount * spacing);
        Vec3 spawnPos{baseX, newY, baseZ};

        auto* blockSource = &p.getDimension().getBlockSourceFromMainChunkSource();
        auto* level = p.getLevel();
        ActorDefinitionIdentifier def("minecraft:armor_stand");
        Actor* entity = level->addEntity(*blockSource, def, spawnPos);

        if (entity) {
            entity->setNameTag(text);
            entity->setNameTagVisible(true);
            entity->setInvisible(true);

            json lineObj;
            lineObj["text"] = text;
            lineObj["uuid"] = entity->getOrCreateUniqueID().rawID;

            db[ftId]["lines"].push_back(lineObj);
            saveDb(db);
            p.sendMessage("§a新行添加成功");
        } else {
            p.sendMessage("§c添加失败：实体生成异常");
        }
    });
}

void registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
        .getOrCreateCommand("ikft", "打开悬浮字管理系统", CommandPermissionLevel::GameDirectors);

    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        Player* player = static_cast<Player*>(origin.getEntity());
        if (player) {
            sendMainMenu(*player);
            output.success("操作面板已打开");
        } else {
            output.error("此命令仅限玩家在游戏内使用");
        }
    });
}

class IKFloatingTextMod {
public:
    static IKFloatingTextMod& getInstance() {
        static IKFloatingTextMod instance;
        return instance;
    }

    bool load() {
        return true;
    }

    bool enable() {
        registerCommand();
        return true;
    }

    bool disable() {
        return true;
    }
};

}

LL_REGISTER_MOD(ikft::IKFloatingTextMod, ikft::IKFloatingTextMod::getInstance());