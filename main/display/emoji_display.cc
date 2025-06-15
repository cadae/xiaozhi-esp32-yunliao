#include <cstring>
#include <unordered_map>
#include <tuple>
#include "display/lcd_display.h"
#include <esp_log.h>
#include <esp_err.h>
#include "mmap_generate_emoji.h"
#include "emoji_display.h"
#include "assets/lang_config.h"
#include "settings.h"
#include <font_awesome_symbols.h>
#include <esp_lvgl_port.h>

#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <inttypes.h>  // 添加这个头文件以使用PRIu32

static const char *TAG = "emoji";

// 根据日志分析得出的实际emoji尺寸
#define EMOJI_WIDTH   160
#define EMOJI_HEIGHT  80

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR       lv_color_black()           // 纯黑色背景
#define DARK_TEXT_COLOR             lv_color_white()           // White text
#define DARK_CHAT_BACKGROUND_COLOR  lv_color_black()           // 纯黑色背景
#define DARK_USER_BUBBLE_COLOR      lv_color_hex(0x1A6C37)     // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)     // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)     // Medium gray
#define DARK_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)     // Light gray text
#define DARK_BORDER_COLOR           lv_color_hex(0x333333)     // Dark gray border
#define DARK_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)     // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR       lv_color_black()           // 纯黑色背景
#define LIGHT_TEXT_COLOR             lv_color_white()           // 改为白色文字以便在黑色背景上可见
#define LIGHT_CHAT_BACKGROUND_COLOR  lv_color_black()           // 纯黑色背景
#define LIGHT_USER_BUBBLE_COLOR      lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333)     // 改为深灰色以便在黑色背景上可见
#define LIGHT_SYSTEM_BUBBLE_COLOR    lv_color_hex(0x2A2A2A)     // 改为深灰色
#define LIGHT_SYSTEM_TEXT_COLOR      lv_color_hex(0xAAAAAA)     // 改为浅灰色文字
#define LIGHT_BORDER_COLOR           lv_color_hex(0x333333)     // 改为深灰色边框
#define LIGHT_LOW_BATTERY_COLOR      lv_color_hex(0xFF0000)     // Red

// Define dark theme colors
const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR
};

// Define light theme colors
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR
};

LV_FONT_DECLARE(font_awesome_30_4);

namespace anim {

bool EmojiPlayer::OnFlushIoReady(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    auto* disp_drv = static_cast<anim_player_handle_t*>(user_ctx);
    anim_player_flush_ready(disp_drv);
    return true;
}

void EmojiPlayer::OnFlush(anim_player_handle_t handle, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    auto* player = static_cast<EmojiPlayer*>(anim_player_get_user_data(handle));
    
    // 计算居中偏移：让160x80的表情在240x240屏幕中居中
    const int x_offset = (player->screen_width_ - EMOJI_WIDTH) / 2;   // (240-160)/2 = 40
    const int y_offset = (player->screen_height_ - EMOJI_HEIGHT) / 2; // (240-80)/2 = 80
    
    int adjusted_x_start = x_start + x_offset;
    int adjusted_y_start = y_start + y_offset;
    int adjusted_x_end = x_end + x_offset;
    int adjusted_y_end = y_end + y_offset;
    
    // 确保坐标在屏幕范围内
    if (adjusted_x_start < 0) adjusted_x_start = 0;
    if (adjusted_y_start < 0) adjusted_y_start = 0;
    if (adjusted_x_end > player->screen_width_) adjusted_x_end = player->screen_width_;
    if (adjusted_y_end > player->screen_height_) adjusted_y_end = player->screen_height_;
    
    ESP_LOGD(TAG, "Drawing emoji at (%d,%d) to (%d,%d), original: (%d,%d) to (%d,%d), center offset: (%d,%d)", 
             adjusted_x_start, adjusted_y_start, adjusted_x_end, adjusted_y_end,
             x_start, y_start, x_end, y_end, x_offset, y_offset);
    
    esp_lcd_panel_draw_bitmap(player->panel_, adjusted_x_start, adjusted_y_start, adjusted_x_end, adjusted_y_end, color_data);
}

EmojiPlayer::EmojiPlayer(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height)
    : panel_(panel), background_color_(0x0000), screen_width_(screen_width), screen_height_(screen_height)  // 0x0000为纯黑色
{
    ESP_LOGI(TAG, "Create EmojiPlayer, panel: %p, panel_io: %p, screen: %dx%d", panel, panel_io, screen_width, screen_height);
    
    // 计算emoji居中位置
    emoji_x_offset_ = (screen_width_ - EMOJI_WIDTH) / 2;
    emoji_y_offset_ = (screen_height_ - EMOJI_HEIGHT) / 2;
    
    ESP_LOGI(TAG, "Emoji offset: (%d, %d)", emoji_x_offset_, emoji_y_offset_);
    
    // 先清除背景
    DrawBackground();
    
    const mmap_assets_config_t assets_cfg = {
        .partition_label = "assets_A",
        .max_files = MMAP_EMOJI_FILES,
        .checksum = MMAP_EMOJI_CHECKSUM,
        .flags = {.mmap_enable = true, .full_check = false}
    };

    esp_err_t ret = mmap_assets_new(&assets_cfg, &assets_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mmap assets: %s", esp_err_to_name(ret));
        assets_handle_ = nullptr;
        player_handle_ = nullptr;
        return;
    }

    anim_player_config_t player_cfg = {
        .flush_cb = OnFlush,
        .update_cb = NULL,
        .user_data = this,  // 传递this指针而不是panel
        .flags = {.swap = true},
        .task = ANIM_PLAYER_INIT_CONFIG()
    };

    player_handle_ = anim_player_init(&player_cfg);
    if (player_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize anim player");
        if (assets_handle_) {
            mmap_assets_del(assets_handle_);
            assets_handle_ = nullptr;
        }
        return;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushIoReady,
    };
    esp_lcd_panel_io_register_event_callbacks(panel_io, &cbs, player_handle_);
    
    StartPlayer(MMAP_EMOJI_CONNECTING_AAF, true, 15);
}

EmojiPlayer::~EmojiPlayer()
{
    if (player_handle_) {
        anim_player_update(player_handle_, PLAYER_ACTION_STOP);
        anim_player_deinit(player_handle_);
        player_handle_ = nullptr;
    }

    if (assets_handle_) {
        mmap_assets_del(assets_handle_);
        assets_handle_ = NULL;
    }
}

void EmojiPlayer::DrawBackground()
{
    if (!panel_) {
        ESP_LOGW(TAG, "Panel not initialized, cannot draw background");
        return;
    }
    
    // 创建背景缓冲区
    size_t buffer_size = screen_width_ * screen_height_ * sizeof(uint16_t);
    uint16_t* background_buffer = (uint16_t*)malloc(buffer_size);
    if (!background_buffer) {
        ESP_LOGE(TAG, "Failed to allocate background buffer");
        return;
    }
    
    // 填充背景色
    for (int i = 0; i < screen_width_ * screen_height_; i++) {
        background_buffer[i] = background_color_;
    }
    
    // 绘制背景到屏幕
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_, 0, 0, screen_width_, screen_height_, background_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw background: %s", esp_err_to_name(ret));
    }
    
    free(background_buffer);
    ESP_LOGI(TAG, "Background drawn with color 0x%04X", background_color_);
}

void EmojiPlayer::SetBackgroundColor(uint16_t color)
{
    background_color_ = color;
    DrawBackground();
}

