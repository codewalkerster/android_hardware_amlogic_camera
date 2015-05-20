/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Contains implementation of a class EmulatedCameraFactory that manages cameras
 * available for emulation.
 */

//#define LOG_NDEBUG 0
//#define LOG_NDDEBUG 0
//#define LOG_NIDEBUG 0
#define LOG_TAG "EmulatedCamera_Factory"
#include <cutils/log.h>
#include <cutils/properties.h>
#include "EmulatedQemuCamera.h"
#include "EmulatedFakeCamera.h"
#include "EmulatedFakeCamera2.h"
#include "EmulatedFakeCamera3.h"
#include "EmulatedCameraHotplugThread.h"
#include "EmulatedCameraFactory.h"

extern camera_module_t HAL_MODULE_INFO_SYM;
volatile int32_t gCamHal_LogLevel = 4;

/* A global instance of EmulatedCameraFactory is statically instantiated and
 * initialized when camera emulation HAL is loaded.
 */
android::EmulatedCameraFactory  gEmulatedCameraFactory;
default_camera_hal::VendorTags gVendorTags;

static const char *SENSOR_PATH[]={
    "/dev/video0",
    "/dev/video1",
    "/dev/video2",
    "/dev/video3",
    "/dev/video4",
    "/dev/video5",
};

