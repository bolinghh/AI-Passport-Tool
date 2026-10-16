idf_component_register(
    SRCS "main.c" "controller.c" "play.c" "play_store.c" "wire.c" "link.c" "capture.c" "screen.c" "screen_play.c"
         "../../../assets/fonts/passport_cjk_16.c" "../../../assets/fonts/passport_ui_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer esp-opus
)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_att_svr_register")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,__wrap_ble_att_svr_register")
