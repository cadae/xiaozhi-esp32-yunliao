#ifndef XIAOZHIYUNLIAO_S3_H
#define XIAOZHIYUNLIAO_S3_H

#include "dual_network_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "button.h"
#include "iot/thing_manager.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "xiaoziyunliao_display.h"
#include "power_save_timer.h"
#include "power_manager.h"

class XiaoziyunliaoDisplay;

class XiaoZhiYunliaoS3 : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    
#if defined(CONFIG_LCD_CONTROLLER_ILI9341) || defined(CONFIG_LCD_CONTROLLER_ST7789)
    XiaoziyunliaoDisplay* display_;
    
    void InitializeSpi();
    void InitializeLCDDisplay();
#endif

    void InitializeI2c();
    void InitializeButtons();
    void InitializeIot();
    void InitializePowerSaveTimer();

public:
    XiaoZhiYunliaoS3();
    virtual ~XiaoZhiYunliaoS3() = default;

#if defined(CONFIG_LCD_CONTROLLER_ILI9341) || defined(CONFIG_LCD_CONTROLLER_ST7789)
    virtual Display* GetDisplay() override;
#endif
    virtual Backlight* GetBacklight() override;
    virtual AudioCodec* GetAudioCodec() override;
    bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override;
    // void EnterWifiConfigMode() override;
    void Sleep();
    void SetPressToTalkEnabled(bool enabled);
    std::string GetHardwareVersion() const;
    void PowerSaveTimerSetEnabled(bool enabled);
};

#endif // XIAOZHIYUNLIAO_S3_H 