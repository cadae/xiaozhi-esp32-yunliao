#ifndef XIAOZIYUNLIAO_DISPLAY_H
#define XIAOZIYUNLIAO_DISPLAY_H

#include "display/lcd_display.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <font_emoji.h>
#include <optional>
#include <atomic>
#include <string>
#include <mutex>

enum class PageIndex {
    PAGE_CHAT = 0,
    PAGE_IDLE = 1,
    PAGE_CONFIG = 2
};

class XiaoziyunliaoDisplay : public SpiLcdDisplay {
public:

    XiaoziyunliaoDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  gpio_num_t backlight_pin, bool backlight_output_invert,
                  int width, int height,  int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                  DisplayFonts fonts);
    ~XiaoziyunliaoDisplay() override;

    void SetupUI() override;
    void SetLogo(const char* logo);
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override; 
    void SetEmotion(const char* emotion) override;
    void ShowStandbyScreen(bool show) override;

    lv_timer_t *idle_timer_ = nullptr;
    lv_obj_t * tab_main = nullptr;
    lv_obj_t * tab_idle = nullptr;
    lv_obj_t * tab_config = nullptr;
    lv_obj_t * tabview_ = nullptr;
    void SetupTabMain();
    void SetupTabIdle();

    PageIndex GetPageIndex() const { return lv_page_index; }
    void SwitchPage(std::optional<PageIndex> target = std::nullopt);
    void NewSmartConfigPage();
    void HideSmartConfigPage();
    void NewConfigPage();
    void DelConfigPage();
    void NewChatPage();
    void DelChatPage();
    void ShowChatPage();
    void HideChatPage();
    bool isActivationStatus() const;
    bool isWifiConfigStatus() const;
    const std::string& GetCurrentStatus() const { return current_status_; }
protected:
    lv_obj_t *logo_label_ = nullptr;
    lv_obj_t* config_container_ = nullptr;
    lv_obj_t* config_text_panel_ = nullptr;
    lv_obj_t* right_container = nullptr;
    lv_obj_t* qrcode_label_ = nullptr;
    lv_obj_t* config_qrcode_panel_ = nullptr;
    lv_obj_t* smartconfig_qrcode_ = nullptr;
    lv_obj_t* qr_container = nullptr;
    lv_obj_t* console_qrcode_ = nullptr;
    PageIndex lv_page_index = PageIndex::PAGE_CHAT;
    std::mutex status_mutex_;
    std::string current_status_ = "";

    void UpdateStatusBar(bool update_all = false) override;
};

#endif // XIAOZIYUNLIAO_DISPLAY_H 