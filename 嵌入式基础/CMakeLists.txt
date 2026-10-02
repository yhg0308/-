# FinalWork —— FOCUS 2026 秋季招新控制组考核 阶段二（嵌入式基础 / 方向二：ESP-IDF）
#
# 工程按"外设 / 功能模块"拆分：所有外设驱动都在 components/BSP 下按外设一个文件夹，
# BSP 整体作为单个组件，由 components/BSP/CMakeLists.txt 统一注册下面的所有子目录；
# main 只放应用入口，不堆外设代码（PDF 提交格式要求）。

cmake_minimum_required(VERSION 3.16)

add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FinalWork)
