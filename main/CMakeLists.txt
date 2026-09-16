# main 组件构建脚本（CMake 构建方式使用）
# 注册本组件的所有源文件与头文件目录
# 注：esp_http_server 等 SDK 内置组件由 ESP8266 SDK 默认链接，无需额外 REQUIRES。
idf_component_register(SRCS "main.c" "led_control.c" "led_fsm.c" "ap_logic.c" "wifi_ap.c"
                       "led_flash.c" "http_service.c"
                       INCLUDE_DIRS ".")