static  int getCameraNum() {
    int iCamerasNum = 0;
    for (int i = 0; i < (int)ARRAY_SIZE(SENSOR_PATH); i++ ) {
        int camera_fd;
        CAMHAL_LOGDB("try access %s\n", SENSOR_PATH[i]);
        if (0 == access(SENSOR_PATH[i], F_OK | R_OK | W_OK)) {
            CAMHAL_LOGDB("access %s success\n", SENSOR_PATH[i]);
            iCamerasNum++;
        }
    }

    return iCamerasNum;
}
namespace android {

EmulatedCameraFactory::EmulatedCameraFactory()
        : mQemuClient(),
          mEmulatedCameraNum(0),
          mFakeCameraNum(0),
          mConstructedOK(false),
          mCallbacks(NULL)
{
    status_t res;
    /* Connect to the factory service in the emulator, and create Qemu cameras. */
    int cameraId = 0;

    memset(mEmulatedCameras, 0,(MAX_CAMERA_NUM) * sizeof(EmulatedBaseCamera*));
    mEmulatedCameraNum = getCameraNum();
    CAMHAL_LOGDB("Camera num = %d", mEmulatedCameraNum);

    for( int i = 0; i < mEmulatedCameraNum; i++ ) {
        cameraId = i;
        mEmulatedCameras[i] = new EmulatedFakeCamera3(cameraId, &HAL_MODULE_INFO_SYM.common);
        if (mEmulatedCameras[i] != NULL) {
            ALOGV("%s: camera device version is %d", __FUNCTION__,
                    getFakeCameraHalVersion(cameraId));
            res = mEmulatedCameras[i]->Initialize();
            if (res != NO_ERROR) {
                ALOGE("%s: Unable to intialize camera %d: %s (%d)",
                    __FUNCTION__, i, strerror(-res), res);
                delete mEmulatedCameras[i];
            }
        }
    }

    CAMHAL_LOGDB("%d cameras are being created",
          mEmulatedCameraNum);

    /* Create hotplug thread */
    {
        Vector<int> cameraIdVector;
        for (int i = 0; i < mEmulatedCameraNum; ++i) {
            cameraIdVector.push_back(i);
        }
        mHotplugThread = new EmulatedCameraHotplugThread(&cameraIdVector[0],
                                                         mEmulatedCameraNum);
        mHotplugThread->run();
    }

    mConstructedOK = true;
}

EmulatedCameraFactory::~EmulatedCameraFactory()
{
    CAMHAL_LOGDA("Camera Factory deconstruct the BaseCamera\n");
    for (int n = 0; n < mEmulatedCameraNum; n++) {
        if (mEmulatedCameras[n] != NULL) {
            delete mEmulatedCameras[n];
        }
    }

    if (mHotplugThread != NULL) {
        mHotplugThread->requestExit();
        mHotplugThread->join();
    }
}

/****************************************************************************
 * Camera HAL API handlers.
 *
 * Each handler simply verifies existence of an appropriate EmulatedBaseCamera
 * instance, and dispatches the call to that instance.
 *
 ***************************************************************************/

int EmulatedCameraFactory::cameraDeviceOpen(int camera_id, hw_device_t** device)
{
    ALOGV("%s: id = %d", __FUNCTION__, camera_id);

    *device = NULL;

    if (!isConstructedOK()) {
        ALOGE("%s: EmulatedCameraFactory has failed to initialize", __FUNCTION__);
        return -EINVAL;
    }

    if (camera_id < 0 || camera_id >= getEmulatedCameraNum()) {
        ALOGE("%s: Camera id %d is out of bounds (%d)",
             __FUNCTION__, camera_id, getEmulatedCameraNum());
        return -ENODEV;
    }

    return mEmulatedCameras[camera_id]->connectCamera(device);
}

int EmulatedCameraFactory::getCameraInfo(int camera_id, struct camera_info* info)
{
    ALOGV("%s: id = %d", __FUNCTION__, camera_id);

    if (!isConstructedOK()) {
        ALOGE("%s: EmulatedCameraFactory has failed to initialize", __FUNCTION__);
        return -EINVAL;
    }

    if (camera_id < 0 || camera_id >= getEmulatedCameraNum()) {
        ALOGE("%s: Camera id %d is out of bounds (%d)",
             __FUNCTION__, camera_id, getEmulatedCameraNum());
        return -ENODEV;
    }

    return mEmulatedCameras[camera_id]->getCameraInfo(info);
}

int EmulatedCameraFactory::setCallbacks(
        const camera_module_callbacks_t *callbacks)
{
    ALOGV("%s: callbacks = %p", __FUNCTION__, callbacks);

    mCallbacks = callbacks;

    return OK;
}

static int get_tag_count(const vendor_tag_ops_t* ops)
{
    return gVendorTags.getTagCount(ops);
}
static void get_all_tags(const vendor_tag_ops_t* ops, uint32_t* tag_array)
{
    gVendorTags.getAllTags(ops, tag_array);
}
static const char* get_section_name(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getSectionName(ops, tag);
}
static const char* get_tag_name(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getTagName(ops, tag);
}
static int get_tag_type(const vendor_tag_ops_t* ops, uint32_t tag)
{
    return gVendorTags.getTagType(ops, tag);
}
void EmulatedCameraFactory::getvendortagops(vendor_tag_ops_t* ops)
{
    ALOGV("%s : ops=%p", __func__, ops);
    ops->get_tag_count      = get_tag_count;
    ops->get_all_tags       = get_all_tags;
    ops->get_section_name   = get_section_name;
    ops->get_tag_name       = get_tag_name;
    ops->get_tag_type       = get_tag_type;
}
/****************************************************************************
 * Camera HAL API callbacks.
 ***************************************************************************/

int EmulatedCameraFactory::device_open(const hw_module_t* module,
                                       const char* name,
                                       hw_device_t** device)
{
    /*
     * Simply verify the parameters, and dispatch the call inside the
     * EmulatedCameraFactory instance.
     */

    if (module != &HAL_MODULE_INFO_SYM.common) {
        ALOGE("%s: Invalid module %p expected %p",
             __FUNCTION__, module, &HAL_MODULE_INFO_SYM.common);
        return -EINVAL;
    }
    if (name == NULL) {
        ALOGE("%s: NULL name is not expected here", __FUNCTION__);
        return -EINVAL;
    }

    return gEmulatedCameraFactory.cameraDeviceOpen(atoi(name), device);
}

int EmulatedCameraFactory::get_number_of_cameras(void)
{
    return gEmulatedCameraFactory.getEmulatedCameraNum();
}

int EmulatedCameraFactory::get_camera_info(int camera_id,
                                           struct camera_info* info)
{
    return gEmulatedCameraFactory.getCameraInfo(camera_id, info);
}

int EmulatedCameraFactory::set_callbacks(
        const camera_module_callbacks_t *callbacks)
{
    return gEmulatedCameraFactory.setCallbacks(callbacks);
}

void EmulatedCameraFactory::get_vendor_tag_ops(vendor_tag_ops_t* ops)
{
	 gEmulatedCameraFactory.getvendortagops(ops);
}
/********************************************************************************
 * Internal API
 *******************************************************************************/

/*
 * Camera information tokens passed in response to the "list" factory query.
 */

/* Device name token. */
static const char lListNameToken[]    = "name=";
/* Frame dimensions token. */
static const char lListDimsToken[]    = "framedims=";
/* Facing direction token. */
static const char lListDirToken[]     = "dir=";

void EmulatedCameraFactory::createQemuCameras()
{
#if 0
    /* Obtain camera list. */
    char* camera_list = NULL;
    status_t res = mQemuClient.listCameras(&camera_list);
    /* Empty list, or list containing just an EOL means that there were no
     * connected cameras found. */
    if (res != NO_ERROR || camera_list == NULL || *camera_list == '\0' ||
        *camera_list == '\n') {
        if (camera_list != NULL) {
            free(camera_list);
        }
        return;
    }

    /*
     * Calculate number of connected cameras. Number of EOLs in the camera list
     * is the number of the connected cameras.
     */

    int num = 0;
    const char* eol = strchr(camera_list, '\n');
    while (eol != NULL) {
        num++;
        eol = strchr(eol + 1, '\n');
    }

    /* Allocate the array for emulated camera instances. Note that we allocate
     * two more entries for back and front fake camera emulation. */
    mEmulatedCameras = new EmulatedBaseCamera*[num + 2];
    if (mEmulatedCameras == NULL) {
        ALOGE("%s: Unable to allocate emulated camera array for %d entries",
             __FUNCTION__, num + 1);
        free(camera_list);
        return;
    }
    memset(mEmulatedCameras, 0, sizeof(EmulatedBaseCamera*) * (num + 1));

    /*
     * Iterate the list, creating, and initializin emulated qemu cameras for each
     * entry (line) in the list.
     */

    int index = 0;
    char* cur_entry = camera_list;
    while (cur_entry != NULL && *cur_entry != '\0' && index < num) {
        /* Find the end of the current camera entry, and terminate it with zero
         * for simpler string manipulation. */
        char* next_entry = strchr(cur_entry, '\n');
        if (next_entry != NULL) {
            *next_entry = '\0';
            next_entry++;   // Start of the next entry.
        }

        /* Find 'name', 'framedims', and 'dir' tokens that are required here. */
        char* name_start = strstr(cur_entry, lListNameToken);
        char* dim_start = strstr(cur_entry, lListDimsToken);
        char* dir_start = strstr(cur_entry, lListDirToken);
        if (name_start != NULL && dim_start != NULL && dir_start != NULL) {
            /* Advance to the token values. */
            name_start += strlen(lListNameToken);
            dim_start += strlen(lListDimsToken);
            dir_start += strlen(lListDirToken);

            /* Terminate token values with zero. */
            char* s = strchr(name_start, ' ');
            if (s != NULL) {
                *s = '\0';
            }
            s = strchr(dim_start, ' ');
            if (s != NULL) {
                *s = '\0';
            }
            s = strchr(dir_start, ' ');
            if (s != NULL) {
                *s = '\0';
            }

            /* Create and initialize qemu camera. */
            EmulatedQemuCamera* qemu_cam =
                new EmulatedQemuCamera(index, &HAL_MODULE_INFO_SYM.common);
            if (NULL != qemu_cam) {
                res = qemu_cam->Initialize(name_start, dim_start, dir_start);
                if (res == NO_ERROR) {
                    mEmulatedCameras[index] = qemu_cam;
                    index++;
                } else {
                    delete qemu_cam;
                }
            } else {
                ALOGE("%s: Unable to instantiate EmulatedQemuCamera",
                     __FUNCTION__);
            }
        } else {
            ALOGW("%s: Bad camera information: %s", __FUNCTION__, cur_entry);
        }

        cur_entry = next_entry;
    }

    mEmulatedCameraNum = index;
#else
    CAMHAL_LOGDA("delete this function");
#endif
}

bool EmulatedCameraFactory::isFakeCameraFacingBack(int cameraId)
{
    if (cameraId%mEmulatedCameraNum == 1)
        return false;

    return true;
}

int EmulatedCameraFactory::getFakeCameraHalVersion(int cameraId)
{
    /* Defined by 'qemu.sf.back_camera_hal_version' boot property: if the
     * property doesn't exist, it is assumed to be 1. */
#if 0
    char prop[PROPERTY_VALUE_MAX];
    if (property_get("qemu.sf.back_camera_hal", prop, NULL) > 0) {
        char *prop_end = prop;
        int val = strtol(prop, &prop_end, 10);
        if (*prop_end == '\0') {
            return val;
        }
        // Badly formatted property, should just be a number
        ALOGE("qemu.sf.back_camera_hal is not a number: %s", prop);
    }
    return 1;
#else
    cameraId = cameraId;
    return 3;
#endif
}

void EmulatedCameraFactory::onStatusChanged(int cameraId, int newStatus)
{
    status_t res;
    char dev_name[128];
    int i = 0;
    //EmulatedBaseCamera *cam = mEmulatedCameras[cameraId];
    const camera_module_callbacks_t* cb = mCallbacks;
    sprintf(dev_name, "%s%d", "/dev/video", cameraId);

    CAMHAL_LOGDB("mEmulatedCameraNum =%d\n", mEmulatedCameraNum);

    /*we release mEmulatedCameras[i] object for the last time construct*/
    for (int i = 0; i < MAX_CAMERA_NUM; i++) {
        if ((mEmulatedCameras[i] != NULL) && (mEmulatedCameras[i]->getCameraStatus() == CAMERA_READY_REMOVE)) {
            mEmulatedCameras[i]->setCameraStatus(CAMERA_INIT);
            delete mEmulatedCameras[i];
            mEmulatedCameras[i] = NULL;
        }
    }

    EmulatedBaseCamera *cam = mEmulatedCameras[cameraId];

    if (!cam) {
        /*suppose only usb camera produce uevent, and it is facing back*/
        cam = new EmulatedFakeCamera3(cameraId, &HAL_MODULE_INFO_SYM.common);
        cam->setCameraStatus(CAMERA_INIT);
        if (cam != NULL) {
            CAMHAL_LOGDB("%s: new camera device version is %d", __FUNCTION__,
                    getFakeCameraHalVersion(cameraId));
            //sleep 10ms for /dev/video* create
            usleep(200000);
            while (i < 4) {
                if (0 == access(dev_name, F_OK | R_OK | W_OK)) {
                    DBG_LOGB("access %s success\n", dev_name);
                    break;
                } else {
                    DBG_LOGB("access %s fail , i = %d .\n", dev_name,i);
                    usleep(200000);
                    i++;
                }
            }
            res = cam->Initialize();
            if (res != NO_ERROR) {
                ALOGE("%s: Unable to intialize camera %d: %s (%d)",
                    __FUNCTION__, cameraId, strerror(-res), res);
                delete cam;
                return ;
            }
        }

        /* Open the camera. then send the callback to framework*/
        mEmulatedCameras[cameraId] = cam;
        mEmulatedCameraNum ++;
        cam->plugCamera();
        if (cb != NULL && cb->camera_device_status_change != NULL) {
            cb->camera_device_status_change(cb, cameraId, newStatus);
        }

        return ;
    }

    CAMHAL_LOGDB("mEmulatedCameraNum =%d\n", mEmulatedCameraNum);

    /**
     * (Order is important)
     * Send the callback first to framework, THEN close the camera.
     */

    if (newStatus == cam->getHotplugStatus()) {
        CAMHAL_LOGDB("%s: Ignoring transition to the same status", __FUNCTION__);
        return;
    }

/*here we don't notify cameraservice close camera, let app to close camera, or will generate crash*/
#if 0
    CAMHAL_LOGDB("mEmulatedCameraNum =%d\n", mEmulatedCameraNum);
    if (cb != NULL && cb->camera_device_status_change != NULL) {
        cb->camera_device_status_change(cb, cameraId, newStatus);
    }
#endif

    CAMHAL_LOGDB("mEmulatedCameraNum =%d\n", mEmulatedCameraNum);

/*don't delete mEmulatedCameras[i], or will generate crash*/
    if (newStatus == CAMERA_DEVICE_STATUS_NOT_PRESENT) {
        //cam->unplugCamera();
////
        //delete mEmulatedCameras[cameraId];
        //mEmulatedCameras[cameraId] = NULL;
        mEmulatedCameras[cameraId]->setCameraStatus(CAMERA_READY_REMOVE);
        mEmulatedCameraNum --;
////
    } else if (newStatus == CAMERA_DEVICE_STATUS_PRESENT) {
        CAMHAL_LOGDA("camera plugged again?\n");
        cam->plugCamera();
    }
    CAMHAL_LOGDB("mEmulatedCameraNum =%d\n", mEmulatedCameraNum);

}

/********************************************************************************
 * Initializer for the static member structure.
 *******************************************************************************/

/* Entry point for camera HAL API. */
struct hw_module_methods_t EmulatedCameraFactory::mCameraModuleMethods = {
    open: EmulatedCameraFactory::device_open
};

}; /* namespace android */
