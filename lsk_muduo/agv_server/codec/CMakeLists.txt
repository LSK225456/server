# ==================== Codec 编解码模块 ====================

message(STATUS "Configuring Codec module...")

set(CODEC_SRCS
    LengthHeaderCodec.cc
)

add_library(agv_codec STATIC ${CODEC_SRCS})

target_include_directories(agv_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(agv_codec
    lsk_muduo_net
    lsk_muduo_base
)

message(STATUS "Codec module configured")
