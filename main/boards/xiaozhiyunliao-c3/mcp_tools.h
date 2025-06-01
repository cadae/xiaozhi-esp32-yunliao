#ifndef __MCP_TOOLS_H__
#define __MCP_TOOLS_H__

#include "mcp_server.h"
#include "xiaoziyunliao_display.h"
#include "freertos/timers.h"
#include "xiaozhiyunliao_c3.h"

class McpTools {
private:
    McpTools(){
    }

    ~McpTools(){
    }

public:
    static McpTools& GetInstance() {
        static McpTools instance;
        instance.AddCommonTools();
        return instance;
    }

    void AddCommonTools() {
        auto& mcp_server = McpServer::GetInstance();

        // System control tools
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "Reboot the device and enter WiFi configuration mode,Requires user confirmation before execution.",
            PropertyList(), [](const PropertyList& properties) {
                auto board = static_cast<XiaoZhiYunliaoC3*>(&Board::GetInstance());
                board->ResetWifiConfiguration();
                return true;
            });

        mcp_server.AddTool("self.system.power_off",
            "Power off the device after 5-second delay,Requires user confirmation before execution.",
            PropertyList(), [this](const PropertyList& properties) {
                ESP_LOGI("McpTools", "Delaying power off for 5 seconds");
                vTaskDelay(pdMS_TO_TICKS(5000));
                auto board = static_cast<XiaoZhiYunliaoC3*>(&Board::GetInstance());
                board->Sleep();
                return true;
            });

        mcp_server.AddTool("self.system.restart",
            "Restart the device after 5-second delay,Requires user confirmation before execution.",
            PropertyList(), [this](const PropertyList& properties) {
                ESP_LOGI("McpTools", "Delaying restart for 5 seconds");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
                return true;
            });

        // Existing display tools
        mcp_server.AddTool("self.screen.show_help_page", 
            "Switch to the help/configuration page. Use this when user needs to access device settings or help information.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    auto lcd_display = static_cast<XiaoziyunliaoDisplay*>(display);
                    lcd_display->SwitchPage(PageIndex::PAGE_CONFIG);
                }
                return true;
            });

        mcp_server.AddTool("self.screen.show_chat_page", 
            "Switch to the main chat interface. Use this as the default view for normal conversation.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    auto lcd_display = static_cast<XiaoziyunliaoDisplay*>(display);
                    lcd_display->SwitchPage(PageIndex::PAGE_CHAT);
                }
                return true;
            });
    }
};


#endif // __MCP_TOOLS_H__
