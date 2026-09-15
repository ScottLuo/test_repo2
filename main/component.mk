#
# main 组件 Makefile（Make 构建方式使用）
# 显式声明组件源文件与头文件目录，避免默认约定遗漏。
#
COMPONENT_SRCS := main.c led_control.c led_fsm.c ap_logic.c wifi_ap.c
COMPONENT_ADD_INCLUDE_DIRS := .
