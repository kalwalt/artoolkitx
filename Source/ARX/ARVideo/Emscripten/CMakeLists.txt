set(SOURCE
    ${SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/videoEmscripten.h
    ${CMAKE_CURRENT_SOURCE_DIR}/videoEmscripten.c
    PARENT_SCOPE
)

set(DEFINES
    ${DEFINES}
    ARVIDEO_INPUT_EMSCRIPTEN
    PARENT_SCOPE
)
