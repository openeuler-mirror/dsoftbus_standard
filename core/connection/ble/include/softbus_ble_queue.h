/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SOFTBUS_BLE_QUEUE_H
#define SOFTBUS_BLE_QUEUE_H

#include <stdint.h>

typedef struct {
    uint32_t halConnId;
    uint32_t connectionId;
    int32_t pid;
    int32_t flag;
    int32_t isServer;
    int32_t isInner;
    int32_t module;
    int32_t seq;
    int32_t len;
    const char *data;
} SendQueueNode;

int BleInnerQueueInit(void);
void BleInnerQueueDeinit(void);
int BleEnqueueNonBlock(const void *msg);
int BleDequeueNonBlock(void **msg);
#endif