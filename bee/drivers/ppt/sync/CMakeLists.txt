# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

# add_subdirectory(lib)
# Compile from source so CONFIG_PPT_DRIVER_LOG_LEVEL takes effect at build time.
set(PPT_SYNC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PPT_DRV_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../driver)

zephyr_library_named(ppt_sync)

zephyr_library_sources(${PPT_DRV_SRC}/common/ppt_driver.c)
zephyr_library_sources(${PPT_DRV_SRC}/simple/ppt_simple.c)
zephyr_library_sources(${PPT_SYNC_SRC}/platform_list.c)
zephyr_library_sources(${PPT_SYNC_SRC}/ppt_sync.c)
zephyr_library_sources(${PPT_SYNC_SRC}/ppt_sync_pendcall.c)
zephyr_library_sources(${PPT_SYNC_SRC}/ppt_sync_master.c)
zephyr_library_sources(${PPT_SYNC_SRC}/ppt_sync_slave.c)

zephyr_include_directories(inc)
zephyr_include_directories(${PPT_SYNC_SRC})
zephyr_include_directories(${PPT_DRV_SRC}/common)
zephyr_include_directories(${PPT_DRV_SRC}/common/rtl87x2g)
zephyr_include_directories(${PPT_DRV_SRC}/simple)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../rtl8752h/boot/inc)
