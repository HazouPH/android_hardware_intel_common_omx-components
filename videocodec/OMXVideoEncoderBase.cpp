/*
* Copyright (c) 2009-2011 Intel Corporation.  All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


// #define LOG_NDEBUG 0
#define LOG_TAG "OMXVideoEncoderBase"
#include <utils/Log.h>
#include "OMXVideoEncoderBase.h"

static const char *RAW_MIME_TYPE = "video/raw";

OMXVideoEncoderBase::OMXVideoEncoderBase()
    :mVideoEncoder(NULL)
    ,mEncoderParams(NULL)
    ,mFrameInputCount(0)
    ,mFrameOutputCount(0)
    ,mPFrames(0)
    ,mFrameRetrieved(OMX_TRUE)
    ,mFirstFrame(OMX_TRUE)
    ,mForceBufferSharing(OMX_FALSE) {

    mEncoderParams = new VideoParamsCommon();
    if (!mEncoderParams) LOGE("OMX_ErrorInsufficientResources");

    mBsState = BS_STATE_INVALID;
    OMX_ERRORTYPE ret = InitBSMode();
    if(ret != OMX_ErrorNone) {
        LOGE("InitBSMode failed in Constructor ");
        DeinitBSMode();
    }
}

OMXVideoEncoderBase::~OMXVideoEncoderBase() {

    // destroy ports
    if (this->ports) {
        if (this->ports[INPORT_INDEX]) {
            delete this->ports[INPORT_INDEX];
            this->ports[INPORT_INDEX] = NULL;
        }

        if (this->ports[OUTPORT_INDEX]) {
            delete this->ports[OUTPORT_INDEX];
            this->ports[OUTPORT_INDEX] = NULL;
        }
    }

    // Release video encoder object
    if(mVideoEncoder) {
        releaseVideoEncoder(mVideoEncoder);
        mVideoEncoder = NULL;
    }

    if(mEncoderParams) {
        delete mEncoderParams;
        mEncoderParams = NULL;
    }

    OMX_ERRORTYPE oret = DeinitBSMode();
    if (oret != OMX_ErrorNone) {
        LOGE("DeinitBSMode in destructor");
    }
}

OMX_ERRORTYPE OMXVideoEncoderBase::InitInputPort(void) {
    this->ports[INPORT_INDEX] = new PortVideo;
    if (this->ports[INPORT_INDEX] == NULL) {
        return OMX_ErrorInsufficientResources;
    }

    PortVideo *port = static_cast<PortVideo *>(this->ports[INPORT_INDEX]);

    // OMX_PARAM_PORTDEFINITIONTYPE
    OMX_PARAM_PORTDEFINITIONTYPE paramPortDefinitionInput;
    memset(&paramPortDefinitionInput, 0, sizeof(paramPortDefinitionInput));
    SetTypeHeader(&paramPortDefinitionInput, sizeof(paramPortDefinitionInput));
    paramPortDefinitionInput.nPortIndex = INPORT_INDEX;
    paramPortDefinitionInput.eDir = OMX_DirInput;
    paramPortDefinitionInput.nBufferCountActual = INPORT_ACTUAL_BUFFER_COUNT;
    paramPortDefinitionInput.nBufferCountMin = INPORT_MIN_BUFFER_COUNT;
    paramPortDefinitionInput.nBufferSize = INPORT_BUFFER_SIZE;
    paramPortDefinitionInput.bEnabled = OMX_TRUE;
    paramPortDefinitionInput.bPopulated = OMX_FALSE;
    paramPortDefinitionInput.eDomain = OMX_PortDomainVideo;
    paramPortDefinitionInput.format.video.cMIMEType = (OMX_STRING)RAW_MIME_TYPE;
    paramPortDefinitionInput.format.video.pNativeRender = NULL;
    paramPortDefinitionInput.format.video.nFrameWidth = 176;
    paramPortDefinitionInput.format.video.nFrameHeight = 144;
    paramPortDefinitionInput.format.video.nStride = 0;
    paramPortDefinitionInput.format.video.nSliceHeight = 0;
    paramPortDefinitionInput.format.video.nBitrate = 64000;
    paramPortDefinitionInput.format.video.xFramerate = 15 << 16;
    paramPortDefinitionInput.format.video.bFlagErrorConcealment = OMX_FALSE;
    paramPortDefinitionInput.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    paramPortDefinitionInput.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    paramPortDefinitionInput.format.video.pNativeWindow = NULL;
    paramPortDefinitionInput.bBuffersContiguous = OMX_FALSE;
    paramPortDefinitionInput.nBufferAlignment = 0;

    // Nothing specific to initialize input port.
    InitInputPortFormatSpecific(&paramPortDefinitionInput);

    port->SetPortDefinition(&paramPortDefinitionInput, true);

    // OMX_VIDEO_PARAM_PORTFORMATTYPE
    OMX_VIDEO_PARAM_PORTFORMATTYPE paramPortFormat;
    memset(&paramPortFormat, 0, sizeof(paramPortFormat));
    SetTypeHeader(&paramPortFormat, sizeof(paramPortFormat));
    paramPortFormat.nPortIndex = INPORT_INDEX;
    paramPortFormat.nIndex = 0;
    paramPortFormat.eCompressionFormat = paramPortDefinitionInput.format.video.eCompressionFormat;
    paramPortFormat.eColorFormat = paramPortDefinitionInput.format.video.eColorFormat;
    paramPortFormat.xFramerate = paramPortDefinitionInput.format.video.xFramerate;

    port->SetPortVideoParam(&paramPortFormat, true);

    return OMX_ErrorNone;
}


OMX_ERRORTYPE OMXVideoEncoderBase::InitOutputPort(void) {
    this->ports[OUTPORT_INDEX] = new PortVideo;
    if (this->ports[OUTPORT_INDEX] == NULL) {
        return OMX_ErrorInsufficientResources;
    }

    PortVideo *port = static_cast<PortVideo *>(this->ports[OUTPORT_INDEX]);

    // OMX_VIDEO_PARAM_BITRATETYPE
    memset(&mParamBitrate, 0, sizeof(mParamBitrate));
    SetTypeHeader(&mParamBitrate, sizeof(mParamBitrate));
    mParamBitrate.nPortIndex = OUTPORT_INDEX;
    mParamBitrate.eControlRate = OMX_Video_ControlRateConstant;
    mParamBitrate.nTargetBitrate = 192000; // to be overridden

    // OMX_VIDEO_CONFIG_PRI_INFOTYPE
    memset(&mConfigPriInfo, 0, sizeof(mConfigPriInfo));
    SetTypeHeader(&mConfigPriInfo, sizeof(mConfigPriInfo));
    mConfigPriInfo.nPortIndex = OUTPORT_INDEX;
    mConfigPriInfo.nCapacity = 0;
    mConfigPriInfo.nHolder = NULL;

    // OMX_VIDEO_PARAM_INTEL_BITRATETYPE
    memset(&mParamIntelBitrate, 0, sizeof(mParamIntelBitrate));
    SetTypeHeader(&mParamIntelBitrate, sizeof(mParamIntelBitrate));
    mParamIntelBitrate.nPortIndex = OUTPORT_INDEX;
    mParamIntelBitrate.eControlRate = OMX_Video_Intel_ControlRateMax;
    mParamIntelBitrate.nTargetBitrate = 0; // to be overridden ?

    // OMX_VIDEO_CONFIG_INTEL_BITRATETYPE
    memset(&mConfigIntelBitrate, 0, sizeof(mConfigIntelBitrate));
    SetTypeHeader(&mConfigIntelBitrate, sizeof(mConfigIntelBitrate));
    mConfigIntelBitrate.nPortIndex = OUTPORT_INDEX;
    mConfigIntelBitrate.nMaxEncodeBitrate = 4000 * 1024; // Maximum bitrate
    mConfigIntelBitrate.nTargetPercentage = 95; // Target bitrate as percentage of maximum bitrate; e.g. 95 is 95%
    mConfigIntelBitrate.nWindowSize = 500; // Window size in milliseconds allowed for bitrate to reach target
    mConfigIntelBitrate.nInitialQP = 24;  // Initial QP for I frames
    mConfigIntelBitrate.nMinQP = 1;

    // OMX_VIDEO_CONFIG_INTEL_AIR
    memset(&mConfigIntelAir, 0, sizeof(mConfigIntelAir));
    SetTypeHeader(&mConfigIntelAir, sizeof(mConfigIntelAir));
    mConfigIntelAir.nPortIndex = OUTPORT_INDEX;
    mConfigIntelAir.bAirEnable = OMX_FALSE;
    mConfigIntelAir.bAirAuto = OMX_FALSE;
    mConfigIntelAir.nAirMBs = 0;
    mConfigIntelAir.nAirThreshold = 0;

    // OMX_CONFIG_FRAMERATETYPE
    memset(&mConfigFramerate, 0, sizeof(mConfigFramerate));
    SetTypeHeader(&mConfigFramerate, sizeof(mConfigFramerate));
    mConfigFramerate.nPortIndex = OUTPORT_INDEX;
    mConfigFramerate.xEncodeFramerate =  0; // Q16 format

    // OMX_VIDEO_PARAM_INTEL_ADAPTIVE_SLICE_CONTROL
    memset(&mParamIntelAdaptiveSliceControl, 0, sizeof(mParamIntelAdaptiveSliceControl));
    SetTypeHeader(&mParamIntelAdaptiveSliceControl, sizeof(mParamIntelAdaptiveSliceControl));
    mParamIntelAdaptiveSliceControl.nPortIndex = OUTPORT_INDEX;
    mParamIntelAdaptiveSliceControl.bEnable = OMX_FALSE;
    mParamIntelAdaptiveSliceControl.nMinPSliceNumber = 5;
    mParamIntelAdaptiveSliceControl.nNumPFramesToSkip = 8;
    mParamIntelAdaptiveSliceControl.nSliceSizeThreshold = 1200;

    // OMX_VIDEO_PARAM_PROFILELEVELTYPE
    memset(&mParamProfileLevel, 0, sizeof(mParamProfileLevel));
    SetTypeHeader(&mParamProfileLevel, sizeof(mParamProfileLevel));
    mParamProfileLevel.nPortIndex = OUTPORT_INDEX;
    mParamProfileLevel.eProfile = 0; // undefined profile, to be overridden
    mParamProfileLevel.eLevel = 0; // undefined level, to be overridden

    // OMX_PARAM_PORTDEFINITIONTYPE
    OMX_PARAM_PORTDEFINITIONTYPE paramPortDefinitionOutput;
    memset(&paramPortDefinitionOutput, 0, sizeof(paramPortDefinitionOutput));
    SetTypeHeader(&paramPortDefinitionOutput, sizeof(paramPortDefinitionOutput));
    paramPortDefinitionOutput.nPortIndex = OUTPORT_INDEX;
    paramPortDefinitionOutput.eDir = OMX_DirOutput;
    paramPortDefinitionOutput.nBufferCountActual = OUTPORT_ACTUAL_BUFFER_COUNT; // to be overridden
    paramPortDefinitionOutput.nBufferCountMin = OUTPORT_MIN_BUFFER_COUNT;
    paramPortDefinitionOutput.nBufferSize = OUTPORT_BUFFER_SIZE; // to be overridden
    paramPortDefinitionOutput.bEnabled = OMX_TRUE;
    paramPortDefinitionOutput.bPopulated = OMX_FALSE;
    paramPortDefinitionOutput.eDomain = OMX_PortDomainVideo;
    paramPortDefinitionOutput.format.video.cMIMEType = NULL; // to be overridden
    paramPortDefinitionOutput.format.video.pNativeRender = NULL;
    paramPortDefinitionOutput.format.video.nFrameWidth = 176;
    paramPortDefinitionOutput.format.video.nFrameHeight = 144;
    paramPortDefinitionOutput.format.video.nStride = 176;
    paramPortDefinitionOutput.format.video.nSliceHeight = 144;
    paramPortDefinitionOutput.format.video.nBitrate = 64000;
    paramPortDefinitionOutput.format.video.xFramerate = 15 << 16;
    paramPortDefinitionOutput.format.video.bFlagErrorConcealment = OMX_FALSE;
    paramPortDefinitionOutput.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused; // to be overridden
    paramPortDefinitionOutput.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    paramPortDefinitionOutput.format.video.pNativeWindow = NULL;
    paramPortDefinitionOutput.bBuffersContiguous = OMX_FALSE;
    paramPortDefinitionOutput.nBufferAlignment = 0;

    InitOutputPortFormatSpecific(&paramPortDefinitionOutput);

    port->SetPortDefinition(&paramPortDefinitionOutput, true);
    port->SetPortBitrateParam(&mParamBitrate, true);

    // OMX_VIDEO_PARAM_PORTFORMATTYPE
    OMX_VIDEO_PARAM_PORTFORMATTYPE paramPortFormat;
    memset(&paramPortFormat, 0, sizeof(paramPortFormat));
    SetTypeHeader(&paramPortFormat, sizeof(paramPortFormat));
    paramPortFormat.nPortIndex = OUTPORT_INDEX;
    paramPortFormat.nIndex = 0;
    paramPortFormat.eCompressionFormat = paramPortDefinitionOutput.format.video.eCompressionFormat;
    paramPortFormat.eColorFormat = paramPortDefinitionOutput.format.video.eColorFormat;
    paramPortFormat.xFramerate = paramPortDefinitionOutput.format.video.xFramerate;

    port->SetPortVideoParam(&paramPortFormat, true);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::InitInputPortFormatSpecific(OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput) {
    // no format specific to initialize input
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetVideoEncoderParam() {

    Encode_Status ret = ENCODE_SUCCESS;
    PortVideo *port_in = NULL;
    PortVideo *port_out = NULL;
    OMX_VIDEO_CONTROLRATETYPE controlrate;
    const OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput = NULL;
    LOGV("OMXVideoEncoderBase::SetVideoEncoderParam called\n");

    port_in = static_cast<PortVideo *>(ports[INPORT_INDEX]);
    port_out = static_cast<PortVideo *>(ports[OUTPORT_INDEX]);
    paramPortDefinitionInput = port_in->GetPortDefinition();
    mEncoderParams->resolution.height = paramPortDefinitionInput->format.video.nFrameHeight;
    mEncoderParams->resolution.width = paramPortDefinitionInput->format.video.nFrameWidth;
    const OMX_VIDEO_PARAM_BITRATETYPE *bitrate = port_out->GetPortBitrateParam();

    mEncoderParams->frameRate.frameRateDenom = 1;
    if(mConfigFramerate.xEncodeFramerate != 0) {
        mEncoderParams->frameRate.frameRateNum = mConfigFramerate.xEncodeFramerate;
    } else {
        mEncoderParams->frameRate.frameRateNum = paramPortDefinitionInput->format.video.xFramerate >> 16;
        mConfigFramerate.xEncodeFramerate = paramPortDefinitionInput->format.video.xFramerate >> 16;
    }

    if(mPFrames == 0) {
        mPFrames = mEncoderParams->frameRate.frameRateNum / 2;
    }
    mEncoderParams->intraPeriod = mPFrames;
    mEncoderParams->rawFormat = RAW_FORMAT_NV12;

    LOGV("frameRate.frameRateDenom = %d\n", mEncoderParams->frameRate.frameRateDenom);
    LOGV("frameRate.frameRateNum = %d\n", mEncoderParams->frameRate.frameRateNum);
    LOGV("intraPeriod = %d\n ", mEncoderParams->intraPeriod);
    mEncoderParams->rcParams.initQP = mConfigIntelBitrate.nInitialQP;
    mEncoderParams->rcParams.minQP = mConfigIntelBitrate.nMinQP;
    mEncoderParams->rcParams.windowSize = mConfigIntelBitrate.nWindowSize;
    mEncoderParams->rcParams.targetPercentage = mConfigIntelBitrate.nTargetPercentage;

    if(mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateMax) {

        mEncoderParams->rcParams.bitRate = mParamBitrate.nTargetBitrate;//bitrate->nTargetBitrate;
        LOGV("rcParams.bitRate = %d\n", mEncoderParams->rcParams.bitRate);

        if (mEncoderParams->rcMode == RATE_CONTROL_CBR) {
            controlrate = OMX_Video_ControlRateConstant;
        } else if (mEncoderParams->rcMode == RATE_CONTROL_VBR) {
            controlrate = OMX_Video_ControlRateVariable;
        } else {
            controlrate = OMX_Video_ControlRateDisable;
        }

        if (controlrate != bitrate->eControlRate) {

            if ((bitrate->eControlRate == OMX_Video_ControlRateVariable) ||
                (bitrate->eControlRate == OMX_Video_ControlRateVariableSkipFrames)) {
                mEncoderParams->rcMode = RATE_CONTROL_VBR;
            } else if ((bitrate->eControlRate == OMX_Video_ControlRateConstant) ||
                       (bitrate->eControlRate == OMX_Video_ControlRateConstantSkipFrames)) {
                mEncoderParams->rcMode = RATE_CONTROL_CBR;
            } else {
                mEncoderParams->rcMode = RATE_CONTROL_NONE;
            }
            LOGV("rcMode = %d\n", mEncoderParams->rcMode);
        }
    } else {

        if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateConstant ) {
            LOGV("%s(), eControlRate == OMX_Video_Intel_ControlRateConstant", __func__);
            mEncoderParams->rcParams.bitRate = mParamIntelBitrate.nTargetBitrate;
            mEncoderParams->rcMode = RATE_CONTROL_CBR;
        } else if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateVariable) {
            LOGV("%s(), eControlRate == OMX_Video_Intel_ControlRateVariable", __func__);
            mEncoderParams->rcParams.bitRate = mParamIntelBitrate.nTargetBitrate;
            mEncoderParams->rcMode = RATE_CONTROL_VBR;
        } else if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateVideoConferencingMode) {
            LOGV("%s(), eControlRate == OMX_Video_Intel_ControlRateVideoConferencingMode ", __func__);
            mEncoderParams->rcMode = RATE_CONTROL_VCM;
            mEncoderParams->rcParams.bitRate = mConfigIntelBitrate.nMaxEncodeBitrate;
            if(mConfigIntelAir.bAirEnable == OMX_TRUE) {
                mEncoderParams->airParams.airAuto = mConfigIntelAir.bAirAuto;
                mEncoderParams->airParams.airMBs = mConfigIntelAir.nAirMBs;
                mEncoderParams->airParams.airThreshold = mConfigIntelAir.nAirThreshold;
                mEncoderParams->refreshType = VIDEO_ENC_AIR;
            } else {
                mEncoderParams->refreshType = VIDEO_ENC_NONIR;
            }
            LOGV("refreshType = %d\n", mEncoderParams->refreshType);
        } else {
           mEncoderParams->rcMode = RATE_CONTROL_NONE;
        }
    }

    ret = mVideoEncoder->setParameters(mEncoderParams);
    CHECK_ENCODE_STATUS("setParameters");
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::ProcessorInit(void) {
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    ret = SetVideoEncoderParam();
    CHECK_STATUS("SetVideoEncoderParam");

    ret = StartBufferSharing();
    CHECK_STATUS("StartBufferSharing");

    if (mVideoEncoder->start() != ENCODE_SUCCESS) {
        LOGE("Start failed, ret = 0x%08x\n", ret);
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::ProcessorDeinit(void) {
    OMX_ERRORTYPE ret;
    if (mBsState == BS_STATE_EXECUTING) {
        ret = StopBufferSharing();
        if (ret != OMX_ErrorNone) {
            LOGE("StopBufferSharing failed, ret = 0x%08x\n", ret);
        }
    }

    if(mVideoEncoder) {
        mVideoEncoder->stop();
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::ProcessorStop(void) {

    this->ports[INPORT_INDEX]->ReturnAllRetainedBuffers();
    return OMX_ErrorNone;
}
OMX_ERRORTYPE OMXVideoEncoderBase:: ProcessorProcess(
    OMX_BUFFERHEADERTYPE **buffers,
    buffer_retain_t *retains,
    OMX_U32 numberBuffers) {

    LOGV("OMXVideoEncoderBase:: ProcessorProcess \n");
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::ProcessorFlush(OMX_U32 portIndex) {
    LOGV("OMXVideoEncoderBase::ProcessorFlush\n");
    if (portIndex == INPORT_INDEX || portIndex == OMX_ALL) {
        this->ports[INPORT_INDEX]->ReturnAllRetainedBuffers();
        mVideoEncoder->flush();
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::AllocateSharedBuffers(int width, int height) {

    int index = 0;
    int bufSize = 0;
    Encode_Status ret = ENCODE_SUCCESS;

    if (width <= 0 || height <= 0) {
        LOGE("width <= 0 || height <= 0");
        return OMX_ErrorBadParameter;
    }

    CHECK_BS_STATE();

    if (mBsState == BS_STATE_INVALID) {
        LOGW("buffer sharing not enabled, do nothing");
        return OMX_ErrorNone;
    }

    if (mSharedBufArray != NULL) {
        delete [] mSharedBufArray;
        mSharedBufArray = NULL;
    }

    mSharedBufArray = new SharedBufferType[mSharedBufCnt];
    VideoParamsUsrptrBuffer paramsUsrptrBuffer;

    bufSize = SHARE_PTR_ALIGN(width) * height * 3 / 2;

    for (index = 0; index < mSharedBufCnt; index++) {

        paramsUsrptrBuffer.type = VideoParamsTypeUsrptrBuffer;
        paramsUsrptrBuffer.size =  sizeof(VideoParamsUsrptrBuffer);
        paramsUsrptrBuffer.expectedSize = bufSize;
        paramsUsrptrBuffer.format = STRING_TO_FOURCC("NV12");
        paramsUsrptrBuffer.width = width;
        paramsUsrptrBuffer.height = height;

        mVideoEncoder->getParameters(&paramsUsrptrBuffer);
        if (ret != ENCODE_SUCCESS) {
            LOGE("Get usrptr failed");
            if (mSharedBufArray) {
                delete []mSharedBufArray;
                mSharedBufArray = NULL;
            }
            return OMX_ErrorUndefined;
        }

        mSharedBufArray[index].pointer = paramsUsrptrBuffer.usrPtr;
        mSharedBufArray[index].stride = paramsUsrptrBuffer.stride;
        mSharedBufArray[index].allocatedSize = paramsUsrptrBuffer.actualSize;
        mSharedBufArray[index].width = width;
        mSharedBufArray[index].height = height;
        mSharedBufArray[index].dataSize = mSharedBufArray[index].stride * height * 3 / 2;

        LOGV("width:%d, Height:%d, stride:%d, pointer:%p", mSharedBufArray[index].width,
             mSharedBufArray[index].height, mSharedBufArray[index].stride,
             mSharedBufArray[index].pointer);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::UploadSharedBuffers() {
    CHECK_BS_STATE();

    if (mBsState == BS_STATE_INVALID) {
        LOGW("buffer sharing not enabled, do nothing.");
        return OMX_ErrorNone;
    }

    BufferShareStatus ret = mBsInstance->encoderSetSharedBuffer(mSharedBufArray, mSharedBufCnt);
    CHECK_BS_STATUS	("encoderSetSharedBuffer");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetBSInfoToPort() {
    CHECK_BS_STATE();

    OMX_VIDEO_CONFIG_PRI_INFOTYPE privateinfoparam;
    memset(&privateinfoparam, 0, sizeof(privateinfoparam));
    SetTypeHeader(&privateinfoparam, sizeof(privateinfoparam));

    //caution: buffer-sharing info stored in INPORT (raw port)
    privateinfoparam.nPortIndex = INPORT_INDEX;
    if (mBsState == BS_STATE_INVALID) {
        privateinfoparam.nCapacity = 0;
        privateinfoparam.nHolder = NULL;
    } else {
        privateinfoparam.nCapacity = mSharedBufCnt;
        privateinfoparam.nHolder = mSharedBufArray;
    }
    OMX_ERRORTYPE ret = static_cast<PortVideo *>(ports[privateinfoparam.nPortIndex])->SetPortPrivateInfoParam(&privateinfoparam, false);

    return ret;
}

OMX_ERRORTYPE OMXVideoEncoderBase::TriggerBSMode() {
    CHECK_BS_STATE();

    if (mBsState == BS_STATE_INVALID) {
        LOGW("buffer sharing not enabled.");
        return OMX_ErrorNone;
    }

    BufferShareStatus ret = mBsInstance->encoderEnterSharingMode();
    if (ret == BS_PEER_DOWN) {
        LOGE("upstream component down during buffer sharing state transition.");
    }
    CHECK_BS_STATUS	("encoderEnterSharingMode");

    mBsState = BS_STATE_EXECUTING;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::CheckAndEnableBSMode() {
    BufferShareStatus bsret;

    if (mBsState != BS_STATE_INVALID) {
        LOGE("failed (incorrect state).");
        return OMX_ErrorUndefined;
    }

    if (mBsInstance->isBufferSharingModeEnabled()) {
        LOGV("Buffer sharing is enabled");
        mBsState = BS_STATE_LOADED;
    } else {
        LOGV("Buffer sharing is disabled");
        mBsState = BS_STATE_INVALID;
    }

    return OMX_ErrorNone;
}


OMX_ERRORTYPE OMXVideoEncoderBase::InitBSMode(void) {
    BufferShareStatus ret;

    if (mBsState != BS_STATE_INVALID) {
        LOGE("InitBSMode: Invalid buffer sharing state: %d", mBsState);
        return OMX_ErrorUndefined;
    }

    mSharedBufCnt = SHARED_BUFFER_CNT;
    mSharedBufArray = NULL;
    mBsInstance = BufferShareRegistry::getInstance();

    ret = mBsInstance->encoderRequestToEnableSharingMode();
    CHECK_BS_STATUS	("encoderRequestToEnableSharingMode");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::DeinitBSMode(void) {
    BufferShareStatus ret;

    CHECK_BS_STATE();

    if (mBsState == BS_STATE_INVALID) {
        return OMX_ErrorNone;
    }

    if (mBsState) {
        delete [] mSharedBufArray;
        mSharedBufArray = NULL;
    }

    ret = mBsInstance->encoderRequestToDisableSharingMode();
    CHECK_BS_STATUS	("encoderRequestToDisableSharingMode");


    mBsState = BS_STATE_INVALID;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::StartBufferSharing(void) {
    // TODO: consolidate the following APIs

    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = CheckAndEnableBSMode();
    CHECK_STATUS("CheckAndEnableBSMode");

    ret = AllocateSharedBuffers(mEncoderParams->resolution.width, mEncoderParams->resolution.height);
    CHECK_STATUS("AllocateSharedBuffers");

    ret = SetBSInfoToPort();
    CHECK_STATUS("SetBSInfoToPort");

    ret = UploadSharedBuffers();
    CHECK_STATUS("UploadSharedBuffers");

    ret = TriggerBSMode();
    CHECK_STATUS("UploadSharedBuffers");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::StopBufferSharing(void) {

    if (mBsState == BS_STATE_INVALID) {
        return OMX_ErrorNone;
    }

    BufferShareStatus ret = mBsInstance->encoderExitSharingMode();
    CHECK_BS_STATUS	("encoderExitSharingMode");


    mBsState = BS_STATE_LOADED;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::BuildHandlerList(void) {
    OMXComponentCodecBase::BuildHandlerList();
    AddHandler(OMX_IndexParamVideoPortFormat, GetParamVideoPortFormat, SetParamVideoPortFormat);
    AddHandler(OMX_IndexParamVideoBitrate, GetParamVideoBitrate, SetParamVideoBitrate);
    AddHandler((OMX_INDEXTYPE)OMX_IndexIntelPrivateInfo, GetIntelPrivateInfo, SetIntelPrivateInfo);
    AddHandler((OMX_INDEXTYPE)OMX_IndexParamIntelBitrate, GetParamIntelBitrate, SetParamIntelBitrate);
    AddHandler((OMX_INDEXTYPE)OMX_IndexConfigIntelBitrate, GetConfigIntelBitrate, SetConfigIntelBitrate);
    AddHandler((OMX_INDEXTYPE)OMX_IndexConfigIntelAIR, GetConfigIntelAIR, SetConfigIntelAIR);
    AddHandler(OMX_IndexConfigVideoFramerate, GetConfigVideoFramerate, SetConfigVideoFramerate);
    AddHandler(OMX_IndexConfigVideoIntraVOPRefresh, GetConfigVideoIntraVOPRefresh, SetConfigVideoIntraVOPRefresh);
    //AddHandler(OMX_IndexParamIntelAdaptiveSliceControl, GetParamIntelAdaptiveSliceControl, SetParamIntelAdaptiveSliceControl);
    AddHandler(OMX_IndexParamVideoProfileLevelQuerySupported, GetParamVideoProfileLevelQuerySupported, SetParamVideoProfileLevelQuerySupported);
    AddHandler((OMX_INDEXTYPE)OMX_IndexStoreMetaDataInBuffers, GetStoreMetaDataInBuffers, SetStoreMetaDataInBuffers);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetParamVideoPortFormat(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_PORTFORMATTYPE *p = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX_RANGE(p);
    CHECK_ENUMERATION_RANGE(p->nIndex, 1);

    PortVideo *port = NULL;
    port = static_cast<PortVideo *>(this->ports[p->nPortIndex]);
    memcpy(p, port->GetPortVideoParam(), sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetParamVideoPortFormat(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_PORTFORMATTYPE *p = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX_RANGE(p);
    CHECK_SET_PARAM_STATE();

    // TODO: do we need to check if port is enabled?
    PortVideo *port = NULL;
    port = static_cast<PortVideo *>(this->ports[p->nPortIndex]);
    port->SetPortVideoParam(p, false);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetParamVideoBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_BITRATETYPE *p = (OMX_VIDEO_PARAM_BITRATETYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mParamBitrate, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetParamVideoBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_BITRATETYPE *p = (OMX_VIDEO_PARAM_BITRATETYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    CHECK_SET_PARAM_STATE();
    OMX_U32 index = p->nPortIndex;
    PortVideo *port = NULL;
    // This disables other type of bitrate control mechanism
    // TODO: check if it is desired
    mParamIntelBitrate.eControlRate = OMX_Video_Intel_ControlRateMax;

    // TODO: can we override  mParamBitrate.nPortIndex (See SetPortBitrateParam)
    mParamBitrate.eControlRate = p->eControlRate;
    mParamBitrate.nTargetBitrate = p->nTargetBitrate;

    port = static_cast<PortVideo *>(ports[index]);
    ret = port->SetPortBitrateParam(p, false);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetIntelPrivateInfo(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_CONFIG_PRI_INFOTYPE *p = (OMX_VIDEO_CONFIG_PRI_INFOTYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mConfigPriInfo, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetIntelPrivateInfo(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_CONFIG_PRI_INFOTYPE *p = (OMX_VIDEO_CONFIG_PRI_INFOTYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // OMX_VIDEO_CONFIG_PRI_INFOTYPE is static parameter?
    CHECK_SET_PARAM_STATE();

    // TODO: can we override  mConfigPriInfo.nPortIndex (See SetPortPrivateInfoParam)

    if(p->nHolder != NULL) {
        // TODO: do we need to free nHolder?
        if (mConfigPriInfo.nHolder) {
            free(mConfigPriInfo.nHolder);
        }
        mConfigPriInfo.nCapacity = p->nCapacity;
        // TODO: nCapacity is in 8-bit unit or 32-bit unit?
        // TODO: check memory allocation
        mConfigPriInfo.nHolder = (OMX_PTR)malloc(sizeof(OMX_U32) * p->nCapacity);
        memcpy(mConfigPriInfo.nHolder, p->nHolder, sizeof(OMX_U32) * p->nCapacity);
    } else {
        mConfigPriInfo.nCapacity = 0;
        mConfigPriInfo.nHolder = NULL;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetParamIntelBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_INTEL_BITRATETYPE *p = (OMX_VIDEO_PARAM_INTEL_BITRATETYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mParamIntelBitrate, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetParamIntelBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_INTEL_BITRATETYPE *p = (OMX_VIDEO_PARAM_INTEL_BITRATETYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    CHECK_SET_PARAM_STATE();

    mParamIntelBitrate = *p;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetConfigIntelBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_CONFIG_INTEL_BITRATETYPE *p = (OMX_VIDEO_CONFIG_INTEL_BITRATETYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mConfigIntelBitrate, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetConfigIntelBitrate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    Encode_Status retStatus = ENCODE_SUCCESS;
    if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateMax) {
        LOGE("SetConfigIntelBitrate failed. Feature is disabled.");
        return OMX_ErrorUnsupportedIndex;
    }
    OMX_VIDEO_CONFIG_INTEL_BITRATETYPE *p = (OMX_VIDEO_CONFIG_INTEL_BITRATETYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // set in either Loaded state (ComponentSetParam) or Executing state (ComponentSetConfig)
    mConfigIntelBitrate = *p;

    // return OMX_ErrorNone if not in Executing state
    // TODO: return OMX_ErrorIncorrectStateOperation?
    CHECK_SET_CONFIG_STATE();

    if (mParamIntelBitrate.eControlRate != OMX_Video_Intel_ControlRateVideoConferencingMode) {
        LOGE("SetConfigIntelBitrate failed. Feature is supported only in VCM.");
        return OMX_ErrorUnsupportedSetting;
    }
    VideoConfigBitRate configBitRate;
    configBitRate.rcParams.bitRate = mConfigIntelBitrate.nMaxEncodeBitrate;
    configBitRate.rcParams.initQP = mConfigIntelBitrate.nInitialQP;
    configBitRate.rcParams.minQP = mConfigIntelBitrate.nMinQP;
    configBitRate.rcParams.windowSize = mConfigIntelBitrate.nWindowSize;
    configBitRate.rcParams.targetPercentage = mConfigIntelBitrate.nTargetPercentage;
    retStatus = mVideoEncoder->setConfig(&configBitRate);
    if(retStatus != ENCODE_SUCCESS) {
        LOGW("failed to set IntelBitrate");
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetConfigIntelAIR(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_CONFIG_INTEL_AIR *p = (OMX_VIDEO_CONFIG_INTEL_AIR *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mConfigIntelAir, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetConfigIntelAIR(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    Encode_Status retStatus = ENCODE_SUCCESS;
    if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateMax) {
        LOGE("SetConfigIntelAIR failed. Feature is disabled.");
        return OMX_ErrorUnsupportedIndex;
    }
    OMX_VIDEO_CONFIG_INTEL_AIR *p = (OMX_VIDEO_CONFIG_INTEL_AIR *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // set in either Loaded  state (ComponentSetParam) or Executing state (ComponentSetConfig)
    mConfigIntelAir = *p;

    // return OMX_ErrorNone if not in Executing state
    // TODO: return OMX_ErrorIncorrectStateOperation?
    CHECK_SET_CONFIG_STATE();

    if (mParamIntelBitrate.eControlRate != OMX_Video_Intel_ControlRateVideoConferencingMode) {
        LOGE("SetConfigIntelAIR failed. Feature is supported only in VCM.");
        return OMX_ErrorUnsupportedSetting;
    }

    VideoConfigAIR configAIR;
    VideoConfigIntraRefreshType configIntraRefreshType;
    if(mConfigIntelAir.bAirEnable == OMX_TRUE) {
        configAIR.airParams.airAuto = mConfigIntelAir.bAirAuto;
        configAIR.airParams.airMBs = mConfigIntelAir.nAirMBs;
        configAIR.airParams.airThreshold = mConfigIntelAir.nAirThreshold;
        configIntraRefreshType.refreshType = VIDEO_ENC_AIR;
    } else {
        configIntraRefreshType.refreshType = VIDEO_ENC_NONIR;
    }

    retStatus = mVideoEncoder->setConfig(&configAIR);
    if(retStatus != ENCODE_SUCCESS) {
        LOGW("Failed to set AIR config");
    }

    retStatus = mVideoEncoder->setConfig(&configIntraRefreshType);
    if(retStatus != ENCODE_SUCCESS) {
        LOGW("Failed to set refresh config");
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetConfigVideoFramerate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_CONFIG_FRAMERATETYPE *p = (OMX_CONFIG_FRAMERATETYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mConfigFramerate, sizeof(*p));
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetConfigVideoFramerate(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    Encode_Status retStatus = ENCODE_SUCCESS;
    if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateMax) {
        LOGE("SetConfigVideoFramerate failed. Feature is disabled.");
        return OMX_ErrorUnsupportedIndex;
    }
    OMX_CONFIG_FRAMERATETYPE *p = (OMX_CONFIG_FRAMERATETYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // set in either Loaded state  (ComponentSetParam) or Executing state (ComponentSetConfig)
    mConfigFramerate = *p;

    // return OMX_ErrorNone if not in Executing state
    // TODO, return OMX_ErrorIncorrectStateOperation?
    CHECK_SET_CONFIG_STATE();

    if (mParamIntelBitrate.eControlRate != OMX_Video_Intel_ControlRateVideoConferencingMode) {
        LOGE("SetConfigIntelAIR failed. Feature is supported only in VCM.");
        return OMX_ErrorUnsupportedSetting;
    }
    VideoConfigFrameRate framerate;
    framerate.frameRate.frameRateDenom = 1;
    framerate.frameRate.frameRateNum = mConfigFramerate.xEncodeFramerate >> 16;
    retStatus = mVideoEncoder->setConfig(&framerate);
    if(retStatus != ENCODE_SUCCESS) {
        LOGW("Failed to set frame rate config");
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetConfigVideoIntraVOPRefresh(OMX_PTR pStructure) {
    LOGW("GetConfigVideoIntraVOPRefresh is not supported.");
    return OMX_ErrorUnsupportedSetting;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetConfigVideoIntraVOPRefresh(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    Encode_Status retStatus = ENCODE_SUCCESS;
    OMX_CONFIG_INTRAREFRESHVOPTYPE *p = (OMX_CONFIG_INTRAREFRESHVOPTYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // return OMX_ErrorNone if not in Executing state
    // TODO: return OMX_ErrorIncorrectStateOperation?
    CHECK_SET_CONFIG_STATE();

    // TODO: apply VOP refresh configuration in Executing state
    VideoConfigIntraRefreshType configIntraRefreshType;
    if(mConfigIntelAir.bAirEnable) {
        configIntraRefreshType.refreshType = VIDEO_ENC_AIR;
    } else {
        configIntraRefreshType.refreshType = VIDEO_ENC_NONIR;
    }
    retStatus = mVideoEncoder->setConfig(&configIntraRefreshType);
    if(retStatus != ENCODE_SUCCESS) {
        LOGW("Failed to set refresh config");
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetParamIntelAdaptiveSliceControl(OMX_PTR pStructure) {

    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_INTEL_ADAPTIVE_SLICE_CONTROL *p = (OMX_VIDEO_PARAM_INTEL_ADAPTIVE_SLICE_CONTROL *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);
    memcpy(p, &mParamIntelAdaptiveSliceControl, sizeof(*p));

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetParamIntelAdaptiveSliceControl(OMX_PTR pStructure) {

    OMX_ERRORTYPE ret;
    if (mParamIntelBitrate.eControlRate == OMX_Video_Intel_ControlRateMax) {
        LOGE("SetParamIntelAdaptiveSliceControl failed. Feature is disabled.");
        return OMX_ErrorUnsupportedIndex;
    }
    OMX_VIDEO_PARAM_INTEL_ADAPTIVE_SLICE_CONTROL *p = (OMX_VIDEO_PARAM_INTEL_ADAPTIVE_SLICE_CONTROL *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // set only in Loaded state (ComponentSetParam)
    CHECK_SET_PARAM_STATE();

    mParamIntelAdaptiveSliceControl = *p;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetParamVideoProfileLevelQuerySupported(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_PROFILELEVELTYPE *p = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, OUTPORT_INDEX);

    // assign values instead of memory coping to avoid nProfileIndex being overridden
    p->eProfile = mParamProfileLevel.eProfile;
    p->eLevel = mParamProfileLevel.eLevel;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoEncoderBase::SetParamVideoProfileLevelQuerySupported(OMX_PTR pStructure) {
    LOGW("SetParamVideoProfileLevelQuerySupported is not supported.");
    return OMX_ErrorUnsupportedSetting;
}

OMX_ERRORTYPE OMXVideoEncoderBase::GetStoreMetaDataInBuffers(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    StoreMetaDataInBuffersParams *p = (StoreMetaDataInBuffersParams *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, INPORT_INDEX);

    p->bStoreMetaData = mForceBufferSharing;

    return OMX_ErrorNone;
};

OMX_ERRORTYPE OMXVideoEncoderBase::SetStoreMetaDataInBuffers(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    StoreMetaDataInBuffersParams *p = (StoreMetaDataInBuffersParams *)pStructure;

    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, INPORT_INDEX);

    LOGD("SetStoreMetaDataInBuffers (enabled = %x)", p->bStoreMetaData);
    mForceBufferSharing = p->bStoreMetaData;

    return OMX_ErrorNone;
};
