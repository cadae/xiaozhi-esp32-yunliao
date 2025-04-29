#include "board.h"
#include "iot/thing.h"
#include "display/display.h"
#include "xiaoziyunliao_display.h"
#include <esp_log.h>

#define TAG "LCDScreen"

namespace iot {

class LCDScreen : public Thing {
public:
    LCDScreen() : Thing("LCDScreen", "屏幕控制") {

        properties_.AddNumberProperty("brightness", "当前亮度百分比", [this]() -> int {
            auto backlight = Board::GetInstance().GetBacklight();
            return backlight ? backlight->brightness() : 0;
        });

        methods_.AddMethod("SetBrightness", "设置亮度", ParameterList({
            Parameter("brightness", "0到100之间的整数", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            uint8_t brightness = static_cast<uint8_t>(parameters["brightness"].number());
            auto backlight = Board::GetInstance().GetBacklight();
            if (backlight) {
                backlight->SetBrightness(brightness, true);
            }
        });

        methods_.AddMethod("ShowHelpPage", "显示帮助页面", ParameterList(), [this](const ParameterList& parameters) {
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                auto lcd_display = static_cast<XiaoziyunliaoDisplay*>(display);
                lcd_display->SwitchPage(PageIndex::PAGE_CONFIG);
            }
        });

        methods_.AddMethod("ShowChatPage", "显示聊天页面", ParameterList(), [this](const ParameterList& parameters) {
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                auto lcd_display = static_cast<XiaoziyunliaoDisplay*>(display);
                lcd_display->SwitchPage(PageIndex::PAGE_CHAT);
            }
        });
    }
};

} // namespace iot

DECLARE_THING(LCDScreen);




