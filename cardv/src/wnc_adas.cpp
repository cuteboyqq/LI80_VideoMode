/*
  (C) 2023-2024 Wistron NeWeb Corporation (WNC) - All Rights Reserved

  This software and its associated documentation are the confidential and
  proprietary information of Wistron NeWeb Corporation (WNC) ("Company") and
  may not be copied, modified, distributed, or otherwise disclosed to third
  parties without the express written consent of the Company.

  Unauthorized reproduction, distribution, or disclosure of this software and
  its associated documentation or the information contained herein is a
  violation of applicable laws and may result in severe legal penalties.
*/
#include "json.hpp"
#include <iostream>
#include <fstream>
#ifdef QCS6490
#include "adas.hpp"
#endif

#ifdef SAV837
#include "wnc_adas.hpp"
#define WNC_DEBUG_INFO 0

// === SGS Libraries === //
#include "mi_ipu.h"
#include "mi_vpe.h"
#include "mi_scl.h"

#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include "ssplayer_player.h" //Alister add 2024-03-12
#include "ssplayer_video.h"  //2024-03-12

MI_PHY _outputaddr[2] = {0};
int    _u32DestSize[2];
int    _u32DestStride[2];
#endif

using namespace std;

#ifdef QCS6490
ADAS::ADAS(std::string configPath)
{
    auto m_logger = spdlog::stdout_color_mt("ADAS");
    m_logger->set_pattern("[%n] [%^%l%$] %v");

    m_logger->info("=================================================");
    m_logger->info("=                   WNC ADAS                    =");
    m_logger->info("=================================================");
    m_logger->info("Version: v{}", ADAS_VERSION);
    m_logger->info("-------------------------------------------------");

    logInfo("=================================================", "", LOG_INFO);
    logInfo("=                   WNC ADAS                    =", "", LOG_INFO);
    logInfo("=================================================", "", LOG_INFO);
    logInfo("Version: v " ADAS_VERSION, "", LOG_INFO);
    logInfo("-------------------------------------------------", "", LOG_INFO);

    // === initialize parameters === //
    _init(configPath);

    // Create a file rotating logger with 5 MB size max and 3 rotated files
    auto   max_size       = 1048576 * 5;
    auto   max_files      = 3;
    string logName        = "log.txt";
    string logPath        = m_dbg_logsDirPath + "/" + logName;
    auto   m_loggerOutput = spdlog::rotating_logger_mt("ADAS_DEBUG", logPath, max_size, max_files);
    m_loggerOutput->flush_on(spdlog::level::info);
    m_loggerOutput->set_pattern("%v");

    if (m_dbg_adas)
    {
        m_logger->set_level(spdlog::level::debug);
    }
    else
    {
        m_logger->set_level(spdlog::level::info);
    }

    // Enable YOLO-ADAS thread
    m_yoloADAS->runThread();
    m_yoloADAS_PostProc->runThread();
};
#endif

#ifdef SAV837
WNC_ADAS::WNC_ADAS(IPU_DlaInfo_S& stDlaInfo) : CIpuCommon(stDlaInfo)
{
    logInfo("=================================================", "", LOG_INFO);
    logInfo("=                   WNC ADAS                    =", "", LOG_INFO);
    logInfo("=================================================", "", LOG_INFO);
    logInfo("Version: v " ADAS_VERSION, "", LOG_INFO);
    logInfo("-------------------------------------------------", "", LOG_INFO);

    // SCL0 to RTSP
    m_stChnOutputPort[0].eModId    = E_MI_MODULE_ID_SCL;
    m_stChnOutputPort[0].u32DevId  = 3;
    m_stChnOutputPort[0].u32ChnId  = 2;
    m_stChnOutputPort[0].u32PortId = 0;

    // SCL1 to IPU (WNC-ADAS model)
    m_stChnOutputPort[1].eModId    = E_MI_MODULE_ID_SCL;
    m_stChnOutputPort[1].u32DevId  = 3;
    m_stChnOutputPort[1].u32ChnId  = 2;
    m_stChnOutputPort[1].u32PortId = 0;
    m_s32StreamFd                  = -1;

    IpuInit(); // Init SGS IPU // wnc add

    _init("/customer/adas/config/config.txt"); // Configurate parameters, default path for device

    // Create a file rotating logger with 5 MB size max and 3 rotated files
    auto   max_size  = 1048576 * 5;
    auto   max_files = 3;
    string logName   = "log.txt";
    string logPath   = m_dbg_logsDirPath + "/" + logName;
#ifdef SPDLOG_USE_SYSLOG
    auto m_loggerOutput = spdlog::syslog_logger_mt("adas-wnc", "adas-main", LOG_CONS | LOG_NDELAY, LOG_SYSLOG);
#else
    auto m_loggerOutput = spdlog::rotating_logger_mt("ADAS_DEBUG", logPath, max_size, max_files);
#endif
    m_loggerOutput->flush_on(spdlog::level::info);
    m_loggerOutput->set_pattern("%v");

    // Enable YOLO-ADAS thread
    m_yoloADAS->runThread();
    m_yoloADAS_PostProc->runThread();
};
#endif

WNC_ADAS::~WNC_ADAS()
{
#ifdef SAV837
    IpuDeInit();
#endif
    stopThread();

    delete m_adasConfigReader;
    delete m_config;
    delete m_yoloADAS;
    delete m_yoloADAS_PostProc;
    delete m_humanTracker;
    delete m_riderTracker;
    delete m_vehicleTracker;
    delete m_laneLineDet;
    delete m_ldw;
    delete m_fcw;
    delete m_OD_ROI;
    delete m_roiBBox;
    delete m_json_log;

    m_adasConfigReader  = nullptr;
    m_config            = nullptr;
    m_yoloADAS          = nullptr;
    m_yoloADAS_PostProc = nullptr;
    m_humanTracker      = nullptr;
    m_riderTracker      = nullptr;
    m_vehicleTracker    = nullptr;
    m_laneLineDet       = nullptr;
    m_ldw               = nullptr;
    m_fcw               = nullptr;
    m_OD_ROI            = nullptr;
    m_roiBBox           = nullptr;
    m_json_log          = nullptr;
};

void WNC_ADAS::stopThread()
{
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->info("Stopping Post Processing threads");
#endif
    logInfo("Stopping Post Processing threads", "", LOG_INFO);
    m_yoloADAS_PostProc->stopThread();

#ifdef QCS6490
    m_logger->info("Stopping AI threads");
#endif
    logInfo("Stopping AI threads", "", LOG_INFO);
    m_yoloADAS->stopThread();
}

#ifdef SAV837
MI_S32 WNC_ADAS::IpuGetSclOutputPortParam(void)
{
    // get preview scaler param
    MI_U32 u32Dev = 3;
    MI_U32 u32Chn = 2;

    ExecFunc(MI_SCL_GetOutputPortParam(u32Dev, u32Chn, 0, &m_stSclOutputPortParam[0]), MI_SUCCESS);
    ExecFunc(MI_SCL_GetOutputPortParam(u32Dev, u32Chn, 0, &m_stSclOutputPortParam[1]), MI_SUCCESS);

    return MI_SUCCESS;
};

MI_S32 WNC_ADAS::IpuInit()
{
    logInfo("IPUInit !!!", __FUNCTION__, LOG_DEBUG);

    IpuGetSclOutputPortParam(); // Get SCL port 0, 1, 2

    // SCL0: RTSP 960 x 960
    m_oriWidth  = m_stSclOutputPortParam[0].stSCLOutputSize.u16Width;
    m_oriHeight = m_stSclOutputPortParam[0].stSCLOutputSize.u16Height;

    // Save frame size of SCL0
    m_frameSizeList.push_back(std::make_pair(m_oriWidth, m_oriHeight));

    // SCL1: WNC-ADAS 320 x 320 TODO: Change
    m_modelWidth  = m_stSclOutputPortParam[1].stSCLOutputSize.u16Width;
    m_modelHeight = m_stSclOutputPortParam[1].stSCLOutputSize.u16Height;

    ExecFunc(IpuGetStreamFd(&m_stChnOutputPort[1], &m_s32StreamFd), MI_SUCCESS);

    int ori_width  = m_stSclOutputPortParam[1].stSCLOutputSize.u16Width;
    int ori_height = m_stSclOutputPortParam[1].stSCLOutputSize.u16Height;

    _u32DestStride[0] = wnc_ALIGN_UP(ori_width, 16) * 4;
    _u32DestSize[0]   = _u32DestStride[0] * ori_height;
    logInfo("SCL1 MMA Alloc Size: " + to_string(_u32DestSize[0]), __FUNCTION__, LOG_DEBUG);

    return MI_SUCCESS;
};

MI_S32 WNC_ADAS::IpuDeInit()
{
    // SCL1: WNCADAS
    ExecFunc(IpuPutStreamFd(m_s32StreamFd), MI_SUCCESS);
    ExecFunc(IpuPutStreamFd(m_s32StreamFd), MI_SUCCESS);
    MI_SYS_MMA_Free(0, _outputaddr[0]);

    return MI_SUCCESS;
};
#endif

