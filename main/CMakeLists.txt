# main 组件构建脚本（CMake 构建方式使用）
# 注册本组件的所有源文件与头文件目录
idf_component_register(SRCS "main.c" "led_control.c" "led_fsm.c" "ap_logic.c" "wifi_ap.c"
                       INCLUDE_DIRS ".")
