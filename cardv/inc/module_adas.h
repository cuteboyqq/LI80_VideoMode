/*
 * module_adas.h- Sigmastar
 *
 * Copyright (C) 2021 Sigmastar Technology Corp.
 *
 * Author: jeff.li <jeff.li@sigmastar.com.cn>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MODULE_ADAS_H__
#define __MODULE_ADAS_H__

#include "module_config.h"
#include "mi_sys.h"
#include <chrono>           //wnc add
#include "wnc_adas.hpp"     //wnc add
#include "dataStructures.h" //wnc add

//==============================================================================
//
//                              MACRO DEFINES
//
//==============================================================================

#define ADAS_DBG_MSG(enable, format, args...) \
    do                                        \
    {                                         \
        if (enable)                           \
            printf(format, ##args);           \
    } while (0)
#define ADAS_DBG_ERR(enable, format, args...) \
    do                                        \
    {                                         \
        if (1)                                \
            printf(format, ##args);           \
    } while (0)

#define ADAS_DRAW_INFO (1 && CARDV_FB_ENABLE)

#define LDWS_EN (0x1)
#define FCWS_EN (0x2)
#define SAG_EN (0x4)
#define BSD_EN (0x8)

//==============================================================================
//
//                              Structure
//
//==============================================================================

typedef struct ADAS_ATTR_s
{
    struct
    {
        MI_U8 ldws;
        MI_U8 fcws;
        MI_U8 sag;
        MI_U8 bsd;
        MI_U8 bcws;
    } feature;

    MI_SYS_ChnPort_t stSrcChnPort;
} ADAS_ATTR_t;

//==============================================================================
//
//                              EXTERN GLOBAL VARIABLES
//
//==============================================================================

extern int         adas_print_msg;
extern ADAS_ATTR_t gFrontAdasAttr;
extern ADAS_ATTR_t gRearAdasAttr;

bool ADAS_IsAnyFeatureEnable(ADAS_ATTR_t* adas_attr);

#endif /* __MODULE_ADAS_H__ */