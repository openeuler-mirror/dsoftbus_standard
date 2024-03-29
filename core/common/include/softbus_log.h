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

#ifndef SOFTBUS_LOG_H
#define SOFTBUS_LOG_H

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include "softbus_adapter_log.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#if defined(__ICCARM__) || defined(__LITEOS_M__)
#define SOFTBUS_DPRINTF(fd, fmt, ...)
#else
#define SOFTBUS_DPRINTF(fd, fmt, ...) dprintf(fd, fmt, ##__VA_ARGS__)
#endif

typedef enum {
    SOFTBUS_LOG_AUTH,
    SOFTBUS_LOG_TRAN,
    SOFTBUS_LOG_CONN,
    SOFTBUS_LOG_LNN,
    SOFTBUS_LOG_DISC,
    SOFTBUS_LOG_COMM,
    SOFTBUS_LOG_MODULE_MAX,
} SoftBusLogModule;

void SoftBusLog(SoftBusLogModule module, SoftBusLogLevel level, const char *fmt, ...);
void NstackxLog(const char *moduleName, uint32_t nstackLevel, const char *format, ...);

void AnonyPacketPrintout(SoftBusLogModule module, const char *msg, const char *packet, size_t packetLen);

const char *AnonyDevId(char **outName, const char *inName);

#define UUID_ANONYMIZED_LENGTH 4
#define NETWORKID_ANONYMIZED_LENGTH 4
#define UDID_ANONYMIZED_LENGTH 4

const char *Anonymizes(const char *target, const uint8_t expectAnonymizedLength);

static inline const char *AnonymizesUUID(const char *input)
{
    return Anonymizes(input, UUID_ANONYMIZED_LENGTH);
}

static inline const char *AnonymizesNetworkID(const char *input)
{
    return Anonymizes(input, NETWORKID_ANONYMIZED_LENGTH);
}

static inline const char *AnonymizesUDID(const char *input)
{
    return Anonymizes(input, UDID_ANONYMIZED_LENGTH);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* SOFTBUS_LOG_H */
