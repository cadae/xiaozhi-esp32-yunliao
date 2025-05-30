#include "board.h"
#include "xiaozhiyunliao_s3.h"
#include "iot/thing.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include <esp_log.h>
#include "application.h"
#include "esp_system.h"
#include "system_info.h"
#include <wifi_station.h>

#define TAG "BoardControl"

namespace iot {

class BoardControl : public Thing {
private:
    TimerHandle_t sleep_timer_;
    enum class TimerMode { SLEEP, RESTART }; 
    TimerMode timer_mode_; 
    static constexpr int MAX_CHECK_COUNT = 100; // 5秒 / 100ms = 50次检查

    std::string GetFirmwareVersion() const {
        return Application::GetInstance().getOta().GetFirmwareVersion();
    }

    std::string GetCurrentVersion() const {
        return Application::GetInstance().getOta().GetCurrentVersion();
    }

    bool HasNewVersion() const {
        return Application::GetInstance().getOta().HasNewVersion();
    }

    static void SleepTimerCallback(TimerHandle_t xTimer) {
        BoardControl* instance = static_cast<BoardControl*>(pvTimerGetTimerID(xTimer));
        if (instance->timer_mode_ == TimerMode::SLEEP) {
            ESP_LOGI(TAG, "开始检查设备状态，准备进入睡眠模式");
            xTaskCreate([](void* pvParameters) {
                BoardControl* instance = static_cast<BoardControl*>(pvParameters);
                auto board = static_cast<XiaoZhiYunliaoS3*>(&Board::GetInstance());
                auto& app = Application::GetInstance();
                
                int check_count = 0;
                while (check_count < instance->MAX_CHECK_COUNT) {
                    if (app.GetDeviceState() == kDeviceStateIdle) {
                        ESP_LOGI(TAG, "设备空闲，立即进入睡眠模式");
                        board->Sleep();
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                    check_count++;
                }
                if (check_count >= instance->MAX_CHECK_COUNT) {
                    ESP_LOGI(TAG, "检查超时，设备进入睡眠模式");
                    board->Sleep();
                }
                vTaskDelete(NULL);
            }, "StateCheckTask", 2048, instance, 1, NULL);
        } else {
            ESP_LOGI(TAG, "System restarting after delay");
            esp_restart();
        }
    }

public:
    BoardControl() : Thing("BoardControl", ""), timer_mode_(TimerMode::SLEEP) {

        sleep_timer_ = xTimerCreate("SleepTimer", pdMS_TO_TICKS(5000), pdFALSE, this, SleepTimerCallback);

        // 添加网络信号质量
        properties_.AddNumberProperty("Network", "网络信号质量", [this]() -> int {
            auto& wifi_station = WifiStation::GetInstance();
            if (wifi_station.IsConnected()) {
                return wifi_station.GetRssi();
            }else{
                return 0;
            }
        });
        
        properties_.AddNumberProperty("Battery", "电量百分比", [this]() -> int {
            int level = 0;
            bool charging = false;
            bool discharging = false;
            Board::GetInstance().GetBatteryLevel(level, charging, discharging);
            // ESP_LOGI(TAG, "当前电池电量: %d%%, 充电状态: %s", level, charging ? "充电中" : "未充电");
            return level;
        });

        properties_.AddBooleanProperty("Charging", "是否正在充电", [this]() -> bool {
            int level = 0;
            bool charging = false;
            bool discharging = false;
            Board::GetInstance().GetBatteryLevel(level, charging, discharging);
            // ESP_LOGI(TAG, "当前电池电量: %d%%, 充电状态: %s", level, charging ? "充电中" : "未充电");
            return charging;
        });

        properties_.AddStringProperty("HardwareVer", "硬件版本", 
            [this]() -> std::string { 
                auto& board = Board::GetInstance();
                return board.GetHardwareVersion();
            });

        // // properties_.AddStringProperty("FirmwareVersion", "最新固件版本", 
        // //     [this]() -> std::string { return GetFirmwareVersion(); });

        properties_.AddStringProperty("FirmwareVer", "固件版本",
            [this]() -> std::string { return GetCurrentVersion(); });

        // // properties_.AddBooleanProperty("HasNewVersion", "是否有新固件版本",
        // //     [this]() -> bool { return HasNewVersion(); });

        properties_.AddStringProperty("MACAddr", "设备MAC地址",
            []() -> std::string { return SystemInfo::GetMacAddress(); });

        // // properties_.AddNumberProperty("FlashSize", "闪存容量(MB)",
        // //     []() -> int { return SystemInfo::GetFlashSize() / 1024 / 1024; });

        // properties_.AddStringProperty("ChipModel", "芯片型号",
        //     []() -> std::string { return SystemInfo::GetChipModelName(); });

        // // properties_.AddNumberProperty("FreeHeap", "可用内存",
        // //     []() -> int { return SystemInfo::GetFreeHeapSize(); });

        methods_.AddMethod("Sleep", "设备关机，需确认", ParameterList(), 
            [this](const ParameterList& parameters) {
                ESP_LOGI(TAG, "Delaying sleep for 5 seconds");
                if (sleep_timer_ != NULL) {
                    timer_mode_ = TimerMode::SLEEP;  // 设置模式为休眠
                    xTimerStart(sleep_timer_, 0);
                }
            });

        methods_.AddMethod("Restart", "重启设备，需确认", ParameterList(),
            [this](const ParameterList& parameters) {
                ESP_LOGI(TAG, "Delaying restart for 5 seconds");
                if (sleep_timer_ != NULL) {
                    timer_mode_ = TimerMode::RESTART;  // 设置模式为重启
                    xTimerStart(sleep_timer_, 0);
                }
            });

        methods_.AddMethod("ResetWifi", "重新配网，需确认", ParameterList(), 
            [this](const ParameterList& parameters) {
                ESP_LOGI(TAG, "ResetWifiConfiguration");
                auto board = static_cast<XiaoZhiYunliaoS3*>(&Board::GetInstance());
                board->ResetWifiConfiguration();
            });

        // methods_.AddMethod("UpdateFirmware", "立即更新固件", ParameterList(),
        //     [this](const ParameterList& parameters) -> bool {
        //         return Application::GetInstance().UpdateNewVersion();
        //     });

    }
    
    ~BoardControl() {
        if (sleep_timer_ != NULL) {
            xTimerDelete(sleep_timer_, 0);
        }
    }
};

} // namespace iot

DECLARE_THING(BoardControl); 