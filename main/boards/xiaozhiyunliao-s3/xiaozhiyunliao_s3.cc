#include "wifi_board.h"
#include "codecs/es8388_audio_codec.h"
#include "xiaoziyunliao_display.h"
#include "xiaozhiyunliao_s3.h"
#include "application.h"
#include "button.h"
#include "settings.h"
#include "config.h"
#include "font_awesome_symbols.h"
#include <wifi_station.h>
#include <ssid_manager.h>
#include <esp_log.h>
// #include "i2c_device.h"
#include <driver/i2c_master.h>
#include <string.h> 
#include <wifi_configuration_ap.h>
#include <assets/lang_config.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/spi_common.h>

#define TAG "YunliaoS3"

LV_FONT_DECLARE(font_awesome_20_4);
LV_FONT_DECLARE(font_puhui_20_4);
#define FONT font_puhui_20_4

esp_lcd_panel_handle_t panel = nullptr;

XiaoZhiYunliaoS3::XiaoZhiYunliaoS3() 
    : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, BOOT_4G_PIN),
      boot_button_(BOOT_BUTTON_PIN, false, KEY_EXPIRE_MS),
      power_manager_(new PowerManager()){
    power_manager_->Start5V();
    power_manager_->Initialize();
    // power_manager_->Shutdown4G();
    InitializeI2c();
    Settings settings1("board", true);
    if(settings1.GetInt("sleep_flag", 0) > 0){
        vTaskDelay(pdMS_TO_TICKS(1000));
        if(boot_button_.getButtonLevel() == 1) {
            Sleep(); //进入休眠模式
        }else{
            settings1.SetInt("sleep_flag", 0);
        }
    }

    InitializeButtons();
    power_manager_->OnChargingStatusDisChanged([this](bool is_discharging) {
        if(power_save_timer_){
            if (is_discharging) {
                // ESP_LOGI(TAG, "SetShutdownEnabled");
                power_save_timer_->SetShutdownEnabled(true);
            } else {
                // ESP_LOGI(TAG, "SetShutdownDisabled");
                power_save_timer_->SetShutdownEnabled(false);
            }
        }
    });
    if(GetNetworkType() == NetworkType::WIFI){
        power_manager_->Shutdown4G();//缺省关闭
    }

    Settings settings("aec", false);
    auto& app = Application::GetInstance();
    app.SetAecMode(settings.GetInt("mode",kAecOnDeviceSide) == kAecOnDeviceSide ? kAecOnDeviceSide : kAecOff);

    InitializeSpi();
    InitializeLCDDisplay();
    if(GetAudioCodec()->output_volume() == 0){
        GetAudioCodec()->SetOutputVolume(70);
    }
    if(GetBacklight()->brightness() == 0){
        GetBacklight()->SetBrightness(60);
    }
    InitializePowerSaveTimer();

#if CONFIG_USE_BLUETOOTH
    bt_emitter_= new BT_Emitter(UART_NUM_1, ML307_RX_PIN, ML307_TX_PIN);
    bt_emitter_->setStatusCallback([this](int status){
        auto& app = Application::GetInstance();
        auto codec = static_cast<Es8388AudioCodec*>(GetAudioCodec());
        switch (status) {
            case BT_Emitter::BT_CONNECTED:
                ESP_LOGI(TAG, "蓝牙已连接");
                codec->EnablePA(false);
                if(app.GetAecMode() == kAecOnDeviceSide){
                    app.SetAecMode(kAecOff);
                    display_->ShowNotification(Lang::Strings::RTC_MODE_OFF);
                }
                break;
            case BT_Emitter::BT_DISCONNECTED:
                ESP_LOGI(TAG, "蓝牙未连接");
                codec->EnablePA(true);
                if(app.GetAecMode() == kAecOff){
                    // app.StopListening();
                    // app.SetDeviceState(kDeviceStateIdle);
                    app.SetAecMode(kAecOnDeviceSide);
                    display_->ShowNotification(Lang::Strings::RTC_MODE_ON);
                }
                break;
            case BT_Emitter::BT_NOT_INSTALLED:
                ESP_LOGI(TAG, "蓝牙未安装");
        }
    });
#endif
    ESP_LOGI(TAG, "Inited");
}


