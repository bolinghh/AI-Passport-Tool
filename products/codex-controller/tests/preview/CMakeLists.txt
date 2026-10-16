cmake_minimum_required(VERSION 3.16)
project(passport_screen_preview C CXX)
set(PRODUCT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PRODUCT}/managed_components/lvgl__lvgl" lvgl)
add_executable(screen_preview preview.c "${PRODUCT}/main/screen.c" "${PRODUCT}/main/screen_play.c"
    "${PRODUCT}/main/controller.c" "${PRODUCT}/main/play.c" "${PRODUCT}/main/wire.c"
    "${PRODUCT}/../../assets/fonts/passport_cjk_16.c"
    "${PRODUCT}/../../assets/fonts/passport_ui_20.c")
target_include_directories(screen_preview PRIVATE . "${PRODUCT}/main")
target_compile_options(screen_preview PRIVATE -std=c11 -Wall -Wextra -Werror)
target_link_libraries(screen_preview PRIVATE lvgl m)

