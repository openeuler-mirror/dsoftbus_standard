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

#include "client_trans_file_listener.h"

#include <securec.h>

#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_mem_interface.h"
#include "softbus_utils.h"

static SoftBusList *g_fileListener = NULL;

int TransFileInit()
{
    if (g_fileListener != NULL) {
        LOG_INFO("file listener has initialized.");
        return SOFTBUS_OK;
    }
    g_fileListener = CreateSoftBusList();
    if (g_fileListener == NULL) {
        LOG_ERR("create file listener list failed.");
        return SOFTBUS_MALLOC_ERR;
    }
    return SOFTBUS_OK;
}

void TransFileDeinit(void)
{
    if (g_fileListener == NULL) {
        return;
    }
    if (pthread_mutex_lock(&(g_fileListener->lock)) != 0) {
        LOG_ERR("file listener deinit lock failed");
        return;
    }
    FileListener *fileNode = NULL;
    LIST_FOR_EACH_ENTRY(fileNode, &(g_fileListener->list), FileListener, node) {
        ListDelete(&(fileNode->node));
        SoftBusFree(fileNode);
    }
    (void)pthread_mutex_unlock(&(g_fileListener->lock));
    DestroySoftBusList(g_fileListener);
    g_fileListener = NULL;
}

int32_t TransSetFileReceiveListener(const char *sessionName,
    const IFileReceiveListener *recvListener, const char *rootDir)
{
    if (g_fileListener == NULL) {
        LOG_ERR("file listener hasn't initialized.");
        return SOFTBUS_ERR;
    }
    if (pthread_mutex_lock(&(g_fileListener->lock)) != 0) {
        LOG_ERR("file receive listener lock failed");
        return SOFTBUS_LOCK_ERR;
    }
    FileListener *fileNode = NULL;
    bool exist = false;
    LIST_FOR_EACH_ENTRY(fileNode, &(g_fileListener->list), FileListener, node) {
        if (strcmp(fileNode->mySessionName, sessionName) == 0) {
            exist = true;
            break;
        }
    }
    if (exist) {
        if (strcpy_s(fileNode->rootDir, FILE_RECV_ROOT_DIR_SIZE_MAX, rootDir) != EOK ||
            memcpy_s(&(fileNode->recvListener), sizeof(IFileReceiveListener),
                recvListener, sizeof(IFileReceiveListener)) != EOK) {
            (void)pthread_mutex_unlock(&(g_fileListener->lock));
            LOG_ERR("update file receive listener failed");
            return SOFTBUS_ERR;
        }
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        LOG_INFO("update file receive listener success");
        return SOFTBUS_OK;
    }
    fileNode = (FileListener *)SoftBusCalloc(sizeof(FileListener));
    if (fileNode == NULL) {
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        LOG_ERR("file receive listener calloc failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (strcpy_s(fileNode->mySessionName, SESSION_NAME_SIZE_MAX, sessionName) != EOK ||
        strcpy_s(fileNode->rootDir, FILE_RECV_ROOT_DIR_SIZE_MAX, rootDir) != EOK ||
        memcpy_s(&(fileNode->recvListener), sizeof(IFileReceiveListener),
            recvListener, sizeof(IFileReceiveListener)) != EOK) {
        LOG_ERR("file node copy failed.");
        SoftBusFree(fileNode);
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        return SOFTBUS_ERR;
    }
    ListAdd(&(g_fileListener->list), &(fileNode->node));
    (void)pthread_mutex_unlock(&(g_fileListener->lock));
    return SOFTBUS_OK;
}

int32_t TransSetFileSendListener(const char *sessionName, const IFileSendListener *sendListener)
{
    if (g_fileListener == NULL) {
        LOG_ERR("file listener hasn't initialized.");
        return SOFTBUS_ERR;
    }
    if (pthread_mutex_lock(&(g_fileListener->lock)) != 0) {
        LOG_ERR("file send listener lock failed");
        return SOFTBUS_LOCK_ERR;
    }
    FileListener *fileNode = NULL;
    bool exist = false;
    LIST_FOR_EACH_ENTRY(fileNode, &(g_fileListener->list), FileListener, node) {
        if (strcmp(fileNode->mySessionName, sessionName) == 0) {
            exist = true;
            break;
        }
    }
    if (exist) {
        if (memcpy_s(&(fileNode->sendListener), sizeof(IFileSendListener),
                sendListener, sizeof(IFileSendListener)) != EOK) {
            (void)pthread_mutex_unlock(&(g_fileListener->lock));
            LOG_ERR("update file send listener failed");
            return SOFTBUS_ERR;
        }
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        LOG_INFO("update file send listener success");
        return SOFTBUS_OK;
    }
    fileNode = (FileListener *)SoftBusCalloc(sizeof(FileListener));
    if (fileNode == NULL) {
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        LOG_ERR("file send listener calloc failed");
        return SOFTBUS_MALLOC_ERR;
    }
    if (strcpy_s(fileNode->mySessionName, SESSION_NAME_SIZE_MAX, sessionName) != EOK ||
        memcpy_s(&(fileNode->sendListener), sizeof(IFileSendListener),
            sendListener, sizeof(IFileSendListener)) != EOK) {
        LOG_ERR("file node copy failed.");
        SoftBusFree(fileNode);
        (void)pthread_mutex_unlock(&(g_fileListener->lock));
        return SOFTBUS_ERR;
    }
    ListAdd(&(g_fileListener->list), &(fileNode->node));
    (void)pthread_mutex_unlock(&(g_fileListener->lock));
    return SOFTBUS_OK;
}

int32_t TransGetFileListener(const char *sessionName, FileListener *fileListener)
{
    if (g_fileListener == NULL) {
        LOG_ERR("file listener hasn't initialized.");
        return SOFTBUS_ERR;
    }
    if (pthread_mutex_lock(&(g_fileListener->lock)) != 0) {
        LOG_ERR("file get listener lock failed");
        return SOFTBUS_LOCK_ERR;
    }

    FileListener *fileNode = NULL;
    LIST_FOR_EACH_ENTRY(fileNode, &(g_fileListener->list), FileListener, node) {
        if (strcmp(fileNode->mySessionName, sessionName) == 0) {
            if (memcpy_s(fileListener, sizeof(FileListener), fileNode, sizeof(FileListener)) != EOK) {
                LOG_ERR("memcpy_s failed.");
                (void)pthread_mutex_unlock(&(g_fileListener->lock));
                return SOFTBUS_ERR;
            }
            (void)pthread_mutex_unlock(&(g_fileListener->lock));
            return SOFTBUS_OK;
        }
    }
    (void)pthread_mutex_unlock(&(g_fileListener->lock));
    return SOFTBUS_ERR;
}

void TransDeleteFileListener(const char *sessionName)
{
    if (g_fileListener == NULL) {
        LOG_ERR("file listener hasn't initialized.");
        return;
    }
    if (pthread_mutex_lock(&(g_fileListener->lock)) != 0) {
        LOG_ERR("file delete lock failed");
        return;
    }

    FileListener *fileNode = NULL;
    LIST_FOR_EACH_ENTRY(fileNode, &(g_fileListener->list), FileListener, node) {
        if (strcmp(fileNode->mySessionName, sessionName) == 0) {
            SoftBusFree(fileNode);
            break;
        }
    }
    (void)pthread_mutex_unlock(&(g_fileListener->lock));
}