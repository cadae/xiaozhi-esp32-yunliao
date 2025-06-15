#pragma once

#include "display/lcd_display.h"
#include <memory>
#include <functional>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "anim_player.h"
#include "mmap_generate_emoji.h"

namespace anim {

class EmojiPlayer;

using FlushIoReadyCallback = std::function<bool(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*)>;
using FlushCallback = std::function<void(anim_player_handle_t, int, int, int, int, const void*)>;

class EmojiPlayer {
public:
    EmojiPlayer(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height);
    ~EmojiPlayer();

    void StartPlayer(int aaf, bool repeat, int fps);
    void StopPlayer();
    
    // 背景相关方法
    void DrawBackground();
    void SetBackgroundColor(uint16_t color);

private:
    static bool OnFlushIoReady(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
    static void OnFlush(anim_player_handle_t handle, int x_start, int y_start, int x_end, int y_end, const void *color_data);

    anim_player_handle_t player_handle_;
    mmap_assets_handle_t assets_handle_;
    esp_lcd_panel_handle_t panel_;
    uint16_t background_color_;
    int screen_width_;
    int screen_height_;
    int emoji_x_offset_;
    int emoji_y_offset_;
};

class EmojiWidget : public Display {
public:
    EmojiWidget(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height, DisplayFonts fonts, bool show_status_bar = true, bool show_chat_box = false);
    virtual ~EmojiWidget();

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetTheme(const std::string& theme_name) override;
    
    // 聊天消息设置方法
    virtual void SetChatMessage(const char* role, const char* content) override;
    
    // 背景设置方法
    void SetBackgroundColor(uint16_t color);
    
    // 状态栏控制方法
    void SetStatusBarVisible(bool visible);
    bool IsStatusBarVisible() const { return show_status_bar_; }
    
    // 聊天框控制方法
    void SetChatBoxVisible(bool visible);
    bool IsChatBoxVisible() const { return show_chat_box_; }
    
    anim::EmojiPlayer* GetPlayer()
    {
        return player_.get();
    }

private:
    void InitializePlayer(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height);
    void InitializeLVGL(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height);
    void SetupStatusBar();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    std::unique_ptr<anim::EmojiPlayer> player_;
    
    // LVGL 相关成员变量
    esp_lcd_panel_handle_t panel_;
    esp_lcd_panel_io_handle_t panel_io_;
    int screen_width_;
    int screen_height_;
    bool show_status_bar_;
    bool show_chat_box_;
    
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* chat_box_ = nullptr;            // 聊天框容器
    lv_obj_t* chat_message_label_ = nullptr;  // 聊天消息标签
    
    DisplayFonts fonts_;
    ThemeColors current_theme_;
};

} // namespace anim 