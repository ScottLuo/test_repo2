# main 组件（Make 构建方式使用）
COMPONENT_SRCS := main.c led_control.c led_fsm.c ap_logic.c wifi_ap.c led_flash.c http_service.c
COMPONENT_ADD_INCLUDE_DIRS := .
# HTTP 服务依赖 esp_http_server 组件
COMPONENT_DEPENDS := esp_http_server
