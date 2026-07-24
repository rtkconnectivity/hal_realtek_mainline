# Copyright (c) 2024 Realtek Semiconductor Corp.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_REALTEK_USING_PPT_SYNC_MASTER OR CONFIG_REALTEK_USING_PPT_SYNC_SLAVE)
    add_subdirectory(sync)
endif()
