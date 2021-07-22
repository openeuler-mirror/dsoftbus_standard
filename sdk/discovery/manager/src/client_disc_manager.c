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

#include "client_disc_manager.h"

#include "disc_server_proxy.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_mem_interface.h"

typedef struct {
    IPublishCallback publishCb;
    IDiscoveryCallback subscribeCb;
} DiscInfo;

static DiscInfo *g_discInfo = NULL;

int32_t PublishServiceInner(const char *packageName, const PublishInfo *info, const IPublishCallback *cb)
{
    g_discInfo->publishCb = *cb;
    int32_t ret = ServerIpcPublishService(packageName, info);
    if (ret != SOFTBUS_OK) {
        LOG_ERR("Server PublishService failed, ret = %d", ret);
        return ret;
    }
    return SOFTBUS_OK;
}

int32_t UnPublishServiceInner(const char *packageName, int32_t publishId)
{
    int32_t ret = ServerIpcUnPublishService(packageName, publishId);
    if (ret != SOFTBUS_OK) {
        LOG_ERR("Server UnPublishService failed, ret = %d", ret);
        return ret;
    }

    return SOFTBUS_OK;
}

int32_t StartDiscoveryInner(const char *packageName, const SubscribeInfo *info, const IDiscoveryCallback *cb)
{
    g_discInfo->subscribeCb = *cb;
    int32_t ret = ServerIpcStartDiscovery(packageName, info);
    if (ret != SOFTBUS_OK) {
        LOG_ERR("Server StartDiscovery failed, ret = %d", ret);
        return ret;
    }

    return SOFTBUS_OK;
}

int32_t StopDiscoveryInner(const char *packageName, int32_t subscribeId)
{
    int32_t ret = ServerIpcStopDiscovery(packageName, subscribeId);
    if (ret != SOFTBUS_OK) {
        LOG_ERR("Server StopDiscovery failed, ret = %d", ret);
        return ret;
    }

    return SOFTBUS_OK;
}

int32_t DiscClientInit(void)
{
    g_discInfo = (DiscInfo *)SoftBusCalloc(sizeof(DiscInfo));
    if (g_discInfo == NULL) {
        LOG_ERR("Calloc failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (DiscServerProxyInit() != SOFTBUS_OK) {
        LOG_ERR("disc server proxy init failed.");
        return SOFTBUS_ERR;
    }
    LOG_INFO("Init success");
    return SOFTBUS_OK;
}

void DiscClientDeinit(void)
{
    if (g_discInfo == NULL) {
        return;
    }
    SoftBusFree(g_discInfo);
    g_discInfo = NULL;
    LOG_INFO("DeInit success");
}

void DiscClientOnDeviceFound(const DeviceInfo *device)
{
    g_discInfo->subscribeCb.OnDeviceFound(device);
}

void DiscClientOnDiscoverySuccess(int32_t subscribeId)
{
    g_discInfo->subscribeCb.OnDiscoverySuccess(subscribeId);
}

void DiscClientOnDiscoverFailed(int32_t subscribeId, DiscoveryFailReason failReason)
{
    g_discInfo->subscribeCb.OnDiscoverFailed(subscribeId, failReason);
}

void DiscClientOnPublishSuccess(int32_t publishId)
{
    g_discInfo->publishCb.OnPublishSuccess(publishId);
}

void DiscClientOnPublishFail(int32_t publishId, PublishFailReason reason)
{
    g_discInfo->publishCb.OnPublishFail(publishId, reason);
}