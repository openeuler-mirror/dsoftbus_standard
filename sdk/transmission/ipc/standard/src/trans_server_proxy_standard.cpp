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

#include "trans_server_proxy_standard.h"

#include "message_parcel.h"
#include "softbus_errcode.h"
#include "softbus_ipc_def.h"
#include "softbus_log.h"

namespace OHOS {
int32_t TransServerProxy::StartDiscovery(const char *pkgName, const SubscribeInfo *subInfo)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::StopDiscovery(const char *pkgName, int subscribeId)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::PublishService(const char *pkgName, const PublishInfo *pubInfo)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::UnPublishService(const char *pkgName, int publishId)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::SoftbusRegisterService(const char *clientPkgName, const sptr<IRemoteObject>& object)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::CreateSessionServer(const char *pkgName, const char *sessionName)
{
    if (pkgName == nullptr || sessionName == nullptr) {
        return SOFTBUS_ERR;
    }
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        LOG_ERR("remote is nullptr!");
        return SOFTBUS_ERR;
    }

    MessageParcel data;
    if (!data.WriteCString(pkgName)) {
        LOG_ERR("CreateSessionServer write pkg name failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteCString(sessionName)) {
        LOG_ERR("CreateSessionServer write session name failed!");
        return SOFTBUS_ERR;
    }
    MessageParcel reply;
    MessageOption option;
    if (remote->SendRequest(SERVER_CREATE_SESSION_SERVER, data, reply, option) != 0) {
        LOG_ERR("CreateSessionServer send request failed!");
        return SOFTBUS_ERR;
    }
    int32_t serverRet = 0;
    if (!reply.ReadInt32(serverRet)) {
        LOG_ERR("CreateSessionServer read serverRet failed!");
        return SOFTBUS_ERR;
    }
    return serverRet;
}

int32_t TransServerProxy::RemoveSessionServer(const char *pkgName, const char *sessionName)
{
    if (pkgName == nullptr || sessionName == nullptr) {
        return SOFTBUS_ERR;
    }
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        LOG_ERR("remote is nullptr!");
        return SOFTBUS_ERR;
    }

    MessageParcel data;
    if (!data.WriteCString(pkgName)) {
        LOG_ERR("RemoveSessionServer write pkg name failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteCString(sessionName)) {
        LOG_ERR("RemoveSessionServer session name failed!");
        return SOFTBUS_ERR;
    }
    MessageParcel reply;
    MessageOption option;
    if (remote->SendRequest(SERVER_REMOVE_SESSION_SERVER, data, reply, option) != 0) {
        LOG_ERR("RemoveSessionServer send request failed!");
        return SOFTBUS_ERR;
    }
    int32_t serverRet = 0;
    if (!reply.ReadInt32(serverRet)) {
        LOG_ERR("RemoveSessionServer read serverRet failed!");
        return SOFTBUS_ERR;
    }
    return serverRet;
}

int32_t TransServerProxy::OpenSession(const char *mySessionName, const char *peerSessionName,
    const char *peerDeviceId, const char *groupId, int32_t flags)
{
    if (mySessionName == nullptr || peerSessionName == nullptr || peerDeviceId == nullptr || groupId == nullptr) {
        return SOFTBUS_ERR;
    }
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        LOG_ERR("remote is nullptr!");
        return SOFTBUS_ERR;
    }

    MessageParcel data;
    if (!data.WriteCString(mySessionName)) {
        LOG_ERR("OpenSession write my session name failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteCString(peerSessionName)) {
        LOG_ERR("OpenSession write peer session name failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteCString(peerDeviceId)) {
        LOG_ERR("OpenSession write addr type length failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteCString(groupId)) {
        LOG_ERR("OpenSession write addr type length failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteInt32(flags)) {
        LOG_ERR("OpenSession write addr type length failed!");
        return SOFTBUS_ERR;
    }
    MessageParcel reply;
    MessageOption option;
    if (remote->SendRequest(SERVER_OPEN_SESSION, data, reply, option) != 0) {
        LOG_ERR("OpenSession send request failed!");
        return SOFTBUS_ERR;
    }
    int32_t channelId = 0;
    if (!reply.ReadInt32(channelId)) {
        LOG_ERR("OpenSession read channelId failed!");
        return SOFTBUS_ERR;
    }
    return channelId;
}

int32_t TransServerProxy::CloseChannel(int32_t channelId, int32_t channelType)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        LOG_ERR("remote is nullptr!");
        return SOFTBUS_ERR;
    }
    MessageParcel data;
    if (!data.WriteInt32(channelId)) {
        LOG_ERR("CloseChannel write channel id failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteInt32(channelType)) {
        LOG_ERR("CloseChannel write channel type failed!");
        return SOFTBUS_ERR;
    }

    MessageParcel reply;
    MessageOption option;
    if (remote->SendRequest(SERVER_CLOSE_CHANNEL, data, reply, option) != 0) {
        LOG_ERR("CloseChannel send request failed!");
        return SOFTBUS_ERR;
    }
    int32_t serverRet = 0;
    if (!reply.ReadInt32(serverRet)) {
        LOG_ERR("CloseChannel read serverRet failed!");
        return SOFTBUS_ERR;
    }
    return serverRet;
}

int32_t TransServerProxy::SendMessage(int32_t channelId, const void *dataInfo, uint32_t len, int32_t msgType)
{
    sptr<IRemoteObject> remote = Remote();
    if (remote == nullptr) {
        LOG_ERR("remote is nullptr!");
        return SOFTBUS_ERR;
    }
    MessageParcel data;
    if (!data.WriteInt32(channelId)) {
        LOG_ERR("SendMessage write channel id failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteUint32(len)) {
        LOG_ERR("SendMessage write dataInfo len failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteRawData(dataInfo, len)) {
        LOG_ERR("SendMessage write dataInfo failed!");
        return SOFTBUS_ERR;
    }
    if (!data.WriteInt32(msgType)) {
        LOG_ERR("SendMessage msgType failed!");
        return SOFTBUS_ERR;
    }

    MessageParcel reply;
    MessageOption option;
    if (remote->SendRequest(SERVER_SESSION_SENDMSG, data, reply, option) != 0) {
        LOG_ERR("SendMessage send request failed!");
        return SOFTBUS_ERR;
    }
    int32_t serverRet = 0;
    if (!reply.ReadInt32(serverRet)) {
        LOG_ERR("SendMessage read serverRet failed!");
        return SOFTBUS_ERR;
    }
    return serverRet;
}

int32_t TransServerProxy::JoinLNN(const char *pkgName, void *addr, uint32_t addrTypeLen)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::LeaveLNN(const char *pkgName, const char *networkId)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::GetAllOnlineNodeInfo(const char *pkgName, void **info, uint32_t infoTypeLen, int *infoNum)
{
    return SOFTBUS_OK;
}

int32_t TransServerProxy::GetLocalDeviceInfo(const char *pkgName, void *info, uint32_t infoTypeLen)
{
    return SOFTBUS_OK;
}
int32_t TransServerProxy::GetNodeKeyInfo(const char *pkgName, const char *networkId, int key, unsigned char *buf,
    uint32_t len)
{
    return SOFTBUS_OK;
}
}