# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_REALTEK_USING_PPT_SYNC_MASTER)
  zephyr_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/libppt_sync_master.a)
endif()

if(CONFIG_REALTEK_USING_PPT_SYNC_SLAVE)
  zephyr_link_libraries(${CMAKE_CURRENT_SOURCE_DIR}/libppt_sync_slave.a)
endif()