void XiaoZhiYunliaoS3::InitializePowerSaveTimer() {
    power_save_timer_ = new PowerSaveTimer(-1, 15, 600);//修改PowerSaveTimer为sleep=idle模式, shutdown=关机模式
    power_save_timer_->OnEnterSleepMode([this]() {
        // ESP_LOGI(TAG, "Enabling idle mode");
#if CONFIG_USE_MUSIC
        auto music = GetMusic();
        if (!music->IsDownloading()) {
            GetDisplay()->ShowStandbyScreen(true);
            GetBacklight()->SetBrightness(30);
        }
#else
        GetDisplay()->ShowStandbyScreen(true);
        GetBacklight()->SetBrightness(30);
#endif
    });
    power_save_timer_->OnExitSleepMode([this]() {
        // ESP_LOGI(TAG, "Exit idle mode");
        GetDisplay()->ShowStandbyScreen(false);
        GetBacklight()->RestoreBrightness();
    });
    power_save_timer_->OnShutdownRequest([this]() {
        ESP_LOGI(TAG, "Shutting down");
        Sleep();
    });
    power_save_timer_->SetEnabled(true);
}

void XiaoZhiYunliaoS3::PowerSaveTimerSetEnabled(bool enabled) {
    if(power_save_timer_){
        power_save_timer_->SetEnabled(enabled);
    }
}

Backlight* XiaoZhiYunliaoS3::GetBacklight() {
    static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    return &backlight;
}



Display* XiaoZhiYunliaoS3::GetDisplay() {
    return display_;
}

void XiaoZhiYunliaoS3::InitializeSpi() {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_SPI_PIN_MOSI;
    buscfg.miso_io_num = DISPLAY_SPI_PIN_MISO;
    buscfg.sclk_io_num = DISPLAY_SPI_PIN_SCLK;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
}


void XiaoZhiYunliaoS3::InitializeLCDDisplay() {
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    // 液晶屏控制IO初始化
    ESP_LOGD(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_SPI_PIN_LCD_CS;
    io_config.dc_gpio_num = DISPLAY_SPI_PIN_LCD_DC;
    io_config.spi_mode = 3;
    io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_LCD_HOST, &io_config, &panel_io));

    // 初始化液晶屏驱动芯片
    ESP_LOGD(TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_SPI_PIN_LCD_RST;
    panel_config.bits_per_pixel = 16;
    panel_config.rgb_ele_order = DISPLAY_RGB_ORDER_COLOR;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    display_ = new XiaoziyunliaoDisplay(
        panel_io,
        panel, 
        DISPLAY_BACKLIGHT_PIN, 
        DISPLAY_BACKLIGHT_OUTPUT_INVERT,
        DISPLAY_WIDTH, 
        DISPLAY_HEIGHT,
        DISPLAY_OFFSET_X, 
        DISPLAY_OFFSET_Y,
        DISPLAY_MIRROR_X, 
        DISPLAY_MIRROR_Y, 
        DISPLAY_SWAP_XY, 
        {                
            .text_font = &FONT,
            .icon_font = &font_awesome_20_4,
            .emoji_font = font_emoji_64_init(),
#if CONFIG_USE_WEATHER
            .weather_32_font = font_weather_32_init(),
#endif
        });
        std::string helpMessage = Lang::Strings::HELP4;
        helpMessage += "\n"; 
        switch (Application::GetInstance().GetAecMode()) {
            case kAecOff:
                helpMessage += Lang::Strings::RTC_MODE_OFF;
                break;
            case kAecOnServerSide:
            case kAecOnDeviceSide:
                helpMessage += Lang::Strings::RTC_MODE_ON;
                break;
            }    
        helpMessage += "\n"; 
        helpMessage += Lang::Strings::HELP1;
        helpMessage += "\n"; 
        helpMessage += Lang::Strings::HELP2;
        display_->SetChatMessage("system", helpMessage.c_str());
}

