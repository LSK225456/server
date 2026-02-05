# ==================== Codec 模块 ====================

set(CODEC_SRCS
    LengthHeaderCodec.cc
)

add_library(agv_codec STATIC ${CODEC_SRCS})

target_link_libraries(agv_codec
    lsk_muduo_net
)

# 设置包含目录
target_include_directories(agv_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}  # 让其他模块能 #include "agv_server/codec/xxx.h"
)

# 输出日志
message(STATUS "Codec module configured")
