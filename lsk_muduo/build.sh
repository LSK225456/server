#!/bin/bash

# build.sh - lsk_muduo一键编译脚本
# 支持 Release 和 Debug 两种构建模式
# 自动检测并安装所需依赖

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
    --skip-deps         跳过依赖检测与安装
    
示例:
    $0                  # Release模式编译
    $0 -d               # Debug模式编译
    $0 -r               # 清理后重新编译
    $0 -j 8             # 使用8个并行任务
    $0 --skip-deps      # 跳过依赖安装直接编译

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
SKIP_DEPS=false

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
        --skip-deps)
            SKIP_DEPS=true
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
print_info "  lsk_muduo AGV 网关服务器编译脚本"
print_info "============================================="
print_info "构建类型: $BUILD_TYPE"
print_info "并行任务: $JOBS"
print_info "构建目录: $BUILD_DIR"
print_info "============================================="

# ==================== 依赖检测与安装 ====================

check_and_install_deps() {
    print_info "检测系统依赖..."
    
    local MISSING_PKGS=()
    
    # 检查 g++
    if ! command -v g++ &>/dev/null; then
        MISSING_PKGS+=(build-essential)
        print_warning "未检测到 g++，将安装 build-essential"
    else
        local GCC_VERSION=$(g++ -dumpversion | cut -d. -f1)
        if [[ "$GCC_VERSION" -lt 9 ]]; then
            print_warning "g++ 版本 $(g++ -dumpversion) 过低，建议 >= 9（C++17 支持）"
        else
            print_info "  ✓ g++ $(g++ -dumpversion)"
        fi
    fi
    
    # 检查 cmake
    if ! command -v cmake &>/dev/null; then
        MISSING_PKGS+=(cmake)
        print_warning "未检测到 cmake"
    else
        print_info "  ✓ cmake $(cmake --version | head -1 | awk '{print $3}')"
    fi
    
    # 检查 make
    if ! command -v make &>/dev/null; then
        MISSING_PKGS+=(make)
        print_warning "未检测到 make"
    else
        print_info "  ✓ make $(make --version | head -1 | awk '{print $3}')"
    fi
    
    # 检查 protobuf
    if ! pkg-config --exists protobuf 2>/dev/null && ! dpkg -s libprotobuf-dev &>/dev/null 2>&1; then
        MISSING_PKGS+=(libprotobuf-dev protobuf-compiler)
        print_warning "未检测到 protobuf 开发库"
    else
        local PROTO_VER=$(protoc --version 2>/dev/null | awk '{print $2}' || echo "unknown")
        print_info "  ✓ protobuf $PROTO_VER"
    fi
    
    # 安装缺失的依赖
    if [[ ${#MISSING_PKGS[@]} -gt 0 ]]; then
        print_info "安装缺失依赖: ${MISSING_PKGS[*]}"
        if command -v apt-get &>/dev/null; then
            sudo apt-get update -qq
            sudo apt-get install -y -qq "${MISSING_PKGS[@]}"
            print_success "依赖安装完成"
        elif command -v yum &>/dev/null; then
            # CentOS/RHEL 包名映射
            local YUM_PKGS=()
            for pkg in "${MISSING_PKGS[@]}"; do
                case "$pkg" in
                    build-essential) YUM_PKGS+=(gcc-c++ make) ;;
                    libprotobuf-dev) YUM_PKGS+=(protobuf-devel) ;;
                    protobuf-compiler) YUM_PKGS+=(protobuf-compiler) ;;
                    *) YUM_PKGS+=("$pkg") ;;
                esac
            done
            sudo yum install -y "${YUM_PKGS[@]}"
            print_success "依赖安装完成"
        else
            print_error "无法自动安装依赖，请手动安装: ${MISSING_PKGS[*]}"
            exit 1
        fi
    else
        print_success "所有依赖已就绪"
    fi
}

if [[ "$SKIP_DEPS" = false && "$ONLY_CLEAN" = false ]]; then
    check_and_install_deps
fi

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

# 列出生成的可执行文件
if [ -d "bin" ] && [ "$(ls -A bin/ 2>/dev/null)" ]; then
    print_info "生成的可执行文件:"
    for f in bin/*; do
        if [ -x "$f" ] && [ -f "$f" ]; then
            local_size=$(du -h "$f" | cut -f1)
            echo -e "  ${GREEN}✓${NC} $f ($local_size)"
        fi
    done
fi

# 列出生成的库文件
if [ -d "lib" ] && [ "$(ls -A lib/ 2>/dev/null)" ]; then
    print_info "生成的库文件:"
    for f in $(find lib/ -maxdepth 1 \( -name '*.a' -o -name '*.so*' \) 2>/dev/null); do
        if [ -f "$f" ]; then
            local_size=$(du -h "$f" | cut -f1)
            echo -e "  ${GREEN}✓${NC} $f ($local_size)"
        fi
    done
fi

print_success "============================================="
print_success "构建流程完成！"
print_success "============================================="
print_info ""
print_info "快速启动:"
print_info "  ./bin/gateway_main --port 9090        # 启动服务器"
print_info "  ./bin/test_lsk_server                 # 运行综合测试"
print_info ""
print_info "下次编译:"
print_info "  ./build.sh           # 增量编译"
print_info "  ./build.sh -r        # 完全重新编译"
print_info "  ./build.sh -d        # Debug模式编译"
print_info "============================================="

exit 0