void XiaoZhiYunliaoS3::InitializeI2c() {
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
}

void XiaoZhiYunliaoS3::InitializeButtons() {
    boot_button_.OnClick([this]() {
        // ESP_LOGI(TAG, "Button OnClick");
        auto& app = Application::GetInstance();
#if CONFIG_USE_ALARM
        app.ToggleChatState();
#endif
        std::string wake_word=Lang::Strings::WAKE_WORD;
        app.WakeWordInvoke(wake_word);
    });
    boot_button_.OnPressDown([this]() {
        // ESP_LOGI(TAG, "Button OnPressDown");
        power_save_timer_->WakeUp();
    });
    boot_button_.OnPressUp([this]() {
        // ESP_LOGI(TAG, "Button OnPressUp");
    });
    boot_button_.OnLongPress([this]() {
        ESP_LOGI(TAG, "Button LongPress to Sleep");
        display_->SetStatus(Lang::Strings::SHUTTING_DOWN);
        display_->HideChatPage();
        display_->HideSmartConfigPage();
        display_->DelConfigPage();
        vTaskDelay(pdMS_TO_TICKS(2000));
        Sleep();
    });    
    boot_button_.OnDoubleClick([this]() {
        // ESP_LOGI(TAG, "Button OnDoubleClick");
        if (display_ && !wifi_config_mode_) {
            display_->SwitchPage();
        }
    });  
    boot_button_.OnThreeClick([this]() {
        ESP_LOGI(TAG, "Button OnThreeClick");
#if CONFIG_USE_DEVICE_AEC
        if (display_->GetPageIndex() == PageIndex::PAGE_CONFIG) {
            auto& app = Application::GetInstance();
            app.StopListening();
            app.SetDeviceState(kDeviceStateIdle);
            app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            Settings settings("aec", true);
            settings.SetInt("mode", app.GetAecMode());
            if(app.GetAecMode() == kAecOff){
                display_->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            }else{
                display_->ShowNotification(Lang::Strings::RTC_MODE_ON);
            }
            display_->SwitchPage();
            return;
        }
#endif
#if CONFIG_USE_BLUETOOTH
        if(GetNetworkType() == NetworkType::ML307){
            //4G已开机
            SwitchNetworkType();
            return;
        }
        if(bt_emitter_->checkStarted()){
            bt_emitter_->stop();//关蓝牙，打开AEC
            display_->ShowBT(false);
            getPowerManager()->Shutdown4G();
        }else{
            getPowerManager()->Start4G();
            BT_Emitter::modultype modultype = bt_emitter_->getModulType();
            if (modultype == BT_Emitter::modultype::MODUL_NONE) {
                modultype = bt_emitter_->checkALLModul();
            }
            if (modultype == BT_Emitter::modultype::MODUL_4G) {
                SwitchNetworkType();
            }else if (modultype == BT_Emitter::modultype::MODUL_BT) {
                bt_emitter_->start();//开蓝牙，关AEC
                display_->ShowBT(true);
            }
        }
#else
        SwitchNetworkType();
#endif
    });  
    boot_button_.OnFourClick([this]() {
        ESP_LOGI(TAG, "Button OnFourClick");
        if (display_->GetPageIndex() == PageIndex::PAGE_CONFIG) {
            ClearWifiConfiguration();
        }
        if (GetWifiConfigMode()) {
            SetFactoryWifiConfiguration();
        }
    });
}

