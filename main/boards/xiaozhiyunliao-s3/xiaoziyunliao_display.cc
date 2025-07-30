#include "xiaoziyunliao_display.h"
#include "xiaozhiyunliao_s3.h"
#include "assets/lang_config.h"
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/ledc.h>
#include <vector>
#include <esp_lvgl_port.h>
#include "board.h"
#include <string.h>
#include "gb2big.h"
#include <libs/gif/lv_gif.h>


#define TAG "YunliaoDisplay"

LV_FONT_DECLARE(font_awesome_30_4);
LV_FONT_DECLARE(time70);
LV_FONT_DECLARE(time50);
LV_FONT_DECLARE(time40);

#if defined(ja_jp)
    LV_FONT_DECLARE(font_noto_14_1_ja_jp);
    #define FONT_CONF &font_noto_14_1_ja_jp
#elif defined(en_us)
    LV_FONT_DECLARE(font_puhui_14_1);
    #define FONT_CONF &font_puhui_14_1
#else
    #define FONT_CONF fonts_.text_font
#endif

constexpr char CONSOLE_URL[] = "https://xiaozhi.me/console";
constexpr char WIFI_URL[] = "https://iot.espressif.cn/configWXDeviceWiFi.html";

XiaoziyunliaoDisplay::XiaoziyunliaoDisplay(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    gpio_num_t backlight_pin,
    bool backlight_output_invert,
    int width, int height,
    int offset_x, int offset_y,
    bool mirror_x, bool mirror_y,
    bool swap_xy,
    DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel,
                width, height,
                offset_x, offset_y,
                mirror_x, mirror_y,
                swap_xy,
                fonts){
        SetupUI();
        SetLogo(Lang::Strings::LOGO);
}

void XiaoziyunliaoDisplay::SetupUI() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    // 创建tabview，填充整个屏幕
    tabview_ = lv_tabview_create(lv_scr_act());
    lv_obj_set_size(tabview_, lv_pct(100), lv_pct(100));
    // 隐藏标签栏
    lv_tabview_set_tab_bar_position(tabview_, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview_, 0);
    lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tabview_);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);
    // 设置tabview的滚动捕捉模式，确保滑动后停留在固定位置
    lv_obj_t * content = lv_tabview_get_content(tabview_);
    lv_obj_set_scroll_snap_x(content, LV_SCROLL_SNAP_CENTER);
    
    // 创建两个TAB页面
    tab_main = lv_tabview_add_tab(tabview_, "TabMain");
    tab_idle = lv_tabview_add_tab(tabview_, "TabIdle");
    lv_obj_clear_flag(tab_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tab_main, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tab_idle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tab_idle, LV_SCROLLBAR_MODE_OFF);

    SetupTabMain();
    SetupTabIdle();
}

void XiaoziyunliaoDisplay::SetupTabMain() {
    DisplayLockGuard lock(this);

    lv_obj_set_style_text_font(tab_main, fonts_.text_font, 0);
    // lv_obj_set_style_text_color(tab_main, current_theme.text, 0);
    // lv_obj_set_style_bg_color(tab_main, current_theme.background, 0);

    /* Container */
    container_ = lv_obj_create(tab_main);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(container_, -13, -13);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);//-

    /* Status bar */
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_text_color(status_bar_, lv_color_make(0xAf, 0xAf, 0xAf), 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    
    /* Status bar */
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    logo_label_ = lv_label_create(status_bar_);
    lv_label_set_text(logo_label_, "");
    lv_obj_set_style_text_font(logo_label_, fonts_.text_font, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_pad_left(network_label_, 3, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_pad_left(battery_label_, 3, 0);
    lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN); // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY); // 子对象居中对齐，等距分布
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lv_color_black(), 0);
    lv_obj_set_style_text_color(content_, lv_color_white(), 0);

    NewChatPage();
}