void EmojiPlayer::StartPlayer(int aaf, bool repeat, int fps)
{
    if (player_handle_ == nullptr || assets_handle_ == nullptr) {
        ESP_LOGW(TAG, "Player or assets not initialized, skipping animation");
        return;
    }
    
    uint32_t start, end;
    const void *src_data;
    size_t src_len;

    src_data = mmap_assets_get_mem(assets_handle_, aaf);
    if (src_data == nullptr) {
        ESP_LOGW(TAG, "Failed to get AAF data for index %d", aaf);
        return;
    }
    
    src_len = mmap_assets_get_size(assets_handle_, aaf);
    if (src_len == 0) {
        ESP_LOGW(TAG, "AAF data size is 0 for index %d", aaf);
        return;
    }

    ESP_LOGI(TAG, "Loading AAF %d, size: %zu bytes", aaf, src_len);

    anim_player_set_src_data(player_handle_, src_data, src_len);
    anim_player_get_segment(player_handle_, &start, &end);
    
    // 尝试获取动画的尺寸信息
    // 注意：这可能需要anim_player库的特定API，如果没有就注释掉
    // anim_player_get_canvas_size() 等API如果存在的话
    ESP_LOGI(TAG, "AAF %d segment: start=%" PRIu32 ", end=%" PRIu32 ", fps=%d", aaf, start, end, fps);
    
    if(MMAP_EMOJI_WAKE_AAF == aaf){
        start = 7;
    }
    anim_player_set_segment(player_handle_, start, end, fps, true);
    anim_player_update(player_handle_, PLAYER_ACTION_START);
}

void EmojiPlayer::StopPlayer()
{
    if (player_handle_ == nullptr) {
        ESP_LOGW(TAG, "Player not initialized, cannot stop");
        return;
    }
    anim_player_update(player_handle_, PLAYER_ACTION_STOP);
}

EmojiWidget::EmojiWidget(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height, DisplayFonts fonts, bool show_status_bar, bool show_chat_box)
    : panel_(panel), panel_io_(panel_io), screen_width_(screen_width), screen_height_(screen_height), show_status_bar_(show_status_bar), show_chat_box_(show_chat_box), fonts_(fonts)
{
    width_ = screen_width;
    height_ = screen_height;

    // 加载主题设置
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // 更新主题
    if (current_theme_name_ == "dark") {
        current_theme_ = DARK_THEME;
    } else if (current_theme_name_ == "light") {
        current_theme_ = LIGHT_THEME;
    }

    InitializePlayer(panel, panel_io, screen_width, screen_height);
    InitializeLVGL(panel, panel_io, screen_width, screen_height);
}

EmojiWidget::~EmojiWidget()
{
    // 清理 LVGL 对象
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
        chat_message_label_ = nullptr;
    }
    if (chat_box_ != nullptr) {
        lv_obj_del(chat_box_);
        chat_box_ = nullptr;
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
        status_bar_ = nullptr;
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
        container_ = nullptr;
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
        display_ = nullptr;
    }
}

void EmojiWidget::InitializeLVGL(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height)
{
    ESP_LOGI(TAG, "Initialize LVGL library for EmojiWidget");
    
    // 如果 LVGL 还没有初始化，则初始化它
    if (!lv_is_initialized()) {
        lv_init();
        
        ESP_LOGI(TAG, "Initialize LVGL port for EmojiWidget");
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        port_cfg.task_priority = 1;
        port_cfg.timer_period_ms = 50;
        lvgl_port_init(&port_cfg);
    }

    ESP_LOGI(TAG, "Adding LCD screen for EmojiWidget");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(screen_width * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(screen_width),
        .vres = static_cast<uint32_t>(screen_height),
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = true,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 1,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display for EmojiWidget");
        return;
    }

    SetupStatusBar();
}

