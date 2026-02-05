# ==================== Codec 模块 ====================

set(CODEC_SRCS
    LengthHeaderCodec.cc
)

add_library(agv_codec STATIC ${CODEC_SRCS})

target_link_libraries(agv_codec
    lsk_muduo
)

target_include_directories(agv_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