void XiaoziyunliaoDisplay::SetupTabIdle() {
    lv_obj_set_style_text_font(tab_idle, fonts_.text_font, 0);
    lv_obj_set_style_text_color(tab_idle, lv_color_white(), 0);
    lv_obj_set_style_bg_color(tab_idle, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tab_idle, LV_OPA_COVER, 0); 
  
    // 时间标签容器
    lv_obj_t *time_container = lv_obj_create(tab_idle);
    lv_obj_remove_style_all(time_container);  // 移除所有默认样式
    lv_obj_set_size(time_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // 大小根据内容调整
    lv_obj_set_style_pad_all(time_container, 0, 0); // 无内边距
    lv_obj_set_style_bg_opa(time_container, LV_OPA_TRANSP, 0); // 透明背景
    lv_obj_set_style_border_width(time_container, 0, 0); // 无边框
    lv_obj_set_flex_flow(time_container, LV_FLEX_FLOW_ROW);// 设置为水平Flex布局
    lv_obj_set_flex_align(time_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(time_container, LV_ALIGN_TOP_MID, 0, 35);// 设置容器位置为屏幕中央
    
    // 创建小时标签
    lv_obj_t *hour_label = lv_label_create(time_container);
    lv_obj_set_style_text_font(hour_label, &time70, 0);
    lv_obj_set_style_text_color(hour_label, lv_color_white(), 0);
    lv_label_set_text(hour_label, "00 ");

    // 创建冒号标签
    lv_obj_t *colon_label = lv_label_create(time_container);
    lv_obj_set_style_text_font(colon_label, &time70, 0);
    lv_obj_set_style_text_color(colon_label, lv_color_white(), 0);
    lv_label_set_text(colon_label, ": ");

    // 创建分钟标签，使用橙色显示
    lv_obj_t *minute_label = lv_label_create(time_container);
    lv_obj_set_style_text_font(minute_label, &time70, 0);
    lv_obj_set_style_text_color(minute_label, lv_color_white(), 0); // 橙色
    lv_label_set_text(minute_label, "00");

    // Create date label directly on tab_idle
    lv_obj_t *date_label = lv_label_create(tab_idle);
    lv_obj_set_style_text_font(date_label, fonts_.text_font, 0);
    lv_obj_set_style_text_color(date_label, lv_color_white(), 0);
    lv_label_set_text(date_label, "01/01");
    lv_obj_align(date_label, LV_ALIGN_CENTER, -60, 10);
    
    // Create weekday label directly on tab_idle
    lv_obj_t *weekday_label = lv_label_create(tab_idle);
    lv_obj_set_style_text_font(weekday_label, fonts_.text_font, 0);
    lv_obj_set_style_text_color(weekday_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(weekday_label, LV_OPA_COVER, 0); 
    lv_obj_set_style_bg_color(weekday_label, lv_color_hex(0xFFA500), 0);
    lv_obj_set_style_radius(weekday_label, 3, 0);
    lv_obj_set_style_pad_all(weekday_label, 2, 0);  // 增加内边距
    lv_obj_set_width(weekday_label, 60);
    lv_obj_set_style_text_align(weekday_label, LV_TEXT_ALIGN_CENTER, 0);// 设置文本在标签内水平居中
    lv_label_set_text(weekday_label, "周一");
    lv_obj_align(weekday_label, LV_ALIGN_CENTER, 60, 10);
        
    
    // 定时器更新时间
    static lv_obj_t* hour_lbl = hour_label;
    static lv_obj_t* minute_lbl = minute_label;
    static lv_obj_t* date_lbl = date_label;
    static lv_obj_t* weekday_lbl = weekday_label;
    static lv_obj_t* colon_lbl = colon_label;

    lv_timer_create([](lv_timer_t *t) {
        if (!hour_lbl || !minute_lbl || 
            !date_lbl || !weekday_lbl) return;
        
        lv_lock();
        // Get current time
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // 格式化时、分、秒
        char hour_str[6];
        char minute_str[3];
        char second_str[3];
        
        sprintf(hour_str, "%02d ", timeinfo.tm_hour);
        sprintf(minute_str, "%02d", timeinfo.tm_min);
        sprintf(second_str, "%02d", timeinfo.tm_sec);
        
        // 更新时间标签
        lv_label_set_text(hour_lbl, hour_str);
        lv_label_set_text(minute_lbl, minute_str);
        if(timeinfo.tm_sec % 2 == 0){
            lv_obj_set_style_text_color(colon_lbl, lv_color_white(), 0);
        }else{
            lv_obj_set_style_text_color(colon_lbl, lv_color_black(), 0);
        }
        
        // Format date as MM-DD
        char date_str[25];
        snprintf(date_str, sizeof(date_str), "%d月%d日", timeinfo.tm_mon + 1, timeinfo.tm_mday);
        
        // Get day of week in Chinese
        const char *weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        
        // Update date and weekday labels
        lv_label_set_text(date_lbl, date_str);
        
        if (timeinfo.tm_wday >= 0 && timeinfo.tm_wday < 7) {
            lv_label_set_text(weekday_lbl, weekdays[timeinfo.tm_wday]);
        }
        
        lv_unlock();
    }, 1000, NULL);
}
void XiaoziyunliaoDisplay::ShowStandbyScreen(bool show) {
    if (tabview_) {
        // 在切换标签页前加锁，防止异常
        lv_lock();
        if (show){
            lv_tabview_set_act(tabview_, (uint32_t) PageIndex::PAGE_IDLE, LV_ANIM_OFF);
        } else {
            lv_tabview_set_act(tabview_, (uint32_t) PageIndex::PAGE_CHAT, LV_ANIM_OFF);
        }
        lv_unlock();
    }
}


void XiaoziyunliaoDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    SpiLcdDisplay::SetChatMessage(role, content);
    // lv_obj_scroll_to_view_recursive(chat_message_label_, LV_ANIM_OFF);

}

void XiaoziyunliaoDisplay::NewChatPage() {
    DisplayLockGuard lock(this);
    DelChatPage();

    if (isActivationStatus()) {
        // 创建带边框的二维码容器
        qr_container = lv_obj_create(content_);
        lv_obj_remove_style_all(qr_container);
        lv_obj_set_size(qr_container, 126, 126);
        lv_obj_set_style_border_color(qr_container, lv_color_white(), 0);
        lv_obj_set_style_border_width(qr_container, 3, 0);
        lv_obj_set_style_bg_color(qr_container, lv_color_black(), 0);
        lv_obj_set_style_pad_all(qr_container, 0, 0);

        // 创建控制台二维码
        console_qrcode_ = lv_qrcode_create(qr_container);
        lv_qrcode_set_size(console_qrcode_, 120);
        lv_qrcode_set_dark_color(console_qrcode_, lv_color_black());
        lv_qrcode_set_light_color(console_qrcode_, lv_color_white());
        lv_qrcode_update(console_qrcode_, CONSOLE_URL, strlen(CONSOLE_URL));
        lv_obj_center(console_qrcode_);

        if (emotion_label_) {
            lv_obj_del(emotion_label_);
            emotion_label_ = nullptr;
        }
    } else {
        // 创建表情标签
        emotion_label_ = lv_label_create(content_);
        lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
        lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
        
        if (console_qrcode_) {
            lv_obj_del(console_qrcode_);
            console_qrcode_ = nullptr;
        }
    }

    // 创建聊天消息标签
    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
}

void XiaoziyunliaoDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    std::string old_status = current_status_;
    current_status_ = status ? status : "";
    if ((old_status != Lang::Strings::ACTIVATION && isActivationStatus()) ||
        (old_status == Lang::Strings::ACTIVATION && !isActivationStatus())) {
        DelConfigPage();
        NewChatPage();
        lv_page_index = PageIndex::PAGE_CHAT;
    }
    LcdDisplay::SetStatus(status);    
}

void XiaoziyunliaoDisplay::SwitchPage(std::optional<PageIndex> target) {
    DisplayLockGuard lock(this);
    PageIndex new_page = target.value_or(
        (lv_page_index == PageIndex::PAGE_CHAT) ? 
        PageIndex::PAGE_CONFIG : PageIndex::PAGE_CHAT
    );

    const bool is_chat_page = (new_page == PageIndex::PAGE_CHAT);
    
    if (is_chat_page) {
        DelConfigPage();
        ShowChatPage();
    } else {
        HideChatPage();
        NewConfigPage();
    }
    lv_page_index = new_page;
}
void XiaoziyunliaoDisplay::NewSmartConfigPage() {
    DisplayLockGuard lock(this);
    DelConfigPage();
    ShowChatPage();
    if (emotion_label_) {
        lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (content_) {
        smartconfig_qrcode_ = lv_qrcode_create(content_);
        lv_qrcode_set_size(smartconfig_qrcode_, 100);
        lv_qrcode_set_dark_color(smartconfig_qrcode_, lv_color_white());
        lv_qrcode_set_light_color(smartconfig_qrcode_, lv_color_black());
        lv_qrcode_update(smartconfig_qrcode_, WIFI_URL, strlen(WIFI_URL));
    }
}

void XiaoziyunliaoDisplay::HideSmartConfigPage() {
    DisplayLockGuard lock(this);
    if (smartconfig_qrcode_) {
        lv_obj_add_flag(smartconfig_qrcode_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_) {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void XiaoziyunliaoDisplay::SetLogo(const char* logo) {
    DisplayLockGuard lock(this);
    if (logo_label_ && logo) {
        lv_label_set_text(logo_label_, logo);
    }
}

void XiaoziyunliaoDisplay::NewConfigPage() {
    DisplayLockGuard lock(this);
    DelConfigPage();
    
    // 创建配置页面
    config_container_ = lv_obj_create(content_);
    lv_obj_remove_style_all(config_container_);
    lv_obj_set_size(config_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(config_container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(config_container_, 10, 0);
    lv_obj_set_style_pad_top(config_container_, 23, 0);
    lv_obj_set_style_flex_main_place(config_container_, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(config_container_, LV_FLEX_ALIGN_CENTER, 0);

    uint8_t left_width =  170;
    uint8_t right_width = 140;
#if defined (zh_tw)
    left_width =  160;
    right_width = 140;
#elif defined (ja_jp)
    left_width =  140;
    right_width = 130;
#elif defined (en_us)
    left_width =  160;
    right_width = 140;
#endif


    // 左侧文本说明区
    config_text_panel_ = lv_label_create(config_container_);
    lv_obj_set_width(config_text_panel_, LV_HOR_RES - left_width);
    std::string hint_text = Lang::Strings::HINT1;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT2;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT3;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT4;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT5;
    hint_text += "\n";
    hint_text += Lang::Strings::HINT6;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT7;
    hint_text += "\n  ";
    hint_text += Lang::Strings::HINT8;
    lv_label_set_text(config_text_panel_, hint_text.c_str());
    lv_obj_set_style_text_font(config_text_panel_, FONT_CONF, 0);
    lv_label_set_long_mode(config_text_panel_, LV_LABEL_LONG_WRAP);

    /* 右侧二维码区 */
    right_container = lv_obj_create(config_container_);
    lv_obj_remove_style_all(right_container);
    lv_obj_set_size(right_container, right_width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(right_container, 5, 0);
    lv_obj_set_style_flex_main_place(right_container, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(right_container, LV_FLEX_ALIGN_CENTER, 0);

    qrcode_label_ = lv_label_create(right_container);
    std::string qrcode_text = Lang::Strings::LOGO;
    qrcode_text += Lang::Strings::VERSION3;
    qrcode_text += "\n";
    qrcode_text += Lang::Strings::HELP3;
    lv_label_set_text(qrcode_label_, qrcode_text.c_str());
    lv_obj_set_style_text_font(qrcode_label_, FONT_CONF, 0);
    lv_obj_set_style_text_line_space(qrcode_label_, 2, 0);
    lv_obj_set_style_text_align(qrcode_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(qrcode_label_, LV_PCT(100));

    // 直接创建二维码
    config_qrcode_panel_ = lv_qrcode_create(right_container);
    lv_qrcode_set_size(config_qrcode_panel_, 120);
    lv_qrcode_set_dark_color(config_qrcode_panel_, lv_color_white());
    lv_qrcode_set_light_color(config_qrcode_panel_, lv_color_black());
    lv_obj_center(config_qrcode_panel_);
    lv_qrcode_update(config_qrcode_panel_, CONSOLE_URL, strlen(CONSOLE_URL));
}

void XiaoziyunliaoDisplay::DelConfigPage() {
    DisplayLockGuard lock(this);
    if (config_text_panel_) {
        lv_obj_del(config_text_panel_);
        config_text_panel_ = nullptr;
    }
    if (config_qrcode_panel_) {
        lv_obj_del(config_qrcode_panel_);
        config_qrcode_panel_ = nullptr;
    }
    if (qrcode_label_) {
        lv_obj_del(qrcode_label_);
        qrcode_label_ = nullptr;
    }
    if (right_container) {
        lv_obj_del(right_container);
        right_container = nullptr;
    }
    if (config_container_) {
        lv_obj_del(config_container_);
        config_container_ = nullptr;
    }
}


void XiaoziyunliaoDisplay::HideChatPage() {
    DisplayLockGuard lock(this);
    if (qr_container) {
        lv_obj_add_flag(qr_container, LV_OBJ_FLAG_HIDDEN);
    }
    if (console_qrcode_) {
        lv_obj_add_flag(console_qrcode_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emotion_label_) {
        lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (chat_message_label_) {
        lv_obj_add_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void XiaoziyunliaoDisplay::ShowChatPage() {
    DisplayLockGuard lock(this);
    if (isActivationStatus()) {
        if (qr_container) {
            lv_obj_clear_flag(qr_container, LV_OBJ_FLAG_HIDDEN);
        }
        if (console_qrcode_) {
            lv_obj_clear_flag(console_qrcode_, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (emotion_label_) {
            lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (chat_message_label_) {
        lv_obj_clear_flag(chat_message_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void XiaoziyunliaoDisplay::DelChatPage() {
    DisplayLockGuard lock(this);
    if (qr_container) {
        lv_obj_del(qr_container);
        qr_container = nullptr;
    }
    if (console_qrcode_) {
        lv_obj_del(console_qrcode_);
        console_qrcode_ = nullptr;
    }
    if (emotion_label_) {
        lv_obj_del(emotion_label_);
        emotion_label_ = nullptr;
    }
    if (chat_message_label_) {
        lv_obj_del(chat_message_label_);
        chat_message_label_ = nullptr;
    }
}

bool XiaoziyunliaoDisplay::isActivationStatus() const {
    return current_status_ == Lang::Strings::ACTIVATION;
}

bool XiaoziyunliaoDisplay::isWifiConfigStatus() const {
    return current_status_ == Lang::Strings::WIFI_CONFIG_MODE;
}

void XiaoziyunliaoDisplay::UpdateStatusBar(bool update_all) {
    auto& board = Board::GetInstance();
    int battery_level;
    bool charging, discharging;
    
    // 添加电池标签显示逻辑
    if (battery_label_ && lv_obj_has_flag(battery_label_, LV_OBJ_FLAG_HIDDEN)
        && board.GetBatteryLevel(battery_level, charging, discharging) 
        && battery_level > 0) {
        lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 调用基类实现
    SpiLcdDisplay::UpdateStatusBar(update_all);
}

XiaoziyunliaoDisplay::~XiaoziyunliaoDisplay() {
    DisplayLockGuard lock(this);
    current_status_.clear();
    
    if (config_container_) { lv_obj_del(config_container_); config_container_ = nullptr; }
    if (container_) { lv_obj_del(container_); container_ = nullptr; }
    if (status_bar_) { lv_obj_del(status_bar_); status_bar_ = nullptr; }
    if (logo_label_) { lv_obj_del(logo_label_); logo_label_ = nullptr; }
    if (notification_label_) { lv_obj_del(notification_label_); notification_label_ = nullptr; }
    if (status_label_) { lv_obj_del(status_label_); status_label_ = nullptr; }
    if (mute_label_) { lv_obj_del(mute_label_); mute_label_ = nullptr; }
    if (network_label_) { lv_obj_del(network_label_); network_label_ = nullptr; }
    if (battery_label_) { lv_obj_del(battery_label_); battery_label_ = nullptr; }
    if (content_) { lv_obj_del(content_); content_ = nullptr; }
    if (smartconfig_qrcode_) { lv_obj_del(smartconfig_qrcode_); smartconfig_qrcode_ = nullptr; }
}

void XiaoziyunliaoDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    LcdDisplay::SetEmotion(emotion);
}
