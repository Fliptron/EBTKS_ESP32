// Copyright 2015-2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

static constexpr const char REMOTEFS_NAME[] = "spiffs";

#include "vfs_api.h"

extern "C" {
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_remotefs.h"
}

#include "REMOTEFS.h"

using namespace fs;

REMOTEFSFS::REMOTEFSFS() : FS(FSImplPtr(new VFSImpl()))
{

}

bool REMOTEFSFS::begin(bool formatOnFail, const char * basePath, uint8_t maxOpenFiles)
{
    if(esp_remotefs_mounted(REMOTEFS_NAME)){
        log_w("REMOTEFS Already Mounted!");
        return true;
    }

    esp_vfs_remotefs_conf_t conf = {
      .base_path = basePath,
      .partition_label = REMOTEFS_NAME,
      .format_if_mount_failed = false
    };

    esp_err_t err = esp_vfs_remotefs_register(&conf);
    if(err == ESP_FAIL && formatOnFail){
        if(format()){
            err = esp_vfs_remotefs_register(&conf);
        }
    }
    if(err != ESP_OK){
        log_e("Mounting REMOTEFS failed! Error: %d", err);
        return false;
    }
    _impl->mountpoint(basePath);
    return true;
}

void REMOTEFSFS::end()
{
    if(esp_remotefs_mounted(REMOTEFS_NAME)){
        esp_err_t err = esp_vfs_remotefs_unregister(REMOTEFS_NAME);
        if(err){
            log_e("Unmounting REMOTEFS failed! Error: %d", err);
            return;
        }
        _impl->mountpoint(NULL);
    }
}

bool REMOTEFSFS::format()
{
    disableCore0WDT();
    esp_err_t err = esp_remotefs_format(REMOTEFS_NAME);
    enableCore0WDT();
    if(err){
        log_e("Formatting REMOTEFS failed! Error: %d", err);
        return false;
    }
    return true;
}

size_t REMOTEFSFS::totalBytes()
{
    size_t total,used;
    if(esp_remotefs_info(REMOTEFS_NAME, &total, &used)){
        return 0;
    }
    return total;
}

size_t REMOTEFSFS::usedBytes()
{
    size_t total,used;
    if(esp_remotefs_info(REMOTEFS_NAME, &total, &used)){
        return 0;
    }
    return used;
}

REMOTEFSFS REMOTEFS;