bool WNC_ADAS::_init(std::string configPath)
{
    // Get Date Time
    utils::getDateTime(m_dbg_dateTime);

    // Read ADAS Configuration
    m_config           = new ADAS_Config_S();
    m_adasConfigReader = new ADAS_ConfigReader();
    m_adasConfigReader->read(configPath);
    m_config = m_adasConfigReader->getConfig();

    logInfo("Finished reading configuration file", __FUNCTION__, LOG_DEBUG);
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->info("[_init] Finished reading configuration file");
#endif

#ifdef SAV837
    // Check IPU firmware path
    if (!utils::checkFileExists(m_config->firmwarePath))
    {
        logInfo("IPU firmware path {" + m_config->firmwarePath + "} not found", __FUNCTION__, LOG_ERR);
        exit(1);
    }
#endif

    // Create AI model
    if (!utils::checkFileExists(m_config->modelPath))
    {
#ifdef QCS6490
        m_logger->error("Model path {} not found", m_config->modelPath);
#endif
        logInfo("Model path {" + m_config->modelPath + "} not found", __FUNCTION__, LOG_ERR);
        exit(1);
    }

    m_yoloADAS = new YOLOADAS(m_config);
#ifdef QCS6490
    m_logger->info("[_init] Initialized AI model");
#endif
    logInfo("Initialized AI model", __FUNCTION__, LOG_DEBUG);

    // ROI
    _initROI();

    // IO
    m_videoWidth  = m_config->frameWidth;
    m_videoHeight = m_config->frameHeight;
    m_segWidth    = m_config->semanticWidth;
    m_maskWidth   = m_config->maskWidth;

    // Model Input Size
    m_modelWidth  = m_config->modelWidth;
    m_modelHeight = m_config->modelHeight;

    // Processing
    m_frameStep_wnc     = m_config->procFrameStep;
    m_focalRescaleRatio = (float)m_videoHeight / (float)m_modelHeight;
    m_yVanish           = m_config->yVanish;

    // Lane Line Detection
    m_laneLineDet = new LaneLineDetection(m_config);
#ifdef QCS6490
    m_logger->info("[_init] Initialized Lane Line Detection module");
#endif
    logInfo("Initialized Lane Line Detection module", __FUNCTION__, LOG_DEBUG);

    // Lane Departure Warning
    m_ldw = new LDW(m_config);
#ifdef QCS6490
    m_logger->info("[_init] Initialized LDW module");
#endif
    logInfo("Initialized LDW module", __FUNCTION__, LOG_DEBUG);

    // Forward Collision Detection
    m_fcw = new FCW(m_config);
    m_fcw->setROI(m_roi);
#ifdef QCS6490
    m_logger->info("[_init] Initialized FCW module");
#endif
    logInfo("Initialized FCW module", __FUNCTION__, LOG_DEBUG);

    // JSON
    m_json_log = new JSON_LOG("output.json", m_config);

    m_OD_ROI = new BoundingBox(m_videoWidth * 0.15, m_videoHeight * 0.2, m_videoWidth * 0.85, m_videoHeight * 0.8, -1);

    // Post Processing
    m_yoloADAS_PostProc = new YOLOADAS_POSTPROC(m_config, m_OD_ROI);
#ifdef QCS6490
    m_logger->info("[_init] Initialized Post Processing module");
#endif
    logInfo("Initialized Post Processing module", __FUNCTION__, LOG_DEBUG);

    // Object Traccker
    m_humanTracker = new ObjectTracker(m_config, "human");
#ifdef QCS6490
    m_logger->info("[_init] Initialized Human Tracker module");
#endif
    logInfo("Initialized Human Tracker module", __FUNCTION__, LOG_DEBUG);

    m_riderTracker = new ObjectTracker(m_config, "rider");
#ifdef QCS6490
    m_logger->info("[_init] Initialized Rider Tracker module");
#endif
    logInfo("Initialized Rider Tracker module", __FUNCTION__, LOG_DEBUG);

    m_vehicleTracker = new ObjectTracker(m_config, "vehicle");
#ifdef QCS6490
    m_logger->info("[_init] Initialized Vehicle Tracker module");
#endif
    logInfo("Initialized Vehicle Tracker module", __FUNCTION__, LOG_DEBUG);

    m_vehicleTracker->setROI(m_roi);
    m_riderTracker->setROI(m_roi);
    m_humanTracker->setROI(m_roi);
#ifdef QCS6490
    m_logger->info("[_init] Initialized ROI for tracker modules");
#endif
    logInfo("Initialized ROI for tracker modules", __FUNCTION__, LOG_DEBUG);

    std::vector<DataFrame>* fcwDataBufferPtr = m_fcw->getDataBuffer();
    m_vehicleTracker->setDataBuffer(fcwDataBufferPtr);
    m_riderTracker->setDataBuffer(fcwDataBufferPtr);
    m_humanTracker->setDataBuffer(fcwDataBufferPtr);

    // TODO: Test
    m_pseudoLaneInfo   = {}; // init
    m_laneBoundaryInfo = {}; // TODO: init

    m_procResult = {};
    m_procResult.humanBBoxList.reserve(MAX_OBJ_BOX);
    m_procResult.riderBBoxList.reserve(MAX_OBJ_BOX);
    m_procResult.vehicleBBoxList.reserve(MAX_OBJ_BOX);
    m_procResult.roadSignBBoxList.reserve(MAX_OBJ_BOX);
    m_procResult.stopSignBBoxList.reserve(MAX_OBJ_BOX);
    m_procResult.vlaBBoxList.reserve(MAX_AREA_BOX);
    m_procResult.vpaBBoxList.reserve(MAX_AREA_BOX);
    m_procResult.duaBBoxList.reserve(MAX_AREA_BOX);
    m_procResult.dmaBBoxList.reserve(MAX_AREA_BOX);
    m_procResult.dlaBBoxList.reserve(MAX_AREA_BOX);
    m_procResult.dcaBBoxList.reserve(MAX_AREA_BOX);

    m_laneLineBox = {};
    m_laneLineBox.vlaBBoxList.reserve(MAX_AREA_BOX);
    m_laneLineBox.vpaBBoxList.reserve(MAX_AREA_BOX);
    m_laneLineBox.duaBBoxList.reserve(MAX_AREA_BOX);
    m_laneLineBox.dmaBBoxList.reserve(MAX_AREA_BOX);
    m_laneLineBox.dlaBBoxList.reserve(MAX_AREA_BOX);
    m_laneLineBox.dcaBBoxList.reserve(MAX_AREA_BOX);

    // TODO: test end

    _readDebugConfig();        // Debug Configuration
    _readDisplayConfig();      // Display Configuration
    _readShowProcTimeConfig(); // Show Processing Time Configuration

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_readDebugConfig()
{
    auto m_logger = spdlog::get("ADAS");

    if (m_config->stDebugConfig.WNC_ADAS)
    {
        m_dbg_adas = true;
    }

    if (m_config->stDebugConfig.yoloADAS)
    {
        m_dbg_yoloADAS = true;

        bool showMask = m_config->stDisplayConfig.laneLineMask;
        // m_yoloADAS->debugON(showMask);
        m_yoloADAS_PostProc->debugON(showMask);
    }

    if (m_config->stDebugConfig.laneLineDetection)
    {
        m_dbg_laneLineDetection = true;
        m_laneLineDet->debugON();
    }

    if (m_config->stDebugConfig.objectDetection)
        m_dbg_objectDetection = true;

    if (m_config->stDebugConfig.objectTracking)
    {
        m_dbg_objectTracking = true;
        if (m_config->stDebugConfig.humanTracker)
            m_humanTracker->debugON();
        if (m_config->stDebugConfig.riderTracker)
            m_riderTracker->debugON();
        if (m_config->stDebugConfig.vehicleTracker)
            m_vehicleTracker->debugON();
    }

    if (m_config->stDebugConfig.laneDeparture)
    {
        m_dbg_laneDeparture = true;
        m_ldw->debugON();
    }

    if (m_config->stDebugConfig.forwardCollision)
        m_dbg_forwardCollision = true;

    if (m_config->stDebugConfig.followingDistance)
        m_dbg_followingDistance = true;

    if (m_config->stDebugConfig.saveLogs)
        m_dbg_saveLogs = true;

    if (m_config->stDebugConfig.saveImages)
        m_dbg_saveImages = true;

    if (m_config->stDebugConfig.saveRawImages)
        m_dbg_saveRawImages = true;

    if (m_config->stDebugConfig.enableNotifyFcw)
        m_dbg_notify_fcw = true;

    if (m_config->stDebugConfig.enableNotifyLcw)
        m_dbg_notify_ldw = true;

    if (m_config->stDebugConfig.logsDirPath != "")
    {
        m_dbg_logsDirPath = m_config->stDebugConfig.logsDirPath + "/" + m_dbg_dateTime;

        if (m_config->stDebugConfig.saveLogs && utils::createDirectories(m_dbg_logsDirPath))
        {
#ifdef QCS6490
            m_logger->info("Folders created successfully: {}", m_dbg_logsDirPath);
#endif
            logInfo("Folders created successfully: " + m_dbg_logsDirPath, __FUNCTION__, LOG_DEBUG);
        }
        else
        {
#ifdef QCS6490
            m_logger->info("Error creating folders: {}", m_dbg_logsDirPath);
#endif
            logInfo("Error creating folders: " + m_dbg_logsDirPath, __FUNCTION__, LOG_DEBUG);
        }
    }

    if (m_config->stDebugConfig.imgsDirPath != "")
    {
        m_dbg_imgsDirPath = m_config->stDebugConfig.imgsDirPath + "/" + m_dbg_dateTime;

        if (m_config->stDebugConfig.saveImages && utils::createDirectories(m_dbg_imgsDirPath))
        {
#ifdef QCS6490
            m_logger->info("Folders created successfully: {}", m_dbg_imgsDirPath);
#endif
            logInfo("Folders created successfully: " + m_dbg_imgsDirPath, __FUNCTION__, LOG_DEBUG);
        }
        else
        {
#ifdef QCS6490
            m_logger->info("Error creating folders: {}", m_dbg_imgsDirPath);
#endif
            logInfo("Error creating folders: " + m_dbg_imgsDirPath, __FUNCTION__, LOG_DEBUG);
        }
    }

    if (m_config->stDebugConfig.rawImgsDirPath != "")
    {
        m_dbg_rawImgsDirPath = m_config->stDebugConfig.rawImgsDirPath + "/" + m_dbg_dateTime;

        if (m_config->stDebugConfig.saveRawImages && utils::createDirectories(m_dbg_rawImgsDirPath))
        {
#ifdef QCS6490
            m_logger->info("Folders created successfully: {}", m_dbg_rawImgsDirPath);
#endif
            logInfo("Folders created successfully: " + m_dbg_rawImgsDirPath, __FUNCTION__, LOG_DEBUG);
        }
        else
        {
#ifdef QCS6490
            m_logger->info("Error creating folders: {}", m_dbg_rawImgsDirPath);
#endif
            logInfo("Error creating folders: " + m_dbg_rawImgsDirPath, __FUNCTION__, LOG_DEBUG);
        }
    }

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_readDisplayConfig()
{
    if (m_config->stDisplayConfig.results)
        m_dsp_results = true;

    if (m_config->stDisplayConfig.laneLineMask)
        m_dsp_laneLineMask = true;

    if (m_config->stDisplayConfig.objectDetection)
        m_dsp_objectDetection = true;

    if (m_config->stDisplayConfig.objectTracking)
        m_dsp_objectTracking = true;

    if (m_config->stDisplayConfig.laneLineDetection)
        m_dsp_laneLineDetection = true;

    if (m_config->stDisplayConfig.vanishingLine)
        m_dsp_vanishingLine = true;

    if (m_config->stDisplayConfig.followingDistance)
        m_dsp_followingDistance = true;

    if (m_config->stDisplayConfig.laneDeparture)
        m_dsp_laneDeparture = true;

    if (m_config->stDisplayConfig.information)
        m_dsp_information = true;

    if (m_config->stDisplayConfig.forwardCollision)
        m_dsp_forwardCollision = true;

    if (m_config->stDisplayConfig.warningZone)
        m_dsp_warningZone = true;

    if (m_config->stDisplayConfig.maxFrameIndex)
        m_dsp_maxFrameIdx = m_config->stDisplayConfig.maxFrameIndex;

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_readShowProcTimeConfig()
{
    if (m_config->stShowProcTimeConfig.WNC_ADAS)
        m_estimateTime = true;

    if (m_config->stShowProcTimeConfig.yoloADAS)
    {
        m_yoloADAS->showProcTime();
        m_yoloADAS_PostProc->showProcTime();
    }

    if (m_config->stShowProcTimeConfig.laneFinder)
        m_laneLineDet->showProcTime();

    if (m_config->stShowProcTimeConfig.objectTracking)
    {
        m_humanTracker->showProcTime();
        m_riderTracker->showProcTime();
        m_vehicleTracker->showProcTime();
    }

    if (m_config->stShowProcTimeConfig.laneDeparture)
        m_ldw->showProcTime();

    if (m_config->stShowProcTimeConfig.forwardCollision)
        m_fcw->showProcTime();

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_initROI()
{
#ifdef QCS6490
    // Center of the ROI
    int xCenter = static_cast<int>(m_config->modelWidth * 0.5);
    int yCenter = static_cast<int>(m_config->modelHeight * 0.5);

    // Get new height and width value
    int newWidth  = static_cast<int>(m_config->modelWidth * 0.15);
    int newHeight = static_cast<int>(m_config->modelHeight * 0.4);

    // Calculate ROI's x boundaries
    int x1 = xCenter - static_cast<int>(newWidth * 0.6);
    int x2 = xCenter + static_cast<int>(newWidth * 0.6);

    // Calculate ROI's y boundaries
    int y1 = yCenter - static_cast<int>(m_config->modelHeight * 0.2);
    int y2 = yCenter + static_cast<int>(m_config->modelHeight * 0.2);

    // Create bounding box ROI area
    m_roi.x1 = x1;
    m_roi.y1 = y1;
    m_roi.x2 = x2;
    m_roi.y2 = y2;

    m_roiBBox = new BoundingBox(m_roi.x1, m_roi.y1, m_roi.x2, m_roi.y2, -1);
#endif

#ifdef SAV837
    // Center of the ROI
    int xCenter = static_cast<int>(m_config->modelWidth * 0.5);
    int yCenter = static_cast<int>(m_config->modelHeight * 0.5);

    // configuration solution file
    int newWidth  = static_cast<int>(m_config->modelWidth * 0.15);
    int newHeight = static_cast<int>(m_config->modelHeight * 0.4);

    // Calculate ROI's x boundaries
    int x1 = xCenter - static_cast<int>(newWidth * 0.6);
    int x2 = xCenter + static_cast<int>(newWidth * 0.6);

    // Calculate ROI's y boundaries
    int y1 = yCenter - static_cast<int>(m_config->modelHeight * 0.2);
    int y2 = yCenter + static_cast<int>(m_config->modelHeight * 0.2);

    // Create bounding box ROI area
    m_roi.x1 = x1;
    m_roi.y1 = y1;
    m_roi.x2 = x2;
    m_roi.y2 = y2;

    m_roiBBox = new BoundingBox(m_roi.x1, m_roi.y1, m_roi.x2, m_roi.y2, -1);
#endif

    return ADAS_SUCCESS;
}

// ============================================
//                   Main
// ============================================
#ifdef QCS6490
bool ADAS::run(cv::Mat& imgFrame, cv::Mat& resultMat)
{
    auto m_logger = spdlog::get("ADAS");
    auto time_0   = std::chrono::high_resolution_clock::now();
    auto time_1   = std::chrono::high_resolution_clock::now();

    bool ret = ADAS_SUCCESS;

    if (imgFrame.empty())
    {
        m_logger->warn("Input image is empty");
        logInfo("Input image is empty", __FUNCTION__, LOG_WARNING);
        return false;
    }

    int newXStart = static_cast<int>(imgFrame.cols * m_config->startXRatio);
    int newXEnd   = static_cast<int>(imgFrame.cols * m_config->endXRatio);
    int newYStart = static_cast<int>(imgFrame.rows * m_config->startYRatio);
    int newYEnd   = static_cast<int>(imgFrame.rows * m_config->endYRatio);

    // Get Image Frame
    if (m_dsp_results)
    {
        m_dsp_img = imgFrame.clone();
        m_dsp_img = m_dsp_img(cv::Range(newYStart, newYEnd), cv::Range(newXStart, newXEnd));
        cv::resize(m_dsp_img, m_dsp_img, cv::Size(m_config->frameWidth, m_config->frameHeight), cv::INTER_LINEAR);
    }

    // Entry Point
    if (m_frameIdx % m_frameStep == 0)
    {
        m_logger->info("");
        m_logger->info("========================================");
        m_logger->info("Frame Index: {}", m_frameIdx);
        m_logger->info("========================================");

        logInfo("", "", LOG_INFO);
        logInfo("========================================", "", LOG_INFO);
        logInfo("Frame Index: " + to_string(m_frameIdx), "", LOG_INFO);
        logInfo("========================================", "", LOG_INFO);

        // Get Image Frame
        m_img = imgFrame.clone();
        m_img = m_img(cv::Range(newYStart, newYEnd), cv::Range(newXStart, newXEnd));
        cv::resize(m_img, m_img, cv::Size(m_config->modelWidth, m_config->modelHeight), cv::INTER_LINEAR);

        // Update YOLO-ADAS frame buffer
        m_yoloADAS->updateInputFrame(m_img);

        // Get last prediction
        YOLOADAS_Prediction pred           = {};
        int                 predBufferSize = m_yoloADAS->getLastestPrediction(pred);
        if (predBufferSize > 0)
        {
            // Start doing post processing ...
            m_yoloADAS_PostProc->updatePredictionBuffer(pred);

            // m_procResult = {}; //TODO:
            int resultBufferSize = m_yoloADAS_PostProc->getLastestResult(m_procResult);

            if (resultBufferSize == 0)
                return false;

            {
                // Get Detected Lane Line Bounding Boxes
                if (!_laneLineDetection())
                {
                    m_logger->warn("Detecting lane lines failed!");
                    logInfo("Detecting lane lines failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Get Detected Bounding Boxes
                if (!_objectDetection())
                {
                    m_logger->warn("Detecting objects failed!");
                    logInfo("Detecting objects failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Lane Line Calibration Using Vehicle's Information
                if (!_laneLineCalibration())
                {
                    m_logger->warn("Calibrating lane lines failed!");
                    logInfo("Calibrating lane lines failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Filter some objects out to reduce CPU loading
                if (!_objectFiltering())
                {
                    m_logger->warn("Filtering objects failed!");
                    logInfo("Filtering objects failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Object Tracking
                if (!_objectTracking())
                {
                    m_logger->warn("Tracking objects failed!");
                    logInfo("Tracking objects failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Detect Lane Departure Event
                if (!_laneDepartureDetection())
                {
                    m_logger->warn("Detecting lane departure event failed!");
                    logInfo("Detecting lane departure event failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }

                // Forward Collision Warning
                if (!_forwardCollisionDetection())
                {
                    m_logger->warn("Detecting forward collision event failed!");
                    logInfo("Detecting forward collision event failed!", __FUNCTION__, LOG_WARNING);
                    ret = ADAS_FAILURE;
                }
            }

            // End of ADAS Tasks
            m_yoloADAS_PostProc->removeFirstResult();

            // Show Results
            _showDetectionResults();

            // Save Results to Debug Logs
            if (m_dbg_saveLogs)
                _saveDetectionResults();

            if (m_estimateTime)
            {
                time_1 = std::chrono::high_resolution_clock::now();
                m_logger->info("");
                m_logger->info("Processing Time: \t{} ms",
                               std::chrono::duration_cast<std::chrono::nanoseconds>(time_1 - time_0).count()
                                   / (1000.0 * 1000));
                logInfo("", "", LOG_INFO);
                logInfo("Processing Time: "
                            + to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(time_1 - time_0).count()
                                        / (1000.0 * 1000))
                            + " ms",
                        "", LOG_INFO);
            }
        }
    }

    // Draw and Save Results
    if (m_dsp_results && ret == ADAS_SUCCESS)
        _drawResults();

    if (m_dsp_results && m_dbg_saveImages && ret == ADAS_SUCCESS)
        _saveDrawResults();

    // Save Raw Images
    if (m_dbg_saveRawImages)
        _saveRawImages();

    getResultImage(resultMat); // copy resized_dsp_img to resultMat for CES

    // ADAS Log with JSON format
    ADAS_Results adasResult;
    getResults(adasResult);
    std::string json_log_str =
        m_json_log->JsonLogString_2(adasResult, m_humanBBoxList, m_riderBBoxList, m_vehicleBBoxList, m_roadSignBBoxList,
                                    m_stopSignBBoxList, m_trackedObjList, m_frameIdx);

    // Update frame index
    _updateFrameIndex();

    return ret;
}
#endif

#ifdef SAV837
void WNC_ADAS::IpuRunProcess() // Placeholder
{
    auto m_logger = spdlog::get("ADAS");
    // auto time_0   = std::chrono::high_resolution_clock::now();
    // auto time_1   = std::chrono::high_resolution_clock::now();

    bool   FRAME_SUCCESS = ADAS_SUCCESS;
    MI_S32 s32Ret        = 0;

    fd_set            read_fds;
    struct timeval    tv;
    MI_SYS_BufInfo_t  stBufInfo;
    MI_SYS_BUF_HANDLE stBufHandle;

    FD_ZERO(&read_fds);
    FD_SET(m_s32StreamFd, &read_fds);
    wnc_ADAS_Results adasResult;

    tv.tv_sec  = 0;
    tv.tv_usec = 1000 * 1000; // timeout : 1000ms

    s32Ret = select(m_s32StreamFd + 1, &read_fds, NULL, NULL, &tv);
    if (s32Ret <= 0)
    {
        cerr << "SCL No Select !!!" << endl;
        FRAME_SUCCESS = ADAS_FAILURE;
        exit(1);
    }

    if (FD_ISSET(m_s32StreamFd, &read_fds))
    {
        memset(&stBufInfo, 0x0, sizeof(MI_SYS_BufInfo_t));
        if (MI_SUCCESS == MI_SYS_ChnOutputPortGetBuf(&m_stChnOutputPort[1], &stBufInfo, &stBufHandle))
        {
            // Convert buffered image to cv::Mat
            MI_S32 s32Ret = MI_SUCCESS;

            auto time_0 = std::chrono::high_resolution_clock::now();
            auto time_1 = std::chrono::high_resolution_clock::now();

            // Check pixel format
            if ((stBufInfo.stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ABGR8888)
                && (stBufInfo.stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ARGB8888)
                && (stBufInfo.stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420))
            {
                cerr << "ERROR!!! Pixel format is not valid" << endl;
                FRAME_SUCCESS = ADAS_FAILURE;
                exit(1);
            }

            m_logger->debug("Start converting buffered image to cv::Mat");
            _scaleToModelSize((MI_SYS_BufInfo_t*)&stBufInfo); // Entry Point at the end
            m_logger->debug("Finished converting buffered image to cv::Mat");

            if (m_frameIdx_wnc % m_frameStep_wnc == 0)
            {
                // m_logger->info("################################# Size before processing {}",
                // m_yoloADAS->m_midLinePointLists.size());
                m_logger->debug("");
                m_logger->debug("========================================");
                m_logger->debug("Frame Index: {}", m_frameIdx_wnc);
                m_logger->debug("========================================");

                // Update YOLO-ADAS frame buffer
                m_yoloADAS->updateInputFrame(m_img);

                // AI Inference
                if (m_yoloADAS->isInferenceDone())
                {
                    // Get last prediction
                    YOLOADAS_Prediction pred;
                    m_yoloADAS->getLastestPrediction(pred);
                    pred.isProcessed = true;

                    // Start doing post processing ...
                    m_yoloADAS_PostProc->run(pred);

                    // Lane Line Detection
                    if (!_laneLineDetection())
                    {
                        m_logger->warn("Detect lane lines failed ...");
                        FRAME_SUCCESS = ADAS_FAILURE;
                    }

                    // Get Detected Bounding Boxes
                    if (!_objectDetection())
                    {
                        m_logger->warn("Detect objects failed ...");
                        FRAME_SUCCESS = ADAS_FAILURE;
                    }

                    // Object Tracking
                    if (!_objectTracking())
                    {
                        m_logger->warn("Track objects failed ...");
                        FRAME_SUCCESS = ADAS_FAILURE;
                    }

                    // Detect Lane Departure Event
                    if (!_laneDepartureDetection())
                    {
                        m_logger->warn("Detect lane departure event failed ...");
                        FRAME_SUCCESS = ADAS_FAILURE;
                    }

                    // Forward Collision Warning
                    if (!_forwardCollisionDetection())
                    {
                        m_logger->warn("Detect forward collision event failed ...");
                        FRAME_SUCCESS = ADAS_FAILURE;
                    }

                    // Show Results
                    _showDetectionResults();

                    // Save results to debug logs
                    if (m_dbg_saveLogs)
                        _saveDetectionResults();

                    if (m_estimateTime)
                    {
                        time_1 = std::chrono::high_resolution_clock::now();
                        m_logger->info("");
                        m_logger->info("Processing Time: \t{} ms",
                                       std::chrono::duration_cast<std::chrono::nanoseconds>(time_1 - time_0).count()
                                           / (1000.0 * 1000));
                    }
                }
            }
            else
            {
                usleep(100);
            }

            // Draw and Save Results
            if (m_dsp_results && FRAME_SUCCESS == ADAS_SUCCESS)
                _drawResults();

            if (m_dsp_results && m_dbg_saveImages && FRAME_SUCCESS == ADAS_SUCCESS)
                _saveDrawResults();

            if (m_dbg_saveRawImages)
                _saveRawImages();

            FRAME_SUCCESS = ADAS_SUCCESS;

            getResults(adasResult);
            if (adasResult.eventType == ADAS_EVENT_LDW_LEFT)
                m_logger->debug("Detect LDW Left Event!");
            if (adasResult.eventType == ADAS_EVENT_LDW_RIGHT)
                m_logger->debug("Detect LDW Right Event!");
            if (adasResult.eventType == ADAS_EVENT_FCW)
                m_logger->debug("Detect FCW Event!");
            if (adasResult.eventType == ADAS_EVENT_LDW_FCW)
            {
                m_logger->debug("Detect LDW Event!");
                m_logger->debug("Detect FCW Event!");
            }

            _updateFrameIndex();
            m_logger->debug("Showing IPU Output");
            // IpuShowOutput(adasResult);

            if (MI_SUCCESS != MI_SYS_ChnOutputPortPutBuf(stBufHandle))
            {
                cerr << "[OD_Tracking] MI_SYS_ChnOutputPortPutBuf error" << endl;
            }
        }
        else
        {
            m_logger->warn("Failed to fetch system buffer");
            FRAME_SUCCESS = ADAS_FAILURE;
        }
    }
}
#endif

// ============================================
//              Work Flow Functions
// ============================================
#ifdef SAV837
bool WNC_ADAS::_scaleToModelSize(MI_SYS_BufInfo_t* pstBufInfo)
{
    int            PixerBytes     = 1;
    MI_U16         u16Height      = 0;
    MI_U32         u32DestStride  = 0;
    MI_U32         u16ModelWidth  = 0;
    MI_U32         u16ModelHeight = 0;
    unsigned char* pSrcImage      = NULL;
    unsigned char* pDstImage      = NULL;

    // Check buffer type
    if (!pstBufInfo || pstBufInfo->eBufType != E_MI_SYS_BUFDATA_FRAME)
    {
        logInfo("Buffer data type is invalid!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }

    // Check image's channel number
    if (m_yoloADAS->m_processedData.imgResizeC != 3)
    {
        logInfo("Image channel number is not 3!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }

    // Locate Virtual address of the Buffer
    pSrcImage = (unsigned char*)pstBufInfo->stFrameData.pVirAddr[0];
    if (!pSrcImage)
    {
        logInfo("Failed to obtain source image virtual address inside the buffer!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }

    u16ModelHeight = m_yoloADAS->m_processedData.imgResizeH;
    u16ModelWidth  = m_yoloADAS->m_processedData.imgResizeW;

    if ((pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ABGR8888)
        || (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ARGB8888))
    {
        PixerBytes    = 4;
        u32DestStride = wnc_ALIGN_UP(u16ModelWidth, 16) * 4;
        u16Height     = pstBufInfo->stFrameData.u16Height;
    }
    else if (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420)
    {
        u32DestStride = wnc_ALIGN_UP(u16ModelWidth, 16);
        u16Height     = pstBufInfo->stFrameData.u16Height * 3 / 2;
    }
#if WNC_DEBUG_INFO

#endif
    pDstImage = (unsigned char*)m_yoloADAS->m_inputTensorVector.astArrayTensors[0].ptTensorData[0];
    memcpy(pDstImage, pSrcImage, u16ModelWidth * u16ModelHeight * PixerBytes);

    cv::Mat imgFrame = cv::Mat(u16ModelHeight, u16ModelWidth, CV_8UC4, pDstImage);
    // cv::Mat rgbImage(imgFrame.rows, imgFrame.cols, CV_8UC3);
    // cv::cvtColor(imgFrame, rgbImage, cv::COLOR_BGRA2BGR);

    // MI_SYS_FlushInvCache(m_yoloADAS->m_inputTensorVector.astArrayTensors[0].ptTensorData[0], u16ModelWidth *
    // u16ModelHeight * PixerBytes);

    // cv::Mat img = cv::imread("test.jpg", -1);
    // // int rows = img.rows;
    // // int cols = img.cols;
    // // int cx = (cols / 2) + 1;
    // // int c1 = cx - (rows / 2);
    // // int c2 = cx + (rows / 2);
    // // cv::Mat croppedImage = img(cv::Range(0, rows), cv::Range(c1, c2));
    // cv::Size inputSize = cv::Size(u16ModelWidth, u16ModelHeight);
    // cv::Mat ResizedImg;
    // cv::Mat Sample;
    // cv::resize(img, ResizedImg, inputSize);
    // // if (ResizedImg.empty())
    // m_logger->info("REACHED");
    // cv::cvtColor(ResizedImg, Sample, cv::COLOR_BGR2BGRA);
    // // cv::namedWindow("Car",cv::WINDOW_AUTOSIZE);
    // cv::imwrite("test_output.jpg", Sample);
    // m_logger->info("PASSED");
    // // // cv::waitKey(3000);
    // // // cv::destroyWindow("Car");

    // memcpy(pDstImage, Sample.data, u16ModelWidth * u16ModelHeight * 4);

    if (m_dsp_results)
        // TODO: cv::cvtColor(imgFrame, m_dsp_img, cv::COLOR_RGBA2RGB);
        m_dsp_img = imgFrame.clone();

    // Entry Point
    m_img = imgFrame.clone();

    memcpy(pDstImage, imgFrame.data, u16ModelWidth * u16ModelHeight * 4);
    // cv::imwrite("input.jpg", m_img); // wnc modify to check input image

    // m_logger->info("FINISHED");
    return MI_SUCCESS;
}
//=================================================Video
// mode=======================================================================
cv::Mat WNC_ADAS::avframeToCvmat(const AVFrame* frame)
{
    int     width  = frame->width;
    int     height = frame->height;
    cv::Mat image(height, width, CV_8UC3);
    int     cvLinesizes[1];
    cvLinesizes[0]         = image.step1();
    SwsContext* conversion = sws_getContext(width, height, (AVPixelFormat)frame->format, width, height,
                                            AVPixelFormat::AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    sws_scale(conversion, frame->data, frame->linesize, 0, height, &image.data, cvLinesizes);
    sws_freeContext(conversion);
    return image;
}
//=====================Alister add 2024-03-14=====================================
bool WNC_ADAS::_scaleToModelSize_FromVideo(MI_SYS_BufInfo_t* pstBufInfo)
{
    int    PixerBytes     = 1;
    MI_U16 u16Height      = 0;
    MI_U32 u32DestStride  = 0;
    MI_U32 u16ModelWidth  = 0;
    MI_U32 u16ModelHeight = 0;
    // unsigned char* pSrcImage      = NULL;
    AVFrame*       pSrcImage = NULL;
    unsigned char* pDstImage = NULL;

    // Check buffer type
    if (!pstBufInfo || pstBufInfo->eBufType != E_MI_SYS_BUFDATA_FRAME)
    {
        logInfo("Buffer data type is invalid!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }

    // Check image's channel number
    if (m_yoloADAS->m_processedData.imgResizeC != 3)
    {
        logInfo("Image channel number is not 3!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }

    // Locate Virtual address of the Buffer
    // pSrcImage = (unsigned char*)pstBufInfo->stFrameData.pVirAddr[0];

    pSrcImage = dequeue(&vfqueue); // Alister 2024-03-14
    if (!pSrcImage)
    {
        logInfo("Failed to obtain source image virtual address inside the buffer!", __func__, LOG_ERR);
        return E_MI_ERR_FAILED;
    }
    cv::Mat imgFrame = avframeToCvmat(pSrcImage); // Alister 2024-03-14
    u16ModelHeight   = m_yoloADAS->m_processedData.imgResizeH;
    u16ModelWidth    = m_yoloADAS->m_processedData.imgResizeW;

    if ((pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ABGR8888)
        || (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ARGB8888))
    {
        PixerBytes    = 4;
        u32DestStride = wnc_ALIGN_UP(u16ModelWidth, 16) * 4;
        u16Height     = pstBufInfo->stFrameData.u16Height;
    }
    else if (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420)
    {
        u32DestStride = wnc_ALIGN_UP(u16ModelWidth, 16);
        u16Height     = pstBufInfo->stFrameData.u16Height * 3 / 2;
    }
#if WNC_DEBUG_INFO

#endif
    pDstImage = (unsigned char*)m_yoloADAS->m_inputTensorVector.astArrayTensors[0].ptTensorData[0];
    memcpy(pDstImage, pSrcImage, u16ModelWidth * u16ModelHeight * PixerBytes);

    // cv::Mat imgFrame = cv::Mat(u16ModelHeight, u16ModelWidth, CV_8UC4, pDstImage);

    // memcpy(pDstImage, Sample.data, u16ModelWidth * u16ModelHeight * 4);

    if (m_dsp_results)
        // TODO: cv::cvtColor(imgFrame, m_dsp_img, cv::COLOR_RGBA2RGB);
        m_dsp_img = imgFrame.clone();

    // Entry Point
    m_img = imgFrame.clone();

    memcpy(pDstImage, imgFrame.data, u16ModelWidth * u16ModelHeight * 4);
    // cv::imwrite("input.jpg", m_img); // wnc modify to check input image

    // m_logger->info("FINISHED");
    return MI_SUCCESS;
}
//================================================================================
#endif

bool WNC_ADAS::_modelInfernece()
{
    if (!m_yoloADAS->run(m_img))
        return ADAS_FAILURE;

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_laneDepartureDetection()
{
    m_isLaneDeparture = m_ldw->run(m_currLaneInfo, m_laneLineInfo, m_roadSignBBoxList, m_linePointList);

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_laneLineDetection()
{
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
#endif
    m_laneLineBox.vlaBBoxList = m_procResult.vlaBBoxList;
    m_laneLineBox.vpaBBoxList = m_procResult.vpaBBoxList;
    m_laneLineBox.dlaBBoxList = m_procResult.dlaBBoxList;
    m_laneLineBox.dmaBBoxList = m_procResult.dmaBBoxList;
    m_laneLineBox.duaBBoxList = m_procResult.duaBBoxList;
    m_laneLineBox.dcaBBoxList = m_procResult.dcaBBoxList;

// Debug Logs
#ifdef QCS6490
    m_logger->debug("[_laneLineDetection] Num of VLA Bounding Box: {} / {}", (int)m_laneLineBox.vlaBBoxList.size(),
                    (int)m_laneLineBox.vlaBBoxList.size());

    m_logger->debug("[_laneLineDetection] Num of VPA Bounding Box: {} / {}", (int)m_laneLineBox.vpaBBoxList.size(),
                    (int)m_laneLineBox.vpaBBoxList.size());

    m_logger->debug("[_laneLineDetection] Num of DLA Bounding Box: {} / {}", (int)m_laneLineBox.dlaBBoxList.size(),
                    (int)m_laneLineBox.dlaBBoxList.size());

    m_logger->debug("[_laneLineDetection] Num of DMA Bounding Box: {} / {}", (int)m_laneLineBox.dmaBBoxList.size(),
                    (int)m_laneLineBox.dmaBBoxList.size());

    m_logger->debug("[_laneLineDetection] Num of DUA Bounding Box: {} / {}", (int)m_laneLineBox.duaBBoxList.size(),
                    (int)m_laneLineBox.duaBBoxList.size());

    m_logger->debug("[_laneLineDetection] Num of DCA Bounding Box: {} / {}", (int)m_laneLineBox.dcaBBoxList.size(),
                    (int)m_laneLineBox.dcaBBoxList.size());
#endif

    logInfo("Num of VLA Bounding Box: " + to_string((int)m_laneLineBox.vlaBBoxList.size()) + "/ "
                + to_string((int)m_laneLineBox.vlaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    logInfo("Num of VPA Bounding Box: " + to_string((int)m_laneLineBox.vpaBBoxList.size()) + "/ "
                + to_string((int)m_laneLineBox.vpaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    logInfo("Num of DLA Bounding Box: " + to_string((int)m_laneLineBox.dlaBBoxList.size()) + "/ "
                + to_string((int)m_laneLineBox.dlaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    logInfo("Num of DMA Bounding Box: " + to_string((int)m_laneLineBox.dmaBBoxList.size()) + "/ "
                + to_string((int)m_laneLineBox.dmaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    logInfo("Num of DUA Bounding Box: " + to_string((int)m_laneLineBox.duaBBoxList.size()) + "/ " + \  
			to_string((int)m_laneLineBox.duaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    logInfo("Num of DCA Bounding Box: " + to_string((int)m_laneLineBox.dcaBBoxList.size()) + "/ "
                + to_string((int)m_laneLineBox.dcaBBoxList.size()),
            __FUNCTION__, LOG_DEBUG);

    m_currLaneInfo = m_laneLineDet->find(m_laneLineBox, m_laneLineInfo);

    m_isDetectLine = m_laneLineDet->isDetectLine();

#ifdef QCS6490
    m_logger->debug("[_laneLineDetection] pLeftCarhood = ({}, {})", m_currLaneInfo.pLeftCarhood.x,
                    m_currLaneInfo.pLeftCarhood.y);
    m_logger->debug("[_laneLineDetection] pLeftFar = ({}, {})", m_currLaneInfo.pLeftFar.x, m_currLaneInfo.pLeftFar.y);
    m_logger->debug("[_laneLineDetection] pRightCarhood = ({}, {})", m_currLaneInfo.pRightCarhood.x,
                    m_currLaneInfo.pRightCarhood.y);
    m_logger->debug("[_laneLineDetection] pRightFar = ({}, {})", m_currLaneInfo.pRightFar.x,
                    m_currLaneInfo.pRightFar.x);
    m_logger->debug("[_laneLineDetection] pLeftDegree = ({})", m_currLaneInfo.leftDegree);
    m_logger->debug("[_laneLineDetection] pRightDegree = ({})", m_currLaneInfo.rightDegree);
#endif

    logInfo("pLeftCarhood = (" + to_string(m_currLaneInfo.pLeftCarhood.x) + ", "
                + to_string(m_currLaneInfo.pLeftCarhood.y) + ")",
            __FUNCTION__, LOG_DEBUG);
    logInfo("pLeftFar = (" + to_string(m_currLaneInfo.pLeftFar.x) + ", " + to_string(m_currLaneInfo.pLeftFar.y) + ")",
            __FUNCTION__, LOG_DEBUG);
    logInfo("pRightCarhood = (" + to_string(m_currLaneInfo.pRightCarhood.x) + ", "
                + to_string(m_currLaneInfo.pRightCarhood.y) + ")",
            __FUNCTION__, LOG_DEBUG);
    logInfo("pRightFar = (" + to_string(m_currLaneInfo.pRightFar.x) + ", " + to_string(m_currLaneInfo.pRightFar.x)
                + ")",
            __FUNCTION__, LOG_DEBUG);
    logInfo("pLeftDegree = " + to_string(m_currLaneInfo.leftDegree), __FUNCTION__, LOG_DEBUG);
    logInfo("pRightDegree = " + to_string(m_currLaneInfo.rightDegree), __FUNCTION__, LOG_DEBUG);

    // Update Vanish Line Y when detect lane lines
    int tmpVanishY = m_laneLineDet->getVanishY();
    if (tmpVanishY != 0)
    {
        m_yVanish = tmpVanishY;
        m_yVanish = std::round(m_yVanish * m_focalRescaleRatio);
    }

    m_fcw->setDetectLines(m_isDetectLine);

    if (m_isDetectLine)
    {
        // Update Detection Zone
        m_fcw->setZone(m_currLaneInfo);
        m_fcw->updateDefaultZone(m_currLaneInfo);

        // Direction Vector
        _updateDirectionVector(m_currLaneInfo);

        // Vanish line
        m_fcw->setVanishLine(m_yVanish);

        // Lane Head Boundary
        m_fcw->setLaneHeadBoundary(m_currLaneInfo.pLeftFar.x, m_currLaneInfo.pRightFar.x);

        // Enable LDW
        m_ldw->enable();
    }
    else
    {
        //
        LaneInfo pseudoLaneInfo = {};
        m_laneLineDet->getPseudoLaneInfo(pseudoLaneInfo);
        m_fcw->setZone(pseudoLaneInfo);

        // Disable LDW
        m_ldw->disable();
    }

// Debug Logs
#ifdef QCS6490
    m_logger->debug("[_laneLineDetection] Detect lane lines = {}", m_isDetectLine);
    m_logger->debug("[_laneLineDetection] Y of vanishing line = {}", m_yVanish);
#endif

    logInfo("Detect lane lines = " + to_string(m_isDetectLine), __FUNCTION__, LOG_DEBUG);
    logInfo("Y of vanishing line = " + to_string(m_yVanish), __FUNCTION__, LOG_DEBUG);

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_objectDetection()
{
    m_humanBBoxList    = m_procResult.humanBBoxList;
    m_riderBBoxList    = m_procResult.riderBBoxList;
    m_vehicleBBoxList  = m_procResult.vehicleBBoxList;
    m_roadSignBBoxList = m_procResult.roadSignBBoxList;
    m_stopSignBBoxList = m_procResult.stopSignBBoxList;

// Debug Logs
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->debug("[_objectDetection] Num of Human Bounding Box: {}", (int)m_humanBBoxList.size());
    m_logger->debug("[_objectDetection] Num of Rider Bounding Box: {}", (int)m_riderBBoxList.size());
    m_logger->debug("[_objectDetection] Num of Vehicle Bounding Box: {}", (int)m_vehicleBBoxList.size());
    m_logger->debug("[_objectDetection] Num of Road Sign Bounding Box: {}", (int)m_roadSignBBoxList.size());
    m_logger->debug("[_objectDetection ]Num of Stop Sign Bounding Box: {}", (int)m_stopSignBBoxList.size());
#endif

    logInfo("Num of Human Bounding Box: " + to_string(m_humanBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Num of Rider Bounding Box: " + to_string(m_riderBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Num of Vehicle Bounding Box: " + to_string(m_vehicleBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Num of Road Sign Bounding Box: " + to_string(m_roadSignBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Num of Stop Sign Bounding Box: " + to_string(m_stopSignBBoxList.size()), __FUNCTION__, LOG_DEBUG);

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_laneLineCalibration()
{
    if (m_fcw->isAllVehicleNegativeDistance(m_vehicleBBoxList))
    {
        m_laneLineDet->resetVanishY();
    }
    else if (m_fcw->isFrontCarNegativeDistance(m_vehicleBBoxList))
    {
        m_laneLineDet->resetVanishY();
    }

    if (m_laneLineDet->getLaneBoundaryInfo(m_laneBoundaryInfo))
    {
        m_isGetLaneBoundary = true;
    }
    else
    {
        m_isGetLaneBoundary = false;
    }

    // Remove vehicles out of boundary
    if (m_isGetLaneBoundary)
    {
        m_fcw->vehicleBoxFilterWithBoundary(m_vehicleBBoxList, m_f_vehicleBBoxList, m_laneBoundaryInfo);
    }
    else
    {
        m_f_vehicleBBoxList = m_vehicleBBoxList;
    }

    // Case 1: When lane line detection fails
    if (!m_isDetectLine)
    {
        int idxBox = m_fcw->isCarRightAheadCloserThanMeters(m_f_vehicleBBoxList, 40, 0.97);

        //, There is a car ahead
        if (idxBox > 0)
        {
            m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo, m_f_vehicleBBoxList[idxBox]);
        }
        // There is no car ahead
        else
        {
            m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo);
        }
        //
        m_fcw->setZone(m_pseudoLaneInfo);
    }

    // Case2: Using information of front cars to adjust lane mask
    else if (m_vehicleBBoxList.size() > 1)
    {
        if (m_laneLineDet->getBestVpaBox(m_bestVpaBox))
        {
            int idxBox = m_fcw->isCarInVpaBoxCloserThanMeters(m_f_vehicleBBoxList, m_bestVpaBox, 55);
            // cout << "isCarInVpaBoxCloserThanMeters adjust lane head, idx = " << idxBox << endl;
            if (idxBox >= 0)
            {
                // cout << "idxBox >= 0" << endl;
                m_laneLineDet->adjustLaneInfoWithVehicleBox(m_currLaneInfo, m_f_vehicleBBoxList[idxBox], false);
                m_fcw->setZone(m_currLaneInfo);
                _updateDirectionVector(m_currLaneInfo);
            }
            else
            {
                int idxBoxNew = m_fcw->isCarRightAheadCloserThanMeters(m_f_vehicleBBoxList, 55, 0.97);
                // cout << "isCarRightAheadCloserThanMeters adjust lane head, idxBoxNew = " << idxBoxNew << endl;
                if (idxBoxNew >= 0)
                {
                    // cout << "idxBoxNew >= 0" << endl;
                    m_laneLineDet->adjustLaneInfoWithVehicleBox(m_currLaneInfo, m_f_vehicleBBoxList[idxBoxNew], true);
                    m_fcw->setZone(m_currLaneInfo);
                    _updateDirectionVector(m_currLaneInfo);
                }
                else
                {
                    // cout << "Using Boundary!!!" << endl; //TODO:
                    // m_currLaneInfo.pLeftFar = m_laneBoundaryInfo.pLeftFar; //TODO:
                    // if (m_currLaneInfo.pLeftFar.x > m_currLaneInfo.pRightFar.x)
                    // {
                    // 	m_currLaneInfo.pLeftFar.x = m_currLaneInfo.pRightFar.x-20; //TODO:
                    // 	m_currLaneInfo.pLeftFar.y = m_currLaneInfo.pRightFar.y;
                    // }

                    // m_currLaneInfo.pRightFar = m_laneBoundaryInfo.pRightFar; //TODO:
                    // if (m_currLaneInfo.pRightFar.x < m_currLaneInfo.pLeftFar.x)
                    // {
                    // 	m_currLaneInfo.pRightFar.x = m_currLaneInfo.pLeftFar.x+20; //TODO:
                    // 	m_currLaneInfo.pRightFar.y = m_currLaneInfo.pLeftFar.y;
                    // }

                    // cout << "Using Pseudo Lane Mask" << endl;

                    m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo);
                    m_fcw->setZone(m_currLaneInfo);

                    // Direction Vector
                    _updateDirectionVector(m_currLaneInfo);
                }
            }
        }
    }

    // TODO: Checking to use previous results, implementing into lane line detection class
    m_laneLineDet->calibrationUsingPrevLaneInfo(m_currLaneInfo);
    m_fcw->setZone(m_currLaneInfo);
    _updateDirectionVector(m_currLaneInfo);

    // if too close to front car, camera cannot see lane line
    // so we use pseudo lane mask
    if (m_fcw->isCarAheadCloserThanMeters(m_vehicleBBoxList, 25))
    {
        // cout << "Ready for pseudo !!!!!!!" << endl;

        if (m_f_vehicleBBoxList.size() > 0)
        {
            int idxBox = m_fcw->isCarRightAheadCloserThanMeters(m_f_vehicleBBoxList, 40, 0.94);
            // cout << "pseudo idxBox of m_f_vehicleBBoxList = " << idxBox << endl;
            //, There is a car ahead
            if (idxBox >= 0)
            {
                m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo, m_f_vehicleBBoxList[idxBox]);
            }
        }
        else if (m_vehicleBBoxList.size() > 0)
        {
            int idxBox = m_fcw->isCarRightAheadCloserThanMeters(m_vehicleBBoxList, 40, 0.94);
            // cout << "pseudo idxBox of m_vehicleBBoxList = " << idxBox << endl;
            //, There is a car ahead
            if (idxBox >= 0)
            {
                m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo, m_vehicleBBoxList[idxBox]);
            }
        }
        // There is no car ahead
        else
        {
            m_laneLineDet->getPseudoLaneInfo(m_pseudoLaneInfo);
        }
        m_fcw->setZone(m_pseudoLaneInfo);
        m_isDetectLine = false;
    }

    return true;
}

bool WNC_ADAS::_objectFiltering()
{
    m_fcw->humanBoxFilter(m_humanBBoxList, m_f_humanBBoxList);
    m_fcw->riderBoxFilter(m_riderBBoxList, m_f_riderBBoxList);
    m_fcw->vehicleBoxFilter(m_vehicleBBoxList, m_f_vehicleBBoxList);

// Debug Logs
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->debug("[_objectFiltering] Last of Human Bounding Box: {}", (int)m_f_humanBBoxList.size());
    m_logger->debug("[_objectFiltering] Last of Rider Bounding Box: {}", (int)m_f_riderBBoxList.size());
    m_logger->debug("[_objectFiltering] Last of Vehicle Bounding Box: {}", (int)m_f_vehicleBBoxList.size());
#endif
    logInfo("Last of Human Bounding Box: " + to_string(m_f_humanBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Last of Rider Bounding Box: " + to_string(m_f_riderBBoxList.size()), __FUNCTION__, LOG_DEBUG);
    logInfo("Last of Vehicle Bounding Box: " + to_string(m_f_vehicleBBoxList.size()), __FUNCTION__, LOG_DEBUG);

    return true;
}

bool WNC_ADAS::_objectTracking()
{
    // Update FCW Data Buffer for Calculating TTC Before Tracking Objects
    m_fcw->updateDataBuffer(m_img, *m_roiBBox);

    // Run Object Tracking
    m_humanTracker->run(m_img, m_f_humanBBoxList, m_yVanish, m_roi);
    m_riderTracker->run(m_img, m_f_riderBBoxList, m_yVanish, m_roi);
    m_vehicleTracker->run(m_img, m_f_vehicleBBoxList, m_yVanish, m_roi);

    // Get Tracked Objects
    m_humanTracker->getObjectList(m_humanObjList);
    m_riderTracker->getObjectList(m_riderObjList);
    m_vehicleTracker->getObjectList(m_vehicleObjList);

    // Merge Tracked Objects
    m_trackedObjList.clear();
    m_trackedObjList.insert(m_trackedObjList.end(), m_humanObjList.begin(), m_humanObjList.end());
    m_trackedObjList.insert(m_trackedObjList.end(), m_riderObjList.begin(), m_riderObjList.end());
    m_trackedObjList.insert(m_trackedObjList.end(), m_vehicleObjList.begin(), m_vehicleObjList.end());

// Debug Logs
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->debug("[_objectTracking] Track: {} Human, {} Rider, {} Vehicle", (int)m_humanObjList.size(),
                    (int)m_riderObjList.size(), (int)m_vehicleObjList.size());
#endif
    logInfo("Track: " + to_string(m_humanObjList.size()) + " Human, " + to_string(m_riderObjList.size()) + " Rider, "
                + to_string(m_vehicleObjList.size()) + "Vehicle",
            __FUNCTION__, LOG_DEBUG);

    return ADAS_SUCCESS;
}

bool WNC_ADAS::_forwardCollisionDetection()
{
    m_isForwardCollision = m_fcw->run(m_img, m_trackedObjList, m_yVanish);

    return ADAS_SUCCESS;
}

// ============================================
//                  Outputs
// ============================================
void WNC_ADAS::getResults(wnc_ADAS_Results& res)
{
    utils::rescaleLine(m_fcw->m_vehicleZone, m_rescaleVehicleZone, m_config->modelWidth, m_config->modelHeight,
                       m_config->frameWidth, m_config->frameHeight);

    m_result.isDetectLine = m_isDetectLine;

    if (m_rescaleVehicleZone.pLeftFar.y != 0 && m_rescaleVehicleZone.pRightFar.y != 0)
    {
        m_result.pLeftFar      = m_rescaleVehicleZone.pLeftFar;
        m_result.pLeftCarhood  = m_rescaleVehicleZone.pLeftCarhood;
        m_result.pRightFar     = m_rescaleVehicleZone.pRightFar;
        m_result.pRightCarhood = m_rescaleVehicleZone.pRightCarhood;
    }

    // Save Vanishing Line's Y
    m_result.yVanish = m_yVanish;

    // Save Event Results
    m_result.eventType = getDetectEvents();

    // Save Tracked Objects
    m_result.objList = m_trackedObjList;

    // Pass reference to res
    res = m_result;
}

int WNC_ADAS::getDetectEvents()
{
    if (m_isLaneDeparture & m_isForwardCollision)
    {
        return ADAS_EVENT_LDW_FCW;
    }
    else if (m_isLaneDeparture)
    {
        int ldw_state = m_ldw->getLDWState();
        if (ldw_state == LDW_WARNING_LEFT)
            return ADAS_EVENT_LDW_LEFT;
        else if (ldw_state == LDW_WARNING_RIGHT)
            return ADAS_EVENT_LDW_RIGHT;
    }
    else if (m_isForwardCollision)
    {
        return ADAS_EVENT_FCW;
    }
    else
    {
        return ADAS_EVENT_NORMAL;
    }
}

float WNC_ADAS::getFollowingDistance()
{
    // TODO: need to improve
    int   maxArea        = 0;
    float followDistance = -1;

    for (int i = 0; i < m_vehicleObjList.size(); i++)
    {
        Object& obj = m_vehicleObjList[i];
        if (obj.bbox.getArea() > maxArea)
        {
            maxArea        = obj.bbox.getArea();
            followDistance = obj.distanceToCamera;
        }
    }

    return followDistance;
}

void WNC_ADAS::getResultImage(cv::Mat& imgResult)
{
    if (m_dsp_results)
        imgResult = m_dsp_imgResize;
}

// ============================================
//                  Results
// ============================================

void WNC_ADAS::_showDetectionResults()
{
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    // Lane Lines Information
    m_logger->debug("[_showDetectionResults] Detect Lane Lines = {:}", m_currLaneInfo.numCarhood);
    m_logger->debug("[_showDetectionResults] Left Line Degree = {:.2f}", m_currLaneInfo.leftDegree);
    m_logger->debug("[_showDetectionResults] Right Line Degree = {:.2f}", m_currLaneInfo.rightDegree);
    m_logger->debug("[_showDetectionResults] Vanishing Line y = {:}", m_yVanish);

    // Lane Departure Warning
    m_logger->debug("[_showDetectionResults] Detect LDW Event = {}", m_isLaneDeparture);

    // Forward Collision Warning
    m_logger->debug("[_showDetectionResults] Detect FCW Event = {}", m_isForwardCollision);
#endif

    // Lane Lines Information
    logInfo("Detect Lane Lines = " + to_string(m_currLaneInfo.numCarhood), __FUNCTION__, LOG_DEBUG);
    logInfo("Left Line Degree = " + to_string(m_currLaneInfo.leftDegree), __FUNCTION__, LOG_DEBUG);
    logInfo("Right Line Degree = " + to_string(m_currLaneInfo.rightDegree), __FUNCTION__, LOG_DEBUG);
    logInfo("Vanishing Line y = " + to_string(m_yVanish), __FUNCTION__, LOG_DEBUG);
    logInfo("Detect LDW Event = " + to_string(m_isLaneDeparture), __FUNCTION__, LOG_DEBUG);
    logInfo("Detect FCW Event = " + to_string(m_isForwardCollision), __FUNCTION__, LOG_DEBUG);

    // Show Tracked Object Information
    for (int i = 0; i < m_trackedObjList.size(); i++)
    {
        const Object& obj = m_trackedObjList[i];

        if (obj.status == 1)
        {
            string classType = "";
            if (obj.bbox.label == HUMAN)
                classType = "Pedestrian";
            else if (obj.bbox.label == SMALL_VEHICLE)
                classType = "Rider";
            else if (obj.bbox.label == BIG_VEHICLE)
                classType = "Vehicle";

            float ttc = obj.currTTC;

            if (!obj.needWarn)
                ttc = NAN;
#ifdef QCS6490
            m_logger->debug(
                "[_showDetectionResults] Tracking Obj[{}]: Cls = {}, Conf = {:.2f},"
                "Dist = {:.2f}, TTC = {:.2f}, needWarn = {}",
                obj.id, classType, obj.bbox.confidence, obj.distanceToCamera, ttc, obj.needWarn);
#endif
            logInfo("Tracking Obj[" + to_string(obj.id) + "]: Cls = " + classType + ", Conf = "
                        + to_string(obj.bbox.confidence) + ", Dist = " + to_string(obj.distanceToCamera) + ", TTC = "
                        + to_string(ttc) + ", needWarn = " + to_string(obj.needWarn),
                    __FUNCTION__, LOG_DEBUG);
        }
    }
}

void WNC_ADAS::_saveDetectionResult(std::vector<std::string>& logs)
{
    auto m_logger = spdlog::get("ADAS_DEBUG");

    for (int i = 0; i < logs.size(); i++)
    {
        m_logger->info(logs[i]);
    }
}

void WNC_ADAS::_saveDetectionResults()
{
    auto m_logger = spdlog::get("ADAS_DEBUG");

    m_logger->info("");
    m_logger->info("=================================");
    m_logger->info("Frame Index:{}", m_frameIdx_wnc);
    m_logger->info("=================================");

    // --- Lane and Line Mask ---  //
    m_loggerManager.m_laneMaskLogger->logResult(m_laneLineInfo.laneMaskInfo);
    m_loggerManager.m_lineMaskLogger->logResult(m_laneLineInfo.lineMaskInfo);

    // --- Ego Direction --- //
    m_loggerManager.m_egoDirectionLogger->logDirectionInfo(m_egoDirectionInfo);

    m_logger->info("");
    m_logger->info("Ego Direction");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_egoDirectionLogger->m_logs);

    // --- Lane Line Detection --- //
    m_loggerManager.m_lineDetetionLogger->logReulst(m_currLaneInfo);
    m_logger->info("");
    m_logger->info("Line Detection");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_lineDetetionLogger->m_logs);

    // --- Object Detection --- //
    std::vector<BoundingBox> bboxList;

    bboxList.insert(bboxList.end(), m_humanBBoxList.begin(), m_humanBBoxList.end());
    bboxList.insert(bboxList.end(), m_riderBBoxList.begin(), m_riderBBoxList.end());
    bboxList.insert(bboxList.end(), m_vehicleBBoxList.begin(), m_vehicleBBoxList.end());
    bboxList.insert(bboxList.end(), m_roadSignBBoxList.begin(), m_roadSignBBoxList.end());
    bboxList.insert(bboxList.end(), m_stopSignBBoxList.begin(), m_stopSignBBoxList.end());

    m_loggerManager.m_objectDetectionLogger->logObjects(bboxList);
    m_logger->info("");
    m_logger->info("Object Detection");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_objectDetectionLogger->m_logs);

    // --- Object Tracking --- //
    m_loggerManager.m_objectTrackingLogger->logObjects(m_trackedObjList);

    m_logger->info("");
    m_logger->info("Object Tracking");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_objectTrackingLogger->m_logs);

    // --- Lane Departure --- //
    LaneDepartureInfo ldwInfo;
    m_ldw->getLDWInfo(ldwInfo);
    m_loggerManager.m_laneDepartureWarningLogger->logResult(ldwInfo);

    m_logger->info("");
    m_logger->info("Lane Departure Warning");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_laneDepartureWarningLogger->m_logs);

    // --- Forward Collision --- //
    ForwardCollisionInfo fcwInfo;
    m_fcw->getFCWInfo(fcwInfo);
    m_loggerManager.m_forwardCollisionWarningLogger->logResult(fcwInfo);

    m_logger->info("");
    m_logger->info("Forward Collision Warning");
    m_logger->info("---------------------------------");
    _saveDetectionResult(m_loggerManager.m_forwardCollisionWarningLogger->m_logs);
}

void WNC_ADAS::_saveDrawResults()
{
    string imgName = "frame_" + std::to_string(m_frameIdx_wnc) + ".jpg";
    string imgPath = m_dbg_imgsDirPath + "/" + imgName;

    cv::imwrite(imgPath, m_dsp_imgResize);
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->debug("Save img to {}", imgPath);
#endif

    logInfo("Save img to " + imgPath, __func__, LOG_DEBUG);
}

void WNC_ADAS::_saveRawImages()
{
    string imgName = "frame_" + std::to_string(m_frameIdx_wnc) + ".jpg";
    string imgPath = m_dbg_rawImgsDirPath + "/" + imgName;

    cv::imwrite(imgPath, m_img);
#ifdef QCS6490
    auto m_logger = spdlog::get("ADAS");
    m_logger->debug("Save raw img to {}", imgPath);
#endif

    logInfo("Save raw img to " + imgPath, __func__, LOG_DEBUG);
}

// ============================================
//               Draw Results
// ============================================

void WNC_ADAS::_drawLDWROI()
{
    float scale_ratio = m_dsp_img.cols / m_maskWidth;
    scale_ratio       = m_dsp_img.cols / m_segWidth;
    int middle_x      = (int)(m_laneLineInfo.laneMaskInfo.xCenterAdjust * scale_ratio);
    cv::line(m_dsp_img, cv::Point(middle_x, 0), cv::Point(middle_x, m_dsp_img.rows - 1), cv::Scalar(255, 255, 255), 2);
}

void WNC_ADAS::_drawLaneLineBoundingBoxes()
{
    std::vector<BoundingBox> boundingBoxLists[] = {m_laneLineBox.vlaBBoxList, m_laneLineBox.vpaBBoxList,
                                                   m_laneLineBox.dlaBBoxList, m_laneLineBox.dmaBBoxList,
                                                   m_laneLineBox.duaBBoxList, m_laneLineBox.dcaBBoxList};

    cv::Scalar colors[] = {
        cv::Scalar(204, 0, 102), // Purple for VLA
        cv::Scalar(0, 0, 204),   // Red for PCA
        cv::Scalar(204, 0, 0),   // DLA
        cv::Scalar(255, 128, 0), // DMA
        cv::Scalar(255, 255, 0), // DUA
        cv::Scalar(128, 255, 0)  // DCA
    };

    for (int j = 0; j < sizeof(boundingBoxLists) / sizeof(boundingBoxLists[0]); j++)
    {
        std::vector<BoundingBox>& boundingBoxList = boundingBoxLists[j];
        cv::Scalar                color           = colors[j];

        for (int i = 0; i < boundingBoxList.size(); i++)
        {
            BoundingBox lastBox = boundingBoxList[i];
            BoundingBox rescaleBox(-1, -1, -1, -1, -1);

            utils::rescaleBBox(lastBox, rescaleBox, m_config->modelWidth, m_config->modelHeight, m_config->frameWidth,
                               m_config->frameHeight);

            imgUtil::roundedRectangle(m_dsp_img, cv::Point(rescaleBox.x1, rescaleBox.y1),
                                      cv::Point(rescaleBox.x2, rescaleBox.y2), color, 2, 0, 10, false);
        }
    }
}

void WNC_ADAS::_drawBoundingBoxes()
{
    std::vector<BoundingBox> boundingBoxLists[] = {m_humanBBoxList, m_riderBBoxList, m_vehicleBBoxList,
                                                   m_roadSignBBoxList, m_stopSignBBoxList};

    cv::Scalar colors[] = {
        cv::Scalar(0, 255, 0),   // Green for vehicles
        cv::Scalar(255, 0, 255), // Magenta for riders
        cv::Scalar(0, 128, 255), // Orange for humans
        cv::Scalar(255, 255, 0), // Yellow for road signs
        cv::Scalar(196, 62, 255) // Purple for stop signs
    };

    for (int j = 0; j < sizeof(boundingBoxLists) / sizeof(boundingBoxLists[0]); j++)
    {
        std::vector<BoundingBox>& boundingBoxList = boundingBoxLists[j];
        cv::Scalar                color           = colors[j];

        for (int i = 0; i < boundingBoxList.size(); i++)
        {
            BoundingBox lastBox = boundingBoxList[i];
            BoundingBox rescaleBox(-1, -1, -1, -1, -1);

            utils::rescaleBBox(lastBox, rescaleBox, m_config->modelWidth, m_config->modelHeight, m_config->frameWidth,
                               m_config->frameHeight);

            imgUtil::roundedRectangle(m_dsp_img, cv::Point(rescaleBox.x1, rescaleBox.y1),
                                      cv::Point(rescaleBox.x2, rescaleBox.y2), color, 2, 0, 10, false);
        }
    }
}

void WNC_ADAS::_drawTrackedObjects()
{
    // Drawing zones if dsp_warningZone is true
    if (m_dsp_warningZone)
    {
        ROI fcw_vehicle_roi;
        ROI fcw_rider_roi;
        ROI fcw_human_roi;

        utils::rescaleROI(m_fcw->m_vehicleROI, fcw_vehicle_roi, m_config->modelWidth, m_config->modelHeight,
                          m_videoWidth, m_videoHeight);

        imgUtil::roundedRectangle(m_dsp_img, cv::Point(fcw_vehicle_roi.x1, fcw_vehicle_roi.y1),
                                  cv::Point(fcw_vehicle_roi.x2, fcw_vehicle_roi.y2), cv::Scalar(255, 255, 255), 2, 0,
                                  10, false);

        utils::rescaleROI(m_fcw->m_riderROI, fcw_rider_roi, m_config->modelWidth, m_config->modelHeight, m_videoWidth,
                          m_videoHeight);

        imgUtil::roundedRectangle(m_dsp_img, cv::Point(fcw_rider_roi.x1, fcw_rider_roi.y1),
                                  cv::Point(fcw_rider_roi.x2, fcw_rider_roi.y2), cv::Scalar(0, 128, 255), 2, 0, 10,
                                  false);

        utils::rescaleROI(m_fcw->m_humanROI, fcw_human_roi, m_config->modelWidth, m_config->modelHeight, m_videoWidth,
                          m_videoHeight);

        imgUtil::roundedRectangle(m_dsp_img, cv::Point(fcw_human_roi.x1, fcw_human_roi.y1),
                                  cv::Point(fcw_human_roi.x2, fcw_human_roi.y2), cv::Scalar(0, 0, 255), 2, 0, 10,
                                  false);
    }

    for (int i = 0; i < m_trackedObjList.size(); i++)
    {
        const Object& trackedObj = m_trackedObjList[i];

        // Skip objects with specific conditions
        if (trackedObj.status == 0 || trackedObj.disappearCounter > 5 || trackedObj.bboxList.empty())
            continue;
        // if (trackedObj.bboxList.empty())
        // 	continue;

        BoundingBox lastBox = trackedObj.bboxList.back();
        BoundingBox rescaleBox(-1, -1, -1, -1, -1);

// Rescale BBoxes
#ifdef SAV837
        utils::rescaleBBox(lastBox, rescaleBox, m_config->modelWidth, m_config->modelHeight, m_config->frameWidth,
                           m_config->frameHeight);
#endif

#ifdef QCS6490
        utils::rescaleBBox(lastBox, rescaleBox, m_config->modelWidth, m_config->modelHeight, m_config->frameWidth,
                           m_config->frameHeight);
#endif

        if (trackedObj.aliveCounter < 3)
        {
            imgUtil::efficientRectangle(m_dsp_img, cv::Point(rescaleBox.x1, rescaleBox.y1),
                                        cv::Point(rescaleBox.x2, rescaleBox.y2), cv::Scalar(0, 250, 0), 2, 0, 10,
                                        false);
        }
        else
        {
            cv::Scalar color;
            if (lastBox.label == 0)                // Human
                color = cv::Scalar(255, 51, 153);  // Purple
            else if (lastBox.label == 1)           // Rider
                color = cv::Scalar(255, 51, 255);  // Pink
            else if (lastBox.label == 2)           // Vehicle
                color = cv::Scalar(255, 153, 153); // Purple Blue

            imgUtil::efficientRectangle(m_dsp_img, cv::Point(rescaleBox.x1, rescaleBox.y1),
                                        cv::Point(rescaleBox.x2, rescaleBox.y2), color, 2, 0, 10, false);
        }

        // Handle Distance Display
        if (m_dsp_followingDistance)
        {
            float distance = trackedObj.distanceToCamera;
            if (distance >= 0 && !std::isnan(distance)) // Show distance if it is positive
            {
                // Text box
                cv::rectangle(m_dsp_img, cv::Point(rescaleBox.x1 + 10, rescaleBox.y1 - 20),
                              cv::Point(rescaleBox.x1 + 40, rescaleBox.y1 - 3), (0, 0, 0), -1
                              /*fill*/);

                cv::putText(m_dsp_img, std::to_string(trackedObj.id) + ":" + std::to_string((int)distance) + "m",
                            cv::Point(int(rescaleBox.x1) + 10, int(rescaleBox.y1) - 10), cv::FONT_HERSHEY_DUPLEX, 0.3,
                            cv::Scalar(255, 255, 255), 1, 5, 0);
            }
        }

        if (m_dsp_forwardCollision)
        {
            float ttc = trackedObj.currTTC;

            // Handle TTC display
            std::string ttcString = utils::to_string_with_precision((float)ttc, 1);
            float       ttcTH     = m_config->stFcwConfig.ttc;

            if (trackedObj.ttcCounter > 0 && ttc < ttcTH)
            {
                // Draw bounding box in red
                imgUtil::efficientRectangle(m_dsp_img, cv::Point(rescaleBox.x1, rescaleBox.y1),
                                            cv::Point(rescaleBox.x2, rescaleBox.y2), cv::Scalar(0, 0, 255), 2, 0, 10,
                                            false);

                // Draw TTC information
                cv::rectangle(m_dsp_img, cv::Point(rescaleBox.x1 + 10, rescaleBox.y2 + 3),
                              cv::Point(rescaleBox.x1 + 88, rescaleBox.y2 + 35), (0, 0, 0), -1 /*fill*/);

                cv::putText(m_dsp_img, ttcString, cv::Point(int(rescaleBox.x1) + 20, int(rescaleBox.y2) + 28),
                            cv::FONT_HERSHEY_DUPLEX, 0.6, cv::Scalar(255, 255, 255), 1, 5, 0);
            }
        }
    }
}

#ifdef SAV837
// TODO: need to optimized
//[WNC fixed m_dsp_calibMask format to CV_8UC4 from CV_8UC3]
cv::Mat convertRGBToARGB(const cv::Mat& rgbImage)
{
    int height = rgbImage.rows;
    int width  = rgbImage.cols;

    cv::Mat argbImage = cv::Mat::zeros(height, width, CV_8UC4);

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            cv::Vec3b  rgbPixel  = rgbImage.at<cv::Vec3b>(y, x);
            cv::Vec4b& argbPixel = argbImage.at<cv::Vec4b>(y, x);

            argbPixel[0] = rgbPixel[2]; // R channel
            argbPixel[1] = rgbPixel[1]; // G channel
            argbPixel[2] = rgbPixel[0]; // B channel
            argbPixel[3] = 255;         // Alpha channel
        }
    }

    return argbImage;
}
#endif

void WNC_ADAS::_drawLaneLines()
{
#ifdef SAV837
    utils::rescaleLine(m_fcw->m_vehicleZone, m_rescaleVehicleZone, m_config->modelWidth, m_config->modelHeight,
                       m_config->frameWidth, m_config->frameHeight);
#endif

#ifdef QCS6490
    utils::rescaleLine(m_fcw->m_vehicleZone, m_rescaleVehicleZone, m_config->modelWidth, m_config->modelHeight,
                       m_config->frameWidth, m_config->frameHeight);
#endif

#ifdef QCS6490
    m_dsp_laneLineResult =
        cv::Mat::zeros(m_dsp_img.rows, m_dsp_img.cols, CV_8UC3); // QD: Change to m_dsp_img for avoiding manual change
#endif

#ifdef SAV837
    m_dsp_laneLineResult =
        cv::Mat::zeros(m_dsp_img.rows, m_dsp_img.cols, CV_8UC4); // QD: Change to m_dsp_img for avoiding manual change
#endif

    // cout << "m_videoROIWidth = " << m_videoROIWidth << endl;
    // cout << "m_videoROIHeight = " << m_videoROIHeight << endl;
    // cout << "m_rescaleVehicleZone.pLeftFar.x = " << m_rescaleVehicleZone.pLeftFar.x << endl;
    // cout << "m_rescaleVehicleZone.pLeftFar.y = " << m_rescaleVehicleZone.pLeftFar.y << endl;
    // cout << "m_rescaleVehicleZone.pLeftCarhood.x = " << m_rescaleVehicleZone.pLeftCarhood.x << endl;
    // cout << "m_rescaleVehicleZone.pLeftCarhood.y = " << m_rescaleVehicleZone.pLeftCarhood.y << endl;
    // cout << "m_rescaleVehicleZone.pRightFar.x = " << m_rescaleVehicleZone.pRightFar.x << endl;
    // cout << "m_rescaleVehicleZone.pRightFar.y = " << m_rescaleVehicleZone.pRightFar.y << endl;
    // cout << "m_rescaleVehicleZone.pRightCarhood.x = " << m_rescaleVehicleZone.pRightCarhood.x << endl;
    // cout << "m_rescaleVehicleZone.pRightCarhood.y = " << m_rescaleVehicleZone.pRightCarhood.y << endl;

    if (m_rescaleVehicleZone.pLeftFar.y != 0 && m_rescaleVehicleZone.pRightFar.y != 0)
    {
        std::vector<cv::Point> fillContSingle;
        fillContSingle.push_back(m_rescaleVehicleZone.pLeftFar);
        fillContSingle.push_back(m_rescaleVehicleZone.pRightFar);
        fillContSingle.push_back(m_rescaleVehicleZone.pRightCarhood);
        fillContSingle.push_back(m_rescaleVehicleZone.pLeftCarhood);

        if (m_dsp_leftLDWCounter > 0 || m_dsp_rightLDWCounter > 0)
            cv::fillPoly(m_dsp_laneLineResult, std::vector<std::vector<cv::Point>>{fillContSingle},
                         cv::Scalar(0, 0, 255));
        else
            cv::fillPoly(m_dsp_laneLineResult, std::vector<std::vector<cv::Point>>{fillContSingle},
                         cv::Scalar(255, 0, 0));

        cv::addWeighted(m_dsp_img, 1.0, m_dsp_laneLineResult, 0.7, 0, m_dsp_img);

        cv::line(m_dsp_img, m_rescaleVehicleZone.pLeftFar, m_rescaleVehicleZone.pLeftCarhood, cv::Scalar(255, 255, 255),
                 2, cv::LINE_AA);
        cv::line(m_dsp_img, m_rescaleVehicleZone.pRightFar, m_rescaleVehicleZone.pRightCarhood,
                 cv::Scalar(255, 255, 255), 2, cv::LINE_AA);

        // if (m_dsp_warningZone)
        // {
        // 	cv::line(m_dsp_img, m_rescaleRiderZone.pLeftFar, m_rescaleRiderZone.pLeftCarhood, cv::Scalar(0, 128, 255),
        // 2, cv::LINE_AA);
        // 	cv::line(m_dsp_img, m_rescaleRiderZone.pRightFar, m_rescaleRiderZone.pRightCarhood, cv::Scalar(0,128, 255),
        // 2, cv::LINE_AA);

        // 	cv::line(m_dsp_img, m_rescaleHumanZone.pLeftFar, m_rescaleHumanZone.pLeftCarhood, cv::Scalar(0, 0, 255), 2,
        // cv::LINE_AA);
        // 	cv::line(m_dsp_img, m_rescaleHumanZone.pRightFar, m_rescaleHumanZone.pRightCarhood, cv::Scalar(0, 0, 255),
        // 2, cv::LINE_AA);
        // }

        if (m_isGetLaneBoundary && m_dsp_laneBoundary)
        {
            cv::line(m_dsp_img, m_laneBoundaryInfo.pLeftFar, m_laneBoundaryInfo.pLeftCarhood, cv::Scalar(0, 255, 0), 2,
                     cv::LINE_AA);
            cv::line(m_dsp_img, m_laneBoundaryInfo.pRightFar, m_laneBoundaryInfo.pRightCarhood, cv::Scalar(0, 255, 0),
                     2, cv::LINE_AA);
        }

        // Driving Direction Vector
        if (m_isDetectLine && m_laneLineInfo.drivingDirectionVector.size() > 0)
        {
            for (int i = 0; i < m_laneLineInfo.drivingDirectionVector.size(); i++)
            {
                utils::rescalePoint(m_laneLineInfo.drivingDirectionVector[i], m_laneLineInfo.drivingDirectionVector[i],
                                    m_config->modelWidth, m_config->modelHeight, m_videoWidth, m_videoHeight);
            }

            // TODO:
            if ((m_laneLineInfo.drivingDirectionVector[0].x != 0) && (m_laneLineInfo.drivingDirectionVector[1].x != 0))
            {
                cv::arrowedLine(m_dsp_img, m_laneLineInfo.drivingDirectionVector[0],
                                m_laneLineInfo.drivingDirectionVector[1], cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            }
        }
    }

    if (m_dsp_vanishingLine)
        cv::line(m_dsp_img, cv::Point(0, m_yVanish), cv::Point(m_videoWidth, m_yVanish), cv::Scalar(255, 255, 255), 2);
}

void WNC_ADAS::_drawInformation()
{
#ifdef SAV837
    int m_frameIdx = m_frameIdx_wnc;
#endif

    cv::putText(m_dsp_imgResize, "Frame: " + std::to_string(m_frameIdx), cv::Point(10, 600),
                cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, 2, 0);

    cv::putText(m_dsp_imgResize, "Vanishing Line: " + std::to_string(m_yVanish), cv::Point(10, 620),
                cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, 2, 0);

    cv::putText(m_dsp_imgResize, "Direction Degree: " + std::to_string(m_laneLineInfo.drivingDirectionDegree),
                cv::Point(10, 640), cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, 2, 0);

    cv::putText(m_dsp_imgResize, "Left Line Angle: " + std::to_string(m_currLaneInfo.leftDegree), cv::Point(10, 660),
                cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, 2, 0);

    cv::putText(m_dsp_imgResize, "Right Line Angle: " + std::to_string(m_currLaneInfo.rightDegree), cv::Point(10, 680),
                cv::FONT_HERSHEY_COMPLEX_SMALL, 0.8, cv::Scalar(0, 255, 0), 1, 2, 0);

    if (m_dsp_laneDeparture)
    {
        int ldw_state = m_ldw->getLDWState();
        if (ldw_state == LDW_WARNING_LEFT)
            m_dsp_leftLDWCounter = 5;
        if (ldw_state == LDW_WARNING_RIGHT)
            m_dsp_rightLDWCounter = 5;

        if (m_dsp_leftLDWCounter > 0)
            cv::putText(m_dsp_imgResize, "Left LDW!!!", cv::Point(100, 300), cv::FONT_HERSHEY_COMPLEX_SMALL, 2,
                        cv::Scalar(0, 0, 255), 2, 5, 0);
        if (m_dsp_rightLDWCounter > 0)
            cv::putText(m_dsp_imgResize, "Right LDW!!!", cv::Point(800, 300), cv::FONT_HERSHEY_COMPLEX_SMALL, 2,
                        cv::Scalar(0, 0, 255), 2, 5, 0);

        if (m_dsp_leftLDWCounter > 0)
            m_dsp_leftLDWCounter--;
        if (m_dsp_rightLDWCounter > 0)
            m_dsp_rightLDWCounter--;
    }

    if (m_isForwardCollision && m_dsp_forwardCollision)
    {
        cv::putText(m_dsp_imgResize, "Forward Collision Warning", cv::Point(300, 100), cv::FONT_HERSHEY_COMPLEX_SMALL,
                    2, cv::Scalar(0, 0, 255), 2, 5, 0);
    }
}

void WNC_ADAS::_drawResults()
{
    int waitKey = 1;
    if (m_dsp_objectDetection)
    {
        _drawBoundingBoxes();
    }

    if (m_dsp_objectTracking)
        _drawTrackedObjects();

    if (m_dsp_laneLineDetection)
    {
        _drawLaneLines();
    }

    // TODO: turn it by setting header file only
    // TODO: develop purpose only don't want others know how we detect lane lines
    if (m_dsp_laneLineBoxes)
    {
        _drawLaneLineBoundingBoxes();
    }

    cv::resize(m_dsp_img, m_dsp_imgResize, cv::Size(1280, 720), cv::INTER_LINEAR); // Width must be mutiplication of 4
#ifdef SAV837
    cv::cvtColor(m_dsp_imgResize, m_dsp_imgResize, cv::COLOR_RGBA2RGB);
#endif

    if (m_dsp_information)
        _drawInformation();

// m_drawResultBuffer.back().matResult = m_dsp_imgResize.clone();
#ifndef SAV837
    if (m_dsp_results)
        cv::imshow("WNC ADAS", m_dsp_imgResize);
#endif

    if (m_frameIdx_wnc < m_dsp_maxFrameIdx)
    {
        waitKey = 1;
    }
    else if (m_dsp_maxFrameIdx == 0)
    {
        waitKey = 1;
    }
    else if (m_isLaneDeparture && m_dbg_laneDeparture)
    {
        waitKey = 0;
    }
    else
    {
        waitKey = 0;
    }
#ifndef SAV837
    if (m_dsp_results)
        cv::waitKey(1);
#endif
}

void WNC_ADAS::_showADASResult()
{
    if (m_isDrawImage && m_drawResultBuffer.size() > 0)
    {
        int waitKey = 1;

        cv::Mat result = m_drawResultBuffer.back().matResult;

#ifndef SAV837
        if (m_dsp_results)
            cv::imshow("WNC ADAS", m_dsp_imgResize);
#endif

        if (m_frameIdx_wnc < m_dsp_maxFrameIdx)
        {
            waitKey = 1;
        }
        else if (m_dsp_maxFrameIdx == 0)
        {
            waitKey = 1;
        }
        else if (m_isLaneDeparture && m_dbg_laneDeparture)
        {
            waitKey = 0;
        }
        else
        {
            waitKey = 0;
        }
#ifndef SAV837
        cv::waitKey(1);
#endif
        m_drawResultBuffer.pop_front();
    }
}

// ============================================
//             Thread Management
// ============================================

bool WNC_ADAS::_runShowLogsFunc()
{
    // while ((!m_threadTerminated))
    // {
    // 	// STEP0: sleep for a while for reducing CPU loading
    // 	std::this_thread::sleep_for(std::chrono::microseconds(100));

    //   // STEP1: Start inference loop
    //   std::lock_guard<std::mutex> lock(m_mutex);

    //   if (!m_threadStarted)
    //   {
    // 	  continue;
    // 	}

    // 	if (m_inputFrameBuffer.size() > 0)
    // 	{
    // 		run(m_inputFrameBuffer.front()); //TODO:
    // 	}
    // }

    return true;
}

bool WNC_ADAS::_runDrawResultFunc()
{
    while ((!m_threadTerminated))
    {
        // STEP0: sleep for a while for reducing CPU loading
        std::this_thread::sleep_for(std::chrono::microseconds(85000)); // TODO: have to find the best value

        // STEP1: Start inference loop
        std::lock_guard<std::mutex> lock(m_mutex);

        // if (!m_threadStarted)
        // {
        //   continue;
        // }

        if (m_drawResultBuffer.size() > 0)
        {
            // Draw and Save Results
            if (m_dsp_results)
            {
                m_drawResultBuffer.back().isDraw = true;
                _drawResults();
                _showADASResult();
            }

            if (m_dsp_results && m_dbg_saveImages)
            {
                _saveDrawResults();
            }

            // Save Raw Images
            if (m_dbg_saveRawImages)
            {
                _saveRawImages();
            }
        }

        m_condition.notify_one();
    }

    return true;
}

// ============================================
//                    Utils
// ============================================
void WNC_ADAS::_updateFrameIndex()
{
    m_frameIdx_wnc = (m_frameIdx_wnc % 65535) + 1;
}

void WNC_ADAS::_updateDirectionVector(LaneInfo& laneInfo)
{
    int       xCenter = (int)((laneInfo.pLeftFar.x + laneInfo.pRightFar.x) * 0.5);
    int       yCenter = laneInfo.pLeftFar.y;
    cv::Point pStart(xCenter, yCenter);

    if (m_laneLineInfo.drivingDirectionVector.size() > 0)
    {
        vector<cv::Point> dirVec(2);
        utils::rescalePoint(m_laneLineInfo.drivingDirectionVector[0], dirVec[0], m_config->modelWidth,
                            m_config->modelHeight, m_videoWidth, m_videoHeight);

        utils::rescalePoint(pStart, dirVec[1], m_config->modelWidth, m_config->modelHeight, m_videoWidth,
                            m_videoHeight);

        m_laneLineInfo.drivingDirectionVector[1] = pStart;
        m_fcw->setLaneDirectionVector(dirVec[1], dirVec[0]);
    }
}

void WNC_ADAS::logInfo(const std::string& message, const std::string& func, int level)
{
    openlog("[ADAS]", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_USER);

    if (m_dbg_adas)
        setlogmask(LOG_UPTO(LOG_DEBUG));
    else
        setlogmask(LOG_UPTO(LOG_INFO));

    if (func == "")
        syslog(level, "%s", message.c_str());
    else
        syslog(level, "[%s] %s", func.c_str(), message.c_str());
    closelog();
}
