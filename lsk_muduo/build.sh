#!/bin/bash

# build.sh - lsk_muduo一键编译脚本
# 支持 Release 和 Debug 两种构建模式

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的信息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示使用说明
show_usage() {
    cat << EOF
用法: $0 [选项]

选项:
    -h, --help          显示此帮助信息
    -d, --debug         Debug模式编译（默认Release）
    -r, --rebuild       清理后重新编译
    -c, --clean         仅清理编译产物
    -j N                使用N个并行任务编译（默认：CPU核心数）
    -i, --install       编译后安装到系统
    --test              编译测试程序
    
示例:
    $0                  # Release模式编译
    $0 -d               # Debug模式编译
    $0 -r               # 清理后重新编译
    $0 -j 8             # 使用8个并行任务
    $0 -d --test        # Debug模式编译并构建测试

EOF
}

# 获取脚本所在目录（项目根目录）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 默认参数
BUILD_TYPE="Release"
BUILD_DIR="build"
CLEAN_BUILD=false
ONLY_CLEAN=false
JOBS=$(nproc 2>/dev/null || echo 4)  # 默认使用所有CPU核心
ENABLE_INSTALL=false
BUILD_TESTS=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--rebuild)
            CLEAN_BUILD=true
            shift
            ;;
        -c|--clean)
            ONLY_CLEAN=true
            shift
            ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        -i|--install)
            ENABLE_INSTALL=true
            shift
            ;;
        --test)
            BUILD_TESTS=true
            shift
            ;;
        *)
            print_error "未知选项: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Banner
print_info "============================================="
print_info "  lsk_muduo 网络库编译脚本"
print_info "============================================="
print_info "构建类型: $BUILD_TYPE"
print_info "并行任务: $JOBS"
print_info "构建目录: $BUILD_DIR"
if [ "$BUILD_TESTS" = true ]; then
    print_info "测试程序: 启用"
fi
print_info "============================================="

# 清理函数
do_clean() {
    print_info "清理编译产物..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "已删除构建目录: $BUILD_DIR"
    fi
    if [ -d "lib" ]; then
        rm -rf lib/*.so*
        print_success "已清理lib目录"
    fi
    if [ -d "bin" ]; then
        rm -rf bin/*
        print_success "已清理bin目录"
    fi
}

# 如果只是清理，执行后退出
if [ "$ONLY_CLEAN" = true ]; then
    do_clean
    print_success "清理完成"
    exit 0
fi

# 如果需要重新编译，先清理
if [ "$CLEAN_BUILD" = true ]; then
    do_clean
fi

# 创建构建目录
print_info "创建构建目录..."
mkdir -p "$BUILD_DIR"
mkdir -p "lib"
mkdir -p "bin"

# 检查并清理CMake缓存（处理不同用户或路径变化的情况）
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CURRENT_PATH=$(pwd)
    CACHED_PATH=$(grep "CMAKE_HOME_DIRECTORY" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo "")
    CACHED_BUILD_TYPE=$(grep "CMAKE_BUILD_TYPE" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2 || echo "")
    
    # 如果缓存路径或构建类型与当前配置不匹配，删除缓存
    if [ -n "$CACHED_PATH" ] && ([ "$CACHED_PATH" != "$CURRENT_PATH" ] || [ "$CACHED_BUILD_TYPE" != "$BUILD_TYPE" ]); then
        print_warning "检测到CMake缓存配置不匹配，自动清理..."
        rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
    fi
fi

# 进入构建目录
cd "$BUILD_DIR"

# 配置CMake
print_info "配置CMake项目..."
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [ "$BUILD_TESTS" = true ]; then
    CMAKE_ARGS+=(-DBUILD_TESTS=ON)
fi

cmake "${CMAKE_ARGS[@]}" .. || {
    print_error "CMake配置失败"
    exit 1
}

# 编译
print_info "开始编译（使用 $JOBS 个并行任务）..."
make -j"$JOBS" || {
    print_error "编译失败"
    exit 1
}

# 返回项目根目录
cd "$SCRIPT_DIR"

# 显示编译结果
print_success "============================================="
print_success "编译成功！"
print_success "============================================="

if [ -f "lib/liblsk_muduo.so" ]; then
    LIB_SIZE=$(du -h lib/liblsk_muduo.so | cut -f1)
    print_info "生成库文件: lib/liblsk_muduo.so ($LIB_SIZE)"
    print_info "库文件详情:"
    ls -lh lib/liblsk_muduo.so* 2>/dev/null || true
else
    print_warning "未找到生成的库文件"
fi

# 安装（如果指定）
if [ "$ENABLE_INSTALL" = true ]; then
    print_info "安装到系统..."
    cd "$BUILD_DIR"
    sudo make install || {
        print_error "安装失败"
        exit 1
    }
    cd "$SCRIPT_DIR"
    print_success "安装完成"
fi

print_success "============================================="
print_success "构建流程完成！"
print_success "============================================="
print_info "使用说明:"
print_info "  1. 库文件位于: lib/liblsk_muduo.so"
print_info "  2. 链接时使用: -L./lib -llsk_muduo -lpthread -lrt"
print_info "  3. 包含头文件: -I./muduo"
print_info ""
print_info "下次编译可使用:"
print_info "  ./build.sh           # 增量编译"
print_info "  ./build.sh -r        # 完全重新编译"
print_info "  ./build.sh -d        # Debug模式编译"
print_info "============================================="

exit 0

