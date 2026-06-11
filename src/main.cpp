#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "fmt/format.h"
#include "sculk/jsonc/jsonc.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ikft {

using Json = sculk::jsonc::ordered_jsonc;

struct FloatingText {
    std::string              id;
    std::vector<std::string> lines;
    double                   x       = 0.0;
    double                   y       = 0.0;
    double                   z       = 0.0;
    int                      dimId   = 0;
    double                   spacing = 0.3;
};

std::vector<FloatingText> gFloatingTexts;

void sendMainMenu(Player& player);
void sendCreateForm(Player& player);
void sendManageListForm(Player& player);
void sendOperationMenu(Player& player, std::string const& ftId);
void sendEditForm(Player& player, std::string const& ftId);
void sendAddLineForm(Player& player, std::string const& ftId);

std::filesystem::path getConfigPath() {
    auto mod = ll::mod::NativeMod::current();
    if (mod) {
        return mod->getConfigDir() / "floating_texts.jsonc";
    }
    return std::filesystem::path{"floating_texts.jsonc"};
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

std::optional<std::string> getFormString(
    ll::form::CustomFormResult const& result,
    std::string const&                key
) {
    if (!result) {
        return std::nullopt;
    }
    auto it = result->find(key);
    if (it == result->end() || !std::holds_alternative<std::string>(it->second)) {
        return std::nullopt;
    }
    return std::get<std::string>(it->second);
}

std::optional<double> parseDouble(std::string const& value) {
    try {
        size_t parsed = 0;
        double result = std::stod(trim(value), &parsed);
        return parsed == trim(value).size() ? std::optional<double>{result} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> parseInt(std::string const& value) {
    try {
        size_t parsed = 0;
        int    result = std::stoi(trim(value), &parsed);
        return parsed == trim(value).size() ? std::optional<int>{result} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

FloatingText* findFloatingText(std::string const& id) {
    auto it = std::find_if(gFloatingTexts.begin(), gFloatingTexts.end(), [&](FloatingText const& ft) { return ft.id == id; });
    return it == gFloatingTexts.end() ? nullptr : &*it;
}

bool idExists(std::string const& id) {
    return findFloatingText(id) != nullptr;
}

std::string generateId() {
    for (int index = 1; index < 100000; ++index) {
        auto id = fmt::format("ft_{:03}", index);
        if (!idExists(id)) {
            return id;
        }
    }
    return fmt::format("ft_{}", gFloatingTexts.size() + 1);
}

Json floatingTextToJson(FloatingText const& ft) {
    Json lines = Json::array();
    for (auto const& line : ft.lines) {
        lines.push_back(line);
    }

    return Json::object({
        {"id", ft.id},
        {"lines", lines},
        {"position", Json::object({{"x", ft.x}, {"y", ft.y}, {"z", ft.z}, {"dimId", ft.dimId}})},
        {"spacing", ft.spacing},
    });
}

std::optional<std::string> jsonString(Json const& node, std::string_view key) {
    if (!node.is_object() || !node.contains(key, sculk::jsonc::value_type::string)) {
        return std::nullopt;
    }
    return node.at(key).get<std::string>();
}

std::optional<double> jsonDouble(Json const& node, std::string_view key) {
    if (!node.is_object() || !node.contains(key)) {
        return std::nullopt;
    }
    auto const& value = node.at(key);
    if (!value.is_number()) {
        return std::nullopt;
    }
    return value.get<double>();
}

std::optional<int> jsonInt(Json const& node, std::string_view key) {
    if (!node.is_object() || !node.contains(key)) {
        return std::nullopt;
    }
    auto const& value = node.at(key);
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    return value.get<int>();
}

std::optional<FloatingText> floatingTextFromJson(Json const& node) {
    auto id = jsonString(node, "id");
    if (!id || id->empty()) {
        return std::nullopt;
    }

    FloatingText ft;
    ft.id = *id;

    if (node.contains("lines", sculk::jsonc::value_type::array)) {
        auto const& lines = node.at("lines").as<Json::array_type>();
        for (auto const& line : lines) {
            if (line.is_string()) {
                ft.lines.push_back(line.get<std::string>());
            }
        }
    }
    if (ft.lines.empty()) {
        ft.lines.push_back("");
    }

    if (node.contains("position", sculk::jsonc::value_type::object)) {
        auto const& pos = node.at("position");
        ft.x           = jsonDouble(pos, "x").value_or(0.0);
        ft.y           = jsonDouble(pos, "y").value_or(0.0);
        ft.z           = jsonDouble(pos, "z").value_or(0.0);
        ft.dimId       = jsonInt(pos, "dimId").value_or(0);
    }
    ft.spacing = jsonDouble(node, "spacing").value_or(0.3);
    return ft;
}

Json makeDefaultConfig() {
    Json config = Json::object();
    config["version"] = 1;
    config["floatingTexts"] = Json::array();
    return config;
}

bool saveConfig() {
    auto path = getConfigPath();
    std::filesystem::create_directories(path.parent_path());

    Json floatingTexts = Json::array();
    for (auto const& ft : gFloatingTexts) {
        floatingTexts.push_back(floatingTextToJson(ft));
    }

    Json config = makeDefaultConfig();
    config["floatingTexts"] = floatingTexts;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << config.dump(4, false, false, true);
    return static_cast<bool>(out);
}

bool loadConfig() {
    auto path = getConfigPath();
    std::filesystem::create_directories(path.parent_path());

    if (!std::filesystem::exists(path)) {
        gFloatingTexts.clear();
        return saveConfig();
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    try {
        auto doc = Json::parse(content, true, false);
        gFloatingTexts.clear();

        if (!doc.contains("floatingTexts", sculk::jsonc::value_type::array)) {
            return saveConfig();
        }

        auto const& array = doc.at("floatingTexts").as<Json::array_type>();
        for (auto const& item : array) {
            if (auto ft = floatingTextFromJson(item); ft && !idExists(ft->id)) {
                gFloatingTexts.push_back(std::move(*ft));
            }
        }
        return true;
    } catch (...) {
        gFloatingTexts.clear();
        return false;
    }
}

void syncFloatingText(FloatingText const&) {
    // TODO: spawn/update the actual floating text entity.
}

void removeFloatingTextEntity(std::string const&) {
    // TODO: remove the actual floating text entity.
}

void sendMainMenu(Player& player) {
    ll::form::SimpleForm form("IK 悬浮字管理", "请选择要执行的操作。");

    form.appendButton("§l§a创建悬浮字\n§r§8新建一条悬浮文本")
        .appendButton("§l§b管理悬浮字\n§r§8编辑、删除已有悬浮字")
        .appendButton("§l§e重载配置\n§r§8从配置文件重新读取")
        .sendTo(player, [](Player& p, int selected, ll::form::FormCancelReason) {
            switch (selected) {
            case 0:
                sendCreateForm(p);
                break;
            case 1:
                sendManageListForm(p);
                break;
            case 2:
                p.sendMessage(loadConfig() ? "§a悬浮字配置已重载。" : "§c悬浮字配置读取失败。");
                sendMainMenu(p);
                break;
            default:
                break;
            }
        });
}

void sendCreateForm(Player& player) {
    ll::form::CustomForm form("创建悬浮字");

    form.appendInput("ft_id", "唯一 ID", "留空自动生成", "")
        .appendInput("ft_text", "第一行文本", "输入悬浮文本", "")
        .appendInput("ft_x", "X 坐标", "", "0.0")
        .appendInput("ft_y", "Y 坐标", "", "0.0")
        .appendInput("ft_z", "Z 坐标", "", "0.0")
        .appendInput("ft_dim", "维度 ID", "", "0")
        .appendInput("ft_spacing", "行距", "", "0.3")
        .setSubmitButton("创建");

    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendMainMenu(p);
            return;
        }

        FloatingText ft;
        ft.id = trim(getFormString(result, "ft_id").value_or(""));
        if (ft.id.empty()) {
            ft.id = generateId();
        }
        if (idExists(ft.id)) {
            p.sendMessage("§c悬浮字 ID 已存在。");
            sendCreateForm(p);
            return;
        }

        auto line = getFormString(result, "ft_text").value_or("");
        if (line.empty()) {
            p.sendMessage("§c悬浮字文本不能为空。");
            sendCreateForm(p);
            return;
        }
        ft.lines.push_back(line);

        auto x       = parseDouble(getFormString(result, "ft_x").value_or(""));
        auto y       = parseDouble(getFormString(result, "ft_y").value_or(""));
        auto z       = parseDouble(getFormString(result, "ft_z").value_or(""));
        auto dimId   = parseInt(getFormString(result, "ft_dim").value_or(""));
        auto spacing = parseDouble(getFormString(result, "ft_spacing").value_or(""));
        if (!x || !y || !z || !dimId || !spacing) {
            p.sendMessage("§c坐标、维度或行距格式不正确。");
            sendCreateForm(p);
            return;
        }

        ft.x       = *x;
        ft.y       = *y;
        ft.z       = *z;
        ft.dimId   = *dimId;
        ft.spacing = *spacing;

        gFloatingTexts.push_back(ft);
        syncFloatingText(ft);
        p.sendMessage(saveConfig() ? fmt::format("§a已创建悬浮字：{}", ft.id) : "§c悬浮字已创建，但配置保存失败。");
        sendManageListForm(p);
    });
}

void sendManageListForm(Player& player) {
    ll::form::SimpleForm form("管理悬浮字", "");
    std::vector<std::string> ids;

    if (gFloatingTexts.empty()) {
        form.setContent("当前没有已保存的悬浮字。");
    } else {
        form.setContent("请选择要管理的悬浮字。");
        for (auto const& ft : gFloatingTexts) {
            ids.push_back(ft.id);
            form.appendButton(fmt::format("§l§3{}\n§r§8{}", ft.id, ft.lines.empty() ? "" : ft.lines.front()));
        }
    }

    form.appendButton("§l§8<< 返回主菜单");
    form.sendTo(player, [ids](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected < 0 || static_cast<size_t>(selected) >= ids.size()) {
            sendMainMenu(p);
            return;
        }
        sendOperationMenu(p, ids[static_cast<size_t>(selected)]);
    });
}

void sendOperationMenu(Player& player, std::string const& ftId) {
    auto* ft = findFloatingText(ftId);
    if (ft == nullptr) {
        player.sendMessage("§c悬浮字不存在。");
        sendManageListForm(player);
        return;
    }

    ll::form::SimpleForm form("悬浮字操作", fmt::format("目标 ID：§f{}\n坐标：{}, {}, {} | 维度：{}", ft->id, ft->x, ft->y, ft->z, ft->dimId));

    form.appendButton("§l§3传送到悬浮字\n§r§8实体/传送逻辑暂未实现")
        .appendButton("§l§6编辑文本内容")
        .appendButton("§l§a添加文本行")
        .appendButton("§l§c删除悬浮字")
        .appendButton("§l§8<< 返回列表")
        .sendTo(player, [ftId](Player& p, int selected, ll::form::FormCancelReason) {
            switch (selected) {
            case 0:
                p.sendMessage("§e悬浮字实体/传送逻辑暂未实现。");
                sendOperationMenu(p, ftId);
                break;
            case 1:
                sendEditForm(p, ftId);
                break;
            case 2:
                sendAddLineForm(p, ftId);
                break;
            case 3:
                gFloatingTexts.erase(
                    std::remove_if(gFloatingTexts.begin(), gFloatingTexts.end(), [&](FloatingText const& ft) { return ft.id == ftId; }),
                    gFloatingTexts.end()
                );
                removeFloatingTextEntity(ftId);
                p.sendMessage(saveConfig() ? "§a悬浮字已删除。" : "§c悬浮字已删除，但配置保存失败。");
                sendManageListForm(p);
                break;
            default:
                sendManageListForm(p);
                break;
            }
        });
}

void sendEditForm(Player& player, std::string const& ftId) {
    auto* ft = findFloatingText(ftId);
    if (ft == nullptr) {
        player.sendMessage("§c悬浮字不存在。");
        sendManageListForm(player);
        return;
    }

    ll::form::CustomForm form(fmt::format("编辑悬浮字 {}", ftId));
    for (size_t i = 0; i < ft->lines.size(); ++i) {
        form.appendInput(fmt::format("line_{}", i), fmt::format("第 {} 行", i + 1), "输入文本", ft->lines[i]);
    }
    form.setSubmitButton("保存");

    form.sendTo(player, [ftId](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendOperationMenu(p, ftId);
            return;
        }

        auto* ft = findFloatingText(ftId);
        if (ft == nullptr) {
            p.sendMessage("§c悬浮字不存在。");
            sendManageListForm(p);
            return;
        }

        std::vector<std::string> lines;
        for (size_t i = 0; i < ft->lines.size(); ++i) {
            auto line = getFormString(result, fmt::format("line_{}", i)).value_or("");
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        if (lines.empty()) {
            p.sendMessage("§c至少保留一行文本。");
            sendEditForm(p, ftId);
            return;
        }

        ft->lines = std::move(lines);
        syncFloatingText(*ft);
        p.sendMessage(saveConfig() ? "§a悬浮字文本已保存。" : "§c文本已修改，但配置保存失败。");
        sendOperationMenu(p, ftId);
    });
}

void sendAddLineForm(Player& player, std::string const& ftId) {
    ll::form::CustomForm form("添加文本行");
    form.appendInput("new_line", "新文本行", "输入要追加的文本", "").setSubmitButton("添加");

    form.sendTo(player, [ftId](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendOperationMenu(p, ftId);
            return;
        }

        auto* ft = findFloatingText(ftId);
        if (ft == nullptr) {
            p.sendMessage("§c悬浮字不存在。");
            sendManageListForm(p);
            return;
        }

        auto line = getFormString(result, "new_line").value_or("");
        if (line.empty()) {
            p.sendMessage("§c新文本行不能为空。");
            sendAddLineForm(p, ftId);
            return;
        }

        ft->lines.push_back(line);
        syncFloatingText(*ft);
        p.sendMessage(saveConfig() ? "§a已添加文本行。" : "§c文本行已添加，但配置保存失败。");
        sendOperationMenu(p, ftId);
    });
}

void registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("ikft", "打开 IK 悬浮字管理系统", CommandPermissionLevel::GameDirectors);

    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        Actor* entity = origin.getEntity();
        if (entity != nullptr && entity->isPlayer()) {
            auto* player = static_cast<Player*>(entity);
            sendMainMenu(*player);
            output.success("悬浮字管理面板已打开。");
        } else {
            output.error("该命令只能由游戏内玩家执行。");
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
        loadConfig();
        return true;
    }

    bool enable() {
        registerCommand();
        return true;
    }

    bool disable() {
        saveConfig();
        return true;
    }
};

} // namespace ikft

LL_REGISTER_MOD(ikft::IKFloatingTextMod, ikft::IKFloatingTextMod::getInstance());
