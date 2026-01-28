set(ASTRA_DIR ${CMAKE_CURRENT_LIST_DIR}/../third_party/oled-ui-astra/Core/Src)
set(U8G2_DIR ${ASTRA_DIR}/hal/hal_dreamCore/components/oled/graph_lib/u8g2)

file(GLOB U8G2_SRC "${U8G2_DIR}/*.c")

set(ASTRA_SRC
    ${ASTRA_DIR}/hal/hal.cpp
    ${ASTRA_DIR}/astra/ui/launcher.cpp
    ${ASTRA_DIR}/astra/ui/item/menu/menu.cpp
    ${ASTRA_DIR}/astra/ui/item/selector/selector.cpp
    ${ASTRA_DIR}/astra/ui/item/camera/camera.cpp
    ${ASTRA_DIR}/astra/ui/item/widget/widget.cpp
    ${ASTRA_DIR}/astra/ui/item/plugin/plugin.cpp
)

idf_component_register(
    SRCS
        "main.cpp"
        "hal_astra_esp32.cpp"
        "astra_animation.cpp"
        "u8g2_font_zpix.c"
        ${ASTRA_SRC}
        ${U8G2_SRC}
    INCLUDE_DIRS
        "."
        "${ASTRA_DIR}"
        "${ASTRA_DIR}/hal"
        "${ASTRA_DIR}/astra"
        "${U8G2_DIR}"
)
