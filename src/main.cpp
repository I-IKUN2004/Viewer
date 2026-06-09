#include "ll/api/mod/NativeMod.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/ModalForm.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "fmt/format.h"
#include <string>
#include <variant>

namespace ikft {

void sendCreateForm(Player& player);
void sendManageListForm(Player& player);
void sendMainMenu(Player& player);

void sendCreateForm(Player& player) {
    ll::form::CustomForm form("创建悬浮字");

    form.appendInput("ft_id", "悬浮字唯一ID (留空自动生成)", "例如: spawn_text", "")
        .appendInput("ft_text", "第一行文本内容 (支持 §)", "例如: §e欢迎", "")
        .appendSlider("ft_spacing", "行距设置", 0.1, 1.0, 0.1, 0.3)
        .appendToggle("ft_invisible", "底座是否隐形", true);

    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendMainMenu(p);
            return;
        }

        auto& data = *result;

        std::string id = std::get<std::string>(data.at("ft_id"));
        std::string text = std::get<std::string>(data.at("ft_text"));
        double spacing = std::get<double>(data.at("ft_spacing"));
        uint64_t invisible = std::get<uint64_t>(data.at("ft_invisible"));

        if (id.empty()) {
            id = "ikft_auto_id";
        }

        p.sendMessage(fmt::format("§a创建请求已接收！ID: {}, 文本: {}, 行距: {}", id, text, spacing));
    });
}

void sendManageListForm(Player& player) {
    ll::form::SimpleForm form("管理悬浮字", "请选择你要管理的悬浮字：");

    form.appendButton("返回主菜单")
        .sendTo(player, [](Player& p, int selected, ll::form::FormCancelReason) {
            if (selected == -1 || selected == 0) {
                sendMainMenu(p);
            }
        });
}

void sendMainMenu(Player& player) {
    ll::form::SimpleForm form("悬浮字管理系统", "请选择你要进行的操作：");

    form.appendButton("➕ 创建悬浮字")
        .appendButton("⚙️ 管理悬浮字")
        .sendTo(player, [](Player& p, int selected, ll::form::FormCancelReason) {
            if (selected == -1) return;
            switch (selected) {
                case 0:
                    sendCreateForm(p);
                    break;
                case 1:
                    sendManageListForm(p);
                    break;
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
            output.success("正在打开悬浮字管理菜单...");
        } else {
            output.error("此命令只能由玩家在游戏中执行！");
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