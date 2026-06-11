#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/form/CustomForm.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "fmt/format.h"
#include <string>
#include <vector>

namespace ikft {

void sendMainMenu(Player& player);
void sendCreateForm(Player& player);
void sendManageListForm(Player& player);
void sendOperationMenu(Player& player, std::string const& ftId);
void sendEditForm(Player& player, std::string const& ftId);
void sendAddLineForm(Player& player, std::string const& ftId);

void sendMainMenu(Player& player) {
    ll::form::SimpleForm form(
        "§l§d悬浮字管理控制台", 
        "§7请选择您要进行的操作："
    );

    form.appendButton("§l§a▶ 创建悬浮字\n§r§8[新建一个悬浮文本]")
        .appendButton("§l§b▶ 管理悬浮字\n§r§8[编辑/删除/传送到目标]")
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
    ll::form::CustomForm form("§l§a创建全新悬浮字");

    form.appendInput("ft_id", "§l§e▶ 悬浮字唯一ID\n§r§8(留空则系统自动生成)", "", "")
        .appendInput("ft_text", "§l§b▶ 第一行文本内容", "输入悬浮文本...", "")
        .appendInput("ft_x", "§l§6▶ X 坐标", "", "0.0")
        .appendInput("ft_y", "§l§6▶ Y 坐标", "", "0.0")
        .appendInput("ft_z", "§l§6▶ Z 坐标", "", "0.0")
        .appendInput("ft_dim", "§l§6▶ 所在维度 ID", "", "0")
        .appendInput("ft_spacing", "§l§d▶ 行距\n§r§8(默认: 0.3)", "", "0.3");

    form.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result) {
            sendMainMenu(p);
            return;
        }
        sendMainMenu(p);
    });
}

void sendManageListForm(Player& player) {
    json db = json::array(); 
    ll::form::SimpleForm form("§l§b管理已有悬浮字", "");

    form.setContent("§7请点击列表中需要管理的悬浮字：");
    form.appendButton("§l§3[演示ID: 001]\n§r§8内容: 示例悬浮字文本");
    form.appendButton("§l§8<< 返回主菜单");

    form.sendTo(player, [](Player& p, int selected, ll::form::FormCancelReason) {
        if (selected == -1 || selected == 1) {
            sendMainMenu(p);
            return;
        }
        sendOperationMenu(p, "001");
    });
}

void sendOperationMenu(Player& player, std::string const& ftId) {
    ll::form::SimpleForm form("§l§e悬浮字操作菜单", fmt::format("§7目标 ID: §f{}", ftId));

    form.appendButton("§l§3▶ 传送到悬浮字\n§r§8[跃迁至该坐标]")
        .appendButton("§l§6▶ 编辑文本内容\n§r§8[修改现有文本]")
        .appendButton("§l§a▶ 添加新文本行\n§r§8[在下方追加一行]")
        .appendButton("§l§c✖ 彻底删除\n§r§8[移除实体与数据]")
        .sendTo(player, [ftId](Player& p, int selected, ll::form::FormCancelReason) {
            if (selected == -1) {
                sendManageListForm(p);
                return;
            }
            if (selected == 1) {
                sendEditForm(p, ftId);
            } else if (selected == 2) {
                sendAddLineForm(p, ftId);
            } else {
                sendManageListForm(p);
            }
        });
}

void sendEditForm(Player& player, std::string const& ftId) {
    ll::form::CustomForm form("§l§6编辑悬浮字内容");
    form.appendInput("line_0", "§l§b▶ 第 1 行内容", "输入修改后的文字...", "");

    form.sendTo(player, [ftId](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        sendOperationMenu(p, ftId);
    });
}

void sendAddLineForm(Player& player, std::string const& ftId) {
    ll::form::CustomForm form("§l§a添加新文本行");
    form.appendInput("new_line", "§l§e▶ 请输入新一行的文本内容", "在此处输入文字...", "");

    form.sendTo(player, [ftId](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        sendOperationMenu(p, ftId);
    });
}

void registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
        .getOrCreateCommand("ikft", "打开悬浮字管理系统", CommandPermissionLevel::GameDirectors);

    cmd.overload().execute([](CommandOrigin const& origin, CommandOutput& output) {
        Actor* entity = origin.getEntity();
        if (entity && entity->isPlayer()) {
            Player* player = static_cast<Player*>(entity);
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