#if CONFIG_USE_BLUETOOTH
    XiaoZhiYunliaoS3::BT_STATUS XiaoZhiYunliaoS3::SwitchBluetooth(bool switch_on){
        if (switch_on) {
            if (bt_emitter_->checkStarted()) {
                // 已开启蓝牙
                return BT_STATUS::ALREADY_STARTED;
            } else {
                getPowerManager()->Start4G();
                BT_Emitter::modultype modultype = bt_emitter_->checkBTModul();
                if (modultype == BT_Emitter::modultype::MODUL_BT) {
                    // 开启蓝牙
                    bt_emitter_->start();
                    display_->ShowBT(true);
                    return BT_STATUS::SUCCESS;
                } else {
                    // 未安装蓝牙模块
                    getPowerManager()->Shutdown4G();
                    return BT_STATUS::NO_BT_MODULE;
                }
            }
        } else {
            if (bt_emitter_->checkStarted()) {
                // 关闭蓝牙
                display_->ShowBT(false);
                bt_emitter_->stop();
                getPowerManager()->Shutdown4G();
                return BT_STATUS::SUCCESS;
            } else {
                // 已关闭
                getPowerManager()->Shutdown4G();
                return BT_STATUS::ALREADY_STOPPED;
            }
        }
    }
#endif

AudioCodec* XiaoZhiYunliaoS3::GetAudioCodec() {
    static Es8388AudioCodec audio_codec(
        codec_i2c_bus_, 
        I2C_NUM_0, 
        AUDIO_INPUT_SAMPLE_RATE, 
        AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_GPIO_MCLK, 
        AUDIO_I2S_GPIO_BCLK, 
        AUDIO_I2S_GPIO_WS, 
        AUDIO_I2S_GPIO_DOUT, 
        AUDIO_I2S_GPIO_DIN,
        AUDIO_CODEC_PA_PIN, 
        AUDIO_CODEC_ES8388_ADDR, 
        AUDIO_INPUT_REFERENCE);
    return &audio_codec;
}

bool XiaoZhiYunliaoS3::GetBatteryLevel(int &level, bool& charging, bool& discharging) {
    level = power_manager_->GetBatteryLevel();
    charging = power_manager_->IsCharging();
    discharging = power_manager_->IsDischarging();
    return true;
}

void XiaoZhiYunliaoS3::EnterWifiConfigMode() {
    ESP_LOGI(TAG, "EnterWifiConfigMode");
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();
    wifi_ap.StartSmartConfig();

    std::string hint = Lang::Strings::SCAN_QR;
    hint += Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += Lang::Strings::HINT_SHUTDOWN;
    
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "system", Lang::Sounds::OGG_WIFICONFIG);

    auto display = Board::GetInstance().GetDisplay();
    static_cast<XiaoziyunliaoDisplay*>(display)->NewSmartConfigPage();
    
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void XiaoZhiYunliaoS3::Sleep() {
    ESP_LOGI(TAG, "Entering deep sleep");
    Settings settings("board", true);
    settings.SetInt("sleep_flag", 1);

    Application::GetInstance().StopListening();
    if (auto* codec = GetAudioCodec()) {
        codec->EnableOutput(false);
        codec->EnableInput(false);
    }
    GetBacklight()->SetBrightness(0);
    if (panel) {
        esp_lcd_panel_disp_on_off(panel, false);
    }
    power_manager_->Shutdown4G();
    power_manager_->Shutdown5V();
    power_manager_->MCUSleep();
}



std::string XiaoZhiYunliaoS3::GetHardwareVersion() const {
    std::string version = Lang::Strings::LOGO;
    version += Lang::Strings::VERSION3;
    return version;
}

void XiaoZhiYunliaoS3::SetPowerSaveMode(bool enabled) {
    if (!enabled) {
        power_save_timer_->WakeUp();
    }
    DualNetworkBoard::SetPowerSaveMode(enabled);
}


DECLARE_BOARD(XiaoZhiYunliaoS3);