void EmojiWidget::SetupStatusBar()
{
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);

    /* Status bar - 固定在屏幕顶部 */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0); // 固定在屏幕顶部
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    
    /* Status bar layout */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -30); // 向上偏移30像素，避开聊天框
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // 聊天框 - 始终创建，但根据配置决定是否显示
    chat_box_ = lv_obj_create(screen);
    lv_obj_set_size(chat_box_, LV_HOR_RES, 25);
    lv_obj_align(chat_box_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(chat_box_, 0, 0);
    lv_obj_set_style_bg_color(chat_box_, current_theme_.background, 0);
    lv_obj_set_style_border_width(chat_box_, 0, 0); // 移除边框
    lv_obj_set_style_pad_all(chat_box_, 2, 0);
    lv_obj_set_scrollbar_mode(chat_box_, LV_SCROLLBAR_MODE_OFF);

    // 创建聊天消息标签
    chat_message_label_ = lv_label_create(chat_box_);
    lv_label_set_text(chat_message_label_, ""); // 不设置默认内容，保持空白
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - 8); // 减去padding
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_CLIP); // 使用CLIP模式直接截断
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_RIGHT, 0); // 右对齐
    lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
    lv_obj_set_height(chat_message_label_, 21); // 设置固定高度，确保单行显示
    lv_obj_align(chat_message_label_, LV_ALIGN_RIGHT_MID, -2, 0); // 右对齐，稍微留一点边距

    // 根据配置决定状态栏的初始显示状态
    if (!show_status_bar_) {
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 根据配置决定聊天框的初始显示状态
    if (!show_chat_box_) {
        lv_obj_add_flag(chat_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmojiWidget::SetEmotion(const char* emotion)
{
    if (!player_) {
        return;
    }

    using Param = std::tuple<int, bool, int>;
    static const std::unordered_map<std::string, Param> emotion_map = {
        {"happy",       {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"laughing",    {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"funny",       {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"loving",      {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"embarrassed", {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"confident",   {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"delicious",   {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"sad",         {MMAP_EMOJI_SAD_LOOP_AAF,   true, 25}},
        {"crying",      {MMAP_EMOJI_SAD_LOOP_AAF,   true, 25}},
        {"sleepy",      {MMAP_EMOJI_SAD_LOOP_AAF,   true, 25}},
        {"silly",       {MMAP_EMOJI_SAD_LOOP_AAF,   true, 25}},
        {"angry",       {MMAP_EMOJI_ANGER_LOOP_AAF, true, 25}},
        {"surprised",   {MMAP_EMOJI_PANIC_LOOP_AAF, true, 25}},
        {"shocked",     {MMAP_EMOJI_PANIC_LOOP_AAF, true, 25}},
        {"thinking",    {MMAP_EMOJI_HAPPY_LOOP_AAF, true, 25}},
        {"winking",     {MMAP_EMOJI_BLINK_QUICK_AAF, true, 5}},
        {"relaxed",     {MMAP_EMOJI_SCORN_LOOP_AAF, true, 25}},
        {"confused",    {MMAP_EMOJI_SCORN_LOOP_AAF, true, 25}},
    };

    auto it = emotion_map.find(emotion);
    if (it != emotion_map.end()) {
        const auto& [aaf, repeat, fps] = it->second;
        player_->StartPlayer(aaf, repeat, fps);
    } else if (strcmp(emotion, "neutral") == 0) {
    }
}

void EmojiWidget::SetStatus(const char* status)
{
    // 更新播放器状态
    if (player_) {
        if (strcmp(status, "聆听中...") == 0) {
            player_->StartPlayer(MMAP_EMOJI_ASKING_AAF, true, 15);
        } else if (strcmp(status, "待命") == 0) {
            player_->StartPlayer(MMAP_EMOJI_WAKE_AAF, true, 15);
        }
    }
    
    // 更新状态栏
    Display::SetStatus(status);
}

void EmojiWidget::SetTheme(const std::string& theme_name)
{
    DisplayLockGuard lock(this);
    
    if (theme_name == "dark" || theme_name == "DARK") {
        current_theme_ = DARK_THEME;
    } else if (theme_name == "light" || theme_name == "LIGHT") {
        current_theme_ = LIGHT_THEME;
    } else {
        ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str());
        return;
    }
    
    // 获取活动屏幕
    lv_obj_t* screen = lv_screen_active();
    
    // 更新屏幕颜色
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    
    // 更新容器颜色
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
        lv_obj_set_style_border_color(container_, current_theme_.border, 0);
    }
    
    // 更新状态栏颜色
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
        
        // 更新状态栏元素
        if (network_label_ != nullptr) {
            lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
        }
        if (status_label_ != nullptr) {
            lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
        }
        if (notification_label_ != nullptr) {
            lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
        }
        if (mute_label_ != nullptr) {
            lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);
        }
        if (battery_label_ != nullptr) {
            lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
        }
    }
    
    // 更新低电量弹窗
    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    }

    // 更新聊天框
    if (chat_box_ != nullptr) {
        lv_obj_set_style_bg_color(chat_box_, current_theme_.background, 0);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
    }

    // 保存主题到设置
    Display::SetTheme(theme_name);
}

bool EmojiWidget::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void EmojiWidget::Unlock()
{
    lvgl_port_unlock();
}

void EmojiWidget::InitializePlayer(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int screen_width, int screen_height)
{
    player_ = std::make_unique<EmojiPlayer>(panel, panel_io, screen_width, screen_height);
}

void EmojiWidget::SetBackgroundColor(uint16_t color)
{
    DisplayLockGuard lock(this);
    
    // 将16位RGB565转换为lv_color_t
    lv_color_t lv_color = lv_color_hex((color & 0xF800) << 8 | (color & 0x07E0) << 5 | (color & 0x001F) << 3);
    
    // 获取活动屏幕并设置背景色
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color, 0);
    
    // 同时更新容器背景色
    if (container_ != nullptr) {
        lv_obj_set_style_bg_color(container_, lv_color, 0);
    }
    
    // 更新状态栏背景色
    if (status_bar_ != nullptr) {
        lv_obj_set_style_bg_color(status_bar_, lv_color, 0);
    }
    
    // 更新聊天框背景色
    if (chat_box_ != nullptr) {
        lv_obj_set_style_bg_color(chat_box_, lv_color, 0);
    }
}

void EmojiWidget::SetStatusBarVisible(bool visible)
{
    DisplayLockGuard lock(this);
    
    if (show_status_bar_ == visible) {
        return; // 状态没有改变，无需操作
    }
    
    show_status_bar_ = visible;
    
    if (status_bar_ != nullptr) {
        if (visible) {
            // 显示状态栏
            lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Status bar shown");
        } else {
            // 隐藏状态栏
            lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Status bar hidden");
        }
    }
}

void EmojiWidget::SetChatMessage(const char* role, const char* content)
{
    DisplayLockGuard lock(this);
    
    if (chat_message_label_ == nullptr) {
        ESP_LOGW(TAG, "Chat message label not initialized");
        return;
    }
    
    // 先强制清空标签内容
    lv_label_set_text(chat_message_label_, "");
    
    // 如果有新内容，则设置新内容
    if (content != nullptr && strlen(content) > 0) {
        lv_label_set_text(chat_message_label_, content);
        ESP_LOGI(TAG, "Chat message set [%s]: %s", role ? role : "unknown", content);
    } else {
        ESP_LOGI(TAG, "Chat message cleared");
    }
    
    // 强制刷新显示
    lv_obj_invalidate(chat_message_label_);
}

void EmojiWidget::SetChatBoxVisible(bool visible)
{
    DisplayLockGuard lock(this);
    
    if (show_chat_box_ == visible) {
        return; // 状态没有改变，无需操作
    }
    
    show_chat_box_ = visible;
    
    if (chat_box_ != nullptr) {
        if (visible) {
            // 显示聊天框
            lv_obj_clear_flag(chat_box_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Chat box shown");
        } else {
            // 隐藏聊天框
            lv_obj_add_flag(chat_box_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Chat box hidden");
        }
    }
}

} // namespace anim 