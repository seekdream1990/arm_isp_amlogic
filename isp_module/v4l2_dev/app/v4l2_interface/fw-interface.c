/*
*
* SPDX-License-Identifier: GPL-2.0
*
* Copyright (C) 2011-2018 ARM or its affiliates
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; version 2.
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*/

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <asm/div64.h>
#include <linux/sched.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "system_interrupts.h"
#include "acamera_command_api.h"
#include "acamera_firmware_settings.h"
#include "application_command_api.h"
#include "acamera_logger.h"
#include "isp-v4l2-common.h"
#include "fw-interface.h"

//use main_firmware.c routines to initialize fw
extern int isp_fw_init( void );
extern void isp_fw_exit( void );

static int isp_started = 0;


/* ----------------------------------------------------------------
 * fw_interface control interface
 */

void custom_initialization( uint32_t ctx_num )
{
}


/* fw-interface isp control interface */
int fw_intf_isp_init( void )
{

    LOG( LOG_INFO, "Initializing fw ..." );
    int rc = 0;
    /* flag variable update */

    rc = isp_fw_init();

    if ( rc == 0 )
        isp_started = 1;

    return rc;
}

int fw_intf_is_isp_started( void )
{
    return isp_started;
}

void fw_intf_isp_deinit( void )
{
    LOG( LOG_INFO, "Deinitializing fw interface ..." );

    /* flag variable update */
    isp_started = 0;

    isp_fw_exit();
}

uint32_t fw_calibration_update( void )
{
    uint32_t retval;
    acamera_command( TSYSTEM, CALIBRATION_UPDATE, UPDATE, COMMAND_SET, &retval );
    return retval;
}

int fw_intf_isp_start( void )
{
    LOG( LOG_DEBUG, "Starting context" );


    return 0;
}

void fw_intf_isp_stop( void )
{
    LOG( LOG_DEBUG, "Enter" );
}

int fw_intf_isp_get_current_ctx_id( void )
{
    int ctx_id = -1;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

#if defined( TGENERAL ) && defined( ACTIVE_CONTEXT )
    acamera_command( TGENERAL, ACTIVE_CONTEXT, 0, COMMAND_GET, &ctx_id );
#endif

    return ctx_id;
}

int fw_intf_isp_get_sensor_info( isp_v4l2_sensor_info *sensor_info )
{
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

#if defined( TSENSOR ) && defined( SENSOR_SUPPORTED_PRESETS ) && defined( SENSOR_INFO_PRESET ) && \
    defined( SENSOR_INFO_FPS ) && defined( SENSOR_INFO_WIDTH ) && defined( SENSOR_INFO_HEIGHT )
    LOG( LOG_INFO, "API found, initializing sensor_info from API." );
    int i, j;
    uint32_t ret_val;
    uint32_t preset_num;

    /* reset buffer */
    memset( sensor_info, 0x0, sizeof( isp_v4l2_sensor_info ) );

    /* get preset size */
    acamera_command( TSENSOR, SENSOR_SUPPORTED_PRESETS, 0, COMMAND_GET, &preset_num );
    if ( preset_num > MAX_SENSOR_PRESET_SIZE ) {
        LOG( LOG_ERR, "MAX_SENSOR_PRESET_SIZE is too small ! (preset_num = %d)", preset_num );
        preset_num = MAX_SENSOR_PRESET_SIZE;
    }

    /* fill preset values */
    for ( i = 0; i < preset_num; i++ ) {
        uint32_t width, height, fps, exposures = 1;

        /* get next preset */
        acamera_command( TSENSOR, SENSOR_INFO_PRESET, i, COMMAND_SET, &ret_val );
        acamera_command( TSENSOR, SENSOR_INFO_FPS, i, COMMAND_GET, &fps );
        acamera_command( TSENSOR, SENSOR_INFO_WIDTH, i, COMMAND_GET, &width );
        acamera_command( TSENSOR, SENSOR_INFO_HEIGHT, i, COMMAND_GET, &height );
#if defined( SENSOR_INFO_EXPOSURES )
        acamera_command( TSENSOR, SENSOR_INFO_EXPOSURES, i, COMMAND_GET, &exposures );
#endif

        /* find proper index from sensor_info */
        for ( j = 0; j < sensor_info->preset_num; j++ ) {
            if ( sensor_info->preset[j].width == width &&
                 sensor_info->preset[j].height == height )
                break;
        }

        /* store preset */
        if ( sensor_info->preset[j].fps_num < MAX_SENSOR_FPS_SIZE ) {
            sensor_info->preset[j].width = width;
            sensor_info->preset[j].height = height;
            sensor_info->preset[j].exposures[sensor_info->preset[j].fps_num] = exposures;
            sensor_info->preset[j].fps[sensor_info->preset[j].fps_num] = fps;
            sensor_info->preset[j].idx[sensor_info->preset[j].fps_num] = i;
            sensor_info->preset[j].fps_num++;
            if ( sensor_info->preset_num <= j )
                sensor_info->preset_num++;
        } else {
            LOG( LOG_ERR, "FPS number overflowed ! (preset#%d / index#%d)", i, j );
        }
    }
    uint32_t spreset, swidth, sheight;
    acamera_command( TSENSOR, SENSOR_PRESET, 0, COMMAND_GET, &spreset );
    acamera_command( TSENSOR, SENSOR_WIDTH, 0, COMMAND_GET, &swidth );
    acamera_command( TSENSOR, SENSOR_HEIGHT, 0, COMMAND_GET, &sheight );

    for ( i = 0; i < sensor_info->preset_num; i++ ) {
        if ( swidth == sensor_info->preset[i].width && sheight == sensor_info->preset[i].height ) {
            sensor_info->preset_cur = i;
            break;
        }
    }

    if ( i < MAX_SENSOR_PRESET_SIZE ) {
        for ( j = 0; j < sensor_info->preset[i].fps_num; j++ ) {
            if ( sensor_info->preset[i].idx[j] == spreset ) {
                sensor_info->preset[i].fps_cur = spreset;
                break;
            }
        }
    }

    LOG( LOG_INFO, "Sensor info current preset:%d v4l2 preset:%d", sensor_info->preset[i].fps_cur, sensor_info->preset_cur );

    //check current sensor settings

    LOG( LOG_INFO, "/* dump sensor info -----------------" );
    for ( i = 0; i < sensor_info->preset_num; i++ ) {
        LOG( LOG_INFO, "   Idx#%02d - W:%04d H:%04d",
             i, sensor_info->preset[i].width, sensor_info->preset[i].height );
        for ( j = 0; j < sensor_info->preset[i].fps_num; j++ )
            LOG( LOG_INFO, "            FPS#%d: %d (preset index = %d) exposures:%d",
                 j, sensor_info->preset[i].fps[j] / 256, sensor_info->preset[i].idx[j], sensor_info->preset[i].exposures[j] );
    }

    LOG( LOG_INFO, "-----------------------------------*/" );

#else
    /* returning default setting (1080p) */
    LOG( LOG_ERR, "API not found, initializing sensor_info to default." );
    sensor_info->preset_num = 1;
    sensor_info->preset[0].fps = 30 * 256;
    sensor_info->preset[0].width = 1920;
    sensor_info->preset[0].height = 1080;
#endif

    return 0;
}

int fw_intf_isp_get_sensor_preset( void )
{
    int value = -1;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

#if defined( TSENSOR ) && defined( SENSOR_PRESET )
    acamera_command( TSENSOR, SENSOR_PRESET, 0, COMMAND_GET, &value );
#endif

    return value;
}

int fw_intf_isp_set_sensor_preset( uint32_t preset )
{
    int value = -1;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

#if defined( TSENSOR ) && defined( SENSOR_PRESET )
    LOG( LOG_INFO, "V4L2 setting sensor preset to %d\n", preset );
    acamera_command( TSENSOR, SENSOR_PRESET, preset, COMMAND_SET, &value );
#endif

    return value;
}

/*
 * fw-interface per-stream control interface
 */
int fw_intf_stream_start( isp_v4l2_stream_type_t streamType )
{
    LOG( LOG_DEBUG, "Starting stream type %d", streamType );

#if ISP_DMA_RAW_CAPTURE && ISP_HAS_RAW_CB
    if ( streamType == V4L2_STREAM_TYPE_RAW ) {
#if defined( TFPGA ) && defined( DMA_RAW_CAPTURE_ENABLED_ID )
        uint32_t ret_val;
        acamera_command( TFPGA, DMA_RAW_CAPTURE_ENABLED_ID, ON, COMMAND_SET, &ret_val );
#else
        LOG( LOG_CRIT, "no api for DMA_RAW_CAPTURE_ENABLED_ID" );
#endif
    }
#endif

    return 0;
}

void fw_intf_stream_stop( isp_v4l2_stream_type_t streamType )
{
    LOG( LOG_DEBUG, "Stopping stream type %d", streamType );
#if ISP_DMA_RAW_CAPTURE && ISP_HAS_RAW_CB
    if ( streamType == V4L2_STREAM_TYPE_RAW ) {
#if defined( TFPGA ) && defined( DMA_RAW_CAPTURE_ENABLED_ID )
        uint32_t ret_val;
        acamera_command( TFPGA, DMA_RAW_CAPTURE_ENABLED_ID, OFF, COMMAND_SET, &ret_val );
#else
        LOG( LOG_CRIT, "no api for DMA_RAW_CAPTURE_ENABLED_ID" );
#endif
    }
#endif
}

void fw_intf_stream_pause( isp_v4l2_stream_type_t streamType, uint8_t bPause )
{
    LOG( LOG_DEBUG, "Pausing/Resuming stream type %d (Flag=%d", streamType, bPause );

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return;
    }

#if ISP_DMA_RAW_CAPTURE && ISP_HAS_RAW_CB
    if ( streamType == V4L2_STREAM_TYPE_RAW ) {
#if defined( TFPGA ) && defined( DMA_RAW_CAPTURE_ENABLED_ID )
        uint32_t ret_val;
        acamera_command( TFPGA, DMA_RAW_CAPTURE_WRITEON_ID, ( bPause ? 0 : 1 ), COMMAND_SET, &ret_val );
#else
        LOG( LOG_CRIT, "no api for DMA_RAW_CAPTURE_WRITEON_ID" );
#endif
    }
#endif

#if defined( TGENERAL ) && defined( DMA_WRITER_SINGLE_FRAME_MODE )
#if ISP_HAS_RAW_CB
    if ( streamType == V4L2_STREAM_TYPE_RAW ) {
        uint32_t ret_val;

        acamera_command( TGENERAL, DMA_WRITER_SINGLE_FRAME_MODE, bPause ? ON : OFF, COMMAND_SET, &ret_val );
    }
#endif
#endif
}

/* fw-interface sensor hw stream control interface */
int fw_intf_sensor_pause( void )
{
    uint32_t rc = 0;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    application_command( TSENSOR, SENSOR_STREAMING, OFF, COMMAND_SET, &rc );

    return rc;
}

int fw_intf_sensor_resume( void )
{
    uint32_t rc = 0;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    application_command( TSENSOR, SENSOR_STREAMING, ON, COMMAND_SET, &rc );

    return rc;
}


/* fw-interface per-stream config interface */
int fw_intf_stream_set_resolution( const isp_v4l2_sensor_info *sensor_info,
                                   isp_v4l2_stream_type_t streamType, uint32_t *width, uint32_t *height )
{
    /*
     * StreamType
     *   - FR : directly update sensor resolution since FR doesn't have down-scaler.
     *   - DS : need to be implemented.
     *   - RAW: can't be configured separately from FR.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    LOG( LOG_DEBUG, "streamtype:%d, w:%d h:%d\n", streamType, *width, *height );
    if ( streamType == V4L2_STREAM_TYPE_FR ) {
#if defined( TSENSOR ) && defined( SENSOR_PRESET )
        int result;
        uint32_t ret_val;
        uint32_t idx = 0x0;
        uint32_t fps = 0x0;
        uint32_t w, h;
        uint32_t i, j;


        w = *width;
        h = *height;

        uint32_t width_cur, height_cur;
        //check if we need to change sensor preset
        acamera_command( TSENSOR, SENSOR_WIDTH, 0, COMMAND_GET, &width_cur );
        acamera_command( TSENSOR, SENSOR_HEIGHT, 0, COMMAND_GET, &height_cur );
        LOG( LOG_INFO, "target (width = %d, height = %d) current (w=%d h=%d)", w, h, width_cur, height_cur );
        if ( width_cur != w || height_cur != h ) {
            /* search resolution from preset table
             *   for now, use the highest fps.
             *   this should be changed properly in the future to pick fps from application
             */
            for ( i = 0; i < sensor_info->preset_num; i++ ) {
                if ( sensor_info->preset[i].width == w && sensor_info->preset[i].height == h ) {
                    *( (char *)&sensor_info->preset[i].fps_cur ) = 0;
                    for ( j = 0; j < sensor_info->preset[i].fps_num; j++ ) {
                        if ( sensor_info->preset[i].fps[j] > fps ) {
                            fps = sensor_info->preset[i].fps[j];
                            idx = sensor_info->preset[i].idx[j];
                            *( (char *)&sensor_info->preset[i].fps_cur ) = j;
                        }
                    }

                    break;
                }
            }
            if ( i >= sensor_info->preset_num ) {
                LOG( LOG_CRIT, "invalid resolution (width = %d, height = %d) reverting to current %dx%d", w, h, width_cur, height_cur );
                *width = width_cur;
                *height = height_cur;
                return 0;
            }

            /* set sensor resolution preset */
            LOG( LOG_CRIT, "Setting new resolution : width = %d, height = %d (preset idx = %d, fps = %d)", w, h, idx, fps / 256 );
            result = acamera_command( TSENSOR, SENSOR_PRESET, idx, COMMAND_SET, &ret_val );
            *( (char *)&sensor_info->preset_cur ) = idx;
            if ( result ) {
                LOG( LOG_CRIT, "Failed to set preset to %u, ret_value: %d.", idx, result );
                return result;
            }
        } else {
            acamera_command( TSENSOR, SENSOR_PRESET, 0, COMMAND_GET, &idx );
            LOG( LOG_CRIT, "Leaving same sensor settings resolution : width = %d, height = %d (preset idx = %d)", w, h, idx );
        }
#endif
    }

    return 0;
}

int fw_intf_stream_set_output_format( isp_v4l2_stream_type_t streamType, uint32_t format )
{

#if defined( TIMAGE ) && defined( FR_FORMAT_BASE_PLANE_ID )
    uint32_t value;

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    switch ( format ) {
#ifdef DMA_FORMAT_RGB32
    case V4L2_PIX_FMT_RGB32:
        value = RGB32;
        break;
#endif
#if defined( DMA_FORMAT_NV12_Y )
    case V4L2_PIX_FMT_NV12:
        value = NV12_YUV;
        break;
#endif
#ifdef DMA_FORMAT_A2R10G10B10
    case ISP_V4L2_PIX_FMT_ARGB2101010:
        value = A2R10G10B10;
        break;
#endif
#ifdef DMA_FORMAT_RGB24
    case ISP_V4L2_PIX_FMT_RGB24:
        value = RGB24;
        break;
#endif

#ifdef DMA_FORMAT_RAW16
    case V4L2_PIX_FMT_SBGGR16:
        value = RAW16;
        break;
#endif
    case ISP_V4L2_PIX_FMT_META:
        LOG( LOG_INFO, "Meta format 0x%x doesn't need to be set to firmware", format );
        return 0;
        break;
    default:
        LOG( LOG_ERR, "Requested format 0x%x is not supported by firmware !", format );
        return -1;
        break;
    }

    if ( streamType == V4L2_STREAM_TYPE_FR ) {

        uint8_t result;
        uint32_t ret_val;

        result = acamera_command( TIMAGE, FR_FORMAT_BASE_PLANE_ID, value, COMMAND_SET, &ret_val );
        LOG( LOG_INFO, "set format for stream %d to %d (0x%x)", streamType, value, format );
        if ( result ) {
            LOG( LOG_ERR, "TIMAGE - FR_FORMAT_BASE_PLANE_ID failed (value = 0x%x, result = %d)", value, result );
        }
    }
#else
    LOG( LOG_ERR, "cannot find proper API for fr base mode ID" );
#endif


    return 0;
}


/* ----------------------------------------------------------------
 * Internal handler for control interface functions
 */
static bool isp_fw_do_validate_control( uint32_t id )
{
    return 1;
}

static int isp_fw_do_set_test_pattern( int enable )
{
#if defined( TSYSTEM ) && defined( TEST_PATTERN_ENABLE_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_CRIT, "test_pattern: %d.", enable );

    if ( enable < 0 )
        return -EIO;

    if ( !isp_started ) {
        LOG( LOG_CRIT, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = application_command( TSYSTEM, TEST_PATTERN_ENABLE_ID, enable ? ON : OFF, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_CRIT, "Failed to set TEST_PATTERN_ENABLE_ID to %u, ret_value: %d.", enable, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_test_pattern_type( int pattern_type )
{
#if defined( TSYSTEM ) && defined( TEST_PATTERN_MODE_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "test_pattern_type: %d.", pattern_type );

    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSYSTEM, TEST_PATTERN_MODE_ID, pattern_type, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set TEST_PATTERN_MODE_ID to %d, ret_value: %d.", pattern_type, result );
        return result;
    }
#endif

    return 0;
}


static int isp_fw_do_set_af_refocus( int val )
{
#if defined( TALGORITHMS ) && defined( AF_MODE_ID )
    int result;
    u32 ret_val;

    result = acamera_command( TALGORITHMS, AF_MODE_ID, AF_AUTO_SINGLE, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AF_MODE_ID to AF_AUTO_SINGLE, ret_value: %u.", ret_val );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_brightness( int brightness )
{
#if defined( TSCENE_MODES ) && defined( BRIGHTNESS_STRENGTH_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "brightness: %d.", brightness );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, BRIGHTNESS_STRENGTH_ID, brightness, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set BRIGHTNESS_STRENGTH_ID to %d, ret_value: %d.", brightness, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_contrast( int contrast )
{
#if defined( TSCENE_MODES ) && defined( CONTRAST_STRENGTH_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "contrast: %d.", contrast );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, CONTRAST_STRENGTH_ID, contrast, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set CONTRAST_STRENGTH_ID to %d, ret_value: %d.", contrast, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_saturation( int saturation )
{
#if defined( TSCENE_MODES ) && defined( SATURATION_STRENGTH_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "saturation: %d.", saturation );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, SATURATION_STRENGTH_ID, saturation, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set SATURATION_STRENGTH_ID to %d, ret_value: %d.", saturation, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_hue( int hue )
{
#if defined( TSCENE_MODES ) && defined( HUE_THETA_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "hue: %d.", hue );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, HUE_THETA_ID, hue, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set HUE_THETA_ID to %d, ret_value: %d.", hue, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_sharpness( int sharpness )
{
#if defined( TSCENE_MODES ) && defined( SHARPENING_STRENGTH_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "sharpness: %d.", sharpness );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, SHARPENING_STRENGTH_ID, sharpness, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set SHARPENING_STRENGTH_ID to %d, ret_value: %d.", sharpness, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_color_fx( int idx )
{
#if defined( TSCENE_MODES ) && defined( COLOR_MODE_ID )
    int result;
    uint32_t ret_val;
    int color_idx;

    LOG( LOG_INFO, "idx: %d.", idx );

    switch ( idx ) {
    case V4L2_COLORFX_NONE:
        color_idx = NORMAL;
        break;
    case V4L2_COLORFX_BW:
        color_idx = BLACK_AND_WHITE;
        break;
    case V4L2_COLORFX_SEPIA:
        color_idx = SEPIA;
        break;
    case V4L2_COLORFX_NEGATIVE:
        color_idx = NEGATIVE;
        break;
    case V4L2_COLORFX_VIVID:
        color_idx = VIVID;
        break;
    default:
        return -EINVAL;
        break;
    }

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TSCENE_MODES, COLOR_MODE_ID, color_idx, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set SYSTEM_ISP_DIGITAL_GAIN to %d, ret_value: %d.", color_idx, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_hflip( bool enable )
{
#if defined( TIMAGE ) && defined( ORIENTATION_HFLIP_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "hflip enable: %d.", enable );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TIMAGE, ORIENTATION_HFLIP_ID, enable ? ENABLE : DISABLE, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set ORIENTATION_HFLIP_ID to %d, ret_value: %d.", enable, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_vflip( bool enable )
{
#if defined( TIMAGE ) && defined( ORIENTATION_VFLIP_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "vflip enable: %d.", enable );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TIMAGE, ORIENTATION_VFLIP_ID, enable ? ENABLE : DISABLE, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set ORIENTATION_VFLIP_ID to %d, ret_value: %d.", enable, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_manual_gain( bool enable )
{
#if defined( TALGORITHMS ) && defined( AE_MODE_ID )
    int result;
    uint32_t mode = 0;
    uint32_t ret_val;

    LOG( LOG_INFO, "manual_gain enable: %d.", enable );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AE_MODE_ID, 0, COMMAND_GET, &ret_val );
    LOG( LOG_INFO, "AE_MODE_ID = %d", ret_val );
    if ( enable ) {
        if ( ret_val == AE_AUTO ) {
            mode = AE_MANUAL_GAIN;
        } else if ( ret_val == AE_MANUAL_EXPOSURE_TIME ) {
            mode = AE_FULL_MANUAL;
        } else {
            LOG( LOG_INFO, "Manual gain is already enabled." );
            return 0;
        }
    } else {
        if ( ret_val == AE_MANUAL_GAIN ) {
            mode = AE_AUTO;
        } else if ( ret_val == AE_FULL_MANUAL ) {
            mode = AE_MANUAL_EXPOSURE_TIME;
        } else {
            LOG( LOG_INFO, "Manual gain is already disabled." );
            return 0;
        }
    }

    result = acamera_command( TALGORITHMS, AE_MODE_ID, mode, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AE_MODE_ID to %u, ret_value: %d.", mode, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_gain( int gain )
{
#if defined( TALGORITHMS ) && defined( AE_GAIN_ID )
    int result;
    int gain_frac;
    uint32_t ret_val;

    LOG( LOG_INFO, "gain: %d.", gain );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AE_MODE_ID, 0, COMMAND_GET, &ret_val );
    LOG( LOG_INFO, "AE_MODE_ID = %d", ret_val );
    if ( ret_val != AE_FULL_MANUAL && ret_val != AE_MANUAL_GAIN ) {
        LOG( LOG_ERR, "Cannot set gain while AE_MODE is %d", ret_val );
        return 0;
    }

    gain_frac = gain / 100;
    gain_frac += ( gain % 100 ) * 256 / 100;

    result = acamera_command( TALGORITHMS, AE_GAIN_ID, gain_frac, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AE_GAIN_ID to %d, ret_value: %d.", gain, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_exposure_auto( int enable )
{
#if defined( TALGORITHMS ) && defined( AE_MODE_ID )
    int result;
    uint32_t mode = 0;
    uint32_t ret_val;

    LOG( LOG_INFO, "manual_exposure enable: %d.", enable );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AE_MODE_ID, 0, COMMAND_GET, &ret_val );
    LOG( LOG_CRIT, "AE_MODE_ID = %d", ret_val );
    switch ( enable ) {
    case true:
        if ( ret_val == AE_AUTO ) {
            mode = AE_MANUAL_EXPOSURE_TIME;
        } else if ( ret_val == AE_MANUAL_GAIN ) {
            mode = AE_FULL_MANUAL;
        } else {
            LOG( LOG_INFO, "Manual exposure is already enabled." );
            return 0;
        }
        break;
    case false:
        if ( ret_val == AE_MANUAL_EXPOSURE_TIME ) {
            mode = AE_AUTO;
        } else if ( ret_val == AE_FULL_MANUAL ) {
            mode = AE_MANUAL_GAIN;
        } else {
            LOG( LOG_INFO, "Manual exposure is already disabled." );
            return 0;
        }
        break;
    }

    result = acamera_command( TALGORITHMS, AE_MODE_ID, mode, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AE_MODE_ID to %u, ret_value: %d.", mode, result );
        return result;
    }
#endif

    return 0;
}

/* set exposure in us unit */
static int isp_fw_do_set_exposure( int exp )
{
#if defined( TALGORITHMS ) && defined( AE_EXPOSURE_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "exp in ms: %d.", exp );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AE_EXPOSURE_ID, exp * 1000, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AE_EXPOSURE_ID to %d, ret_value: %d.", exp, result );
        return result;
    }
#endif
    return 0;
}

static int isp_fw_do_set_variable_frame_rate( int enable )
{
    // SYSTEM_EXPOSURE_PRIORITY ??
    return 0;
}

static int isp_fw_do_set_white_balance_mode( int wb_mode )
{
#if defined( TALGORITHMS ) && defined( AWB_MODE_ID )
#if defined( ISP_HAS_AWB_MESH_FSM ) || defined( ISP_HAS_AWB_MESH_NBP_FSM ) || defined( ISP_HAS_AWB_MANUAL_FSM )
    static int32_t last_wb_request = AWB_DAY_LIGHT;
#else
    static int32_t last_wb_request = AWB_MANUAL;
#endif
    int result;
    uint32_t mode = 0;
    uint32_t ret_val;

    LOG( LOG_INFO, "new white balance request: %d.", wb_mode );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AWB_MODE_ID, 0, COMMAND_GET, &ret_val );
    LOG( LOG_CRIT, "AWB_MODE_ID = %d", ret_val );
    switch ( wb_mode ) {
    case AWB_MANUAL:
        /* we set the last mode instead of MANUAL */
        mode = last_wb_request;
        break;
    case AWB_AUTO:
        /* we set the last mode instead of MANUAL */
        if ( ret_val != AWB_AUTO ) {
            mode = wb_mode;
        } else {
            LOG( LOG_INFO, "Auto WB is already enabled." );
            return 0;
        }
        break;
#if defined( ISP_HAS_AWB_MESH_FSM ) || defined( ISP_HAS_AWB_MESH_NBP_FSM ) || defined( ISP_HAS_AWB_MANUAL_FSM )
    case AWB_DAY_LIGHT:
    case AWB_CLOUDY:
    case AWB_INCANDESCENT:
    case AWB_FLOURESCENT:
    case AWB_TWILIGHT:
    case AWB_SHADE:
    case AWB_WARM_FLOURESCENT:
        if ( ret_val != AWB_AUTO ) {
            mode = wb_mode;
        } else {
            /* wb mode is not updated when it's in auto mode */
            LOG( LOG_INFO, "Auto WB is enabled, remembering mode." );
            last_wb_request = wb_mode;
            return 0;
        }
        break;
#endif
    default:
        return -EINVAL;
    }

    result = acamera_command( TALGORITHMS, AWB_MODE_ID, mode, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AWB_MODE_ID to %u, ret_value: %d.", mode, result );
        return result;
    }
#endif
    return 0;
}

static int isp_fw_do_set_focus_auto( int enable )
{
#if defined( TALGORITHMS ) && defined( AF_MODE_ID )
    int result;
    uint32_t mode = 0;
    uint32_t ret_val;

    LOG( LOG_INFO, "new auto focus request: %d.", enable );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AF_MODE_ID, 0, COMMAND_GET, &ret_val );
    LOG( LOG_CRIT, "AF_MODE_ID = %d", ret_val );
    switch ( enable ) {
    case 1:
        /* we set the last mode instead of MANUAL */
        if ( ret_val != AF_AUTO_CONTINUOUS ) {
            mode = AF_AUTO_CONTINUOUS;
        } else {
            LOG( LOG_INFO, "Auto focus is already enabled." );
            return 0;
        }
        break;
    case 0:
        if ( ret_val != AF_MANUAL ) {
            mode = AF_MANUAL;
        } else {
            /* wb mode is not updated when it's in auto mode */
            LOG( LOG_INFO, "Auto WB is enabled, remembering mode." );
            return 0;
        }
        break;
    default:
        return -EINVAL;
    }

    result = acamera_command( TALGORITHMS, AF_MODE_ID, mode, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AF_MODE_ID to %u, ret_value: %d.", mode, result );
        return result;
    }
#endif

    return 0;
}

static int isp_fw_do_set_focus( int focus )
{
#if defined( TALGORITHMS ) && defined( AF_MANUAL_CONTROL_ID )
    int result;
    uint32_t ret_val;

    LOG( LOG_INFO, "focus: %d.", focus );

    /* some controls(such brightness) will call acamera_command()
     * before isp_fw initialed, so we need to check.
     */
    if ( !isp_started ) {
        LOG( LOG_ERR, "ISP FW not inited yet" );
        return -EBUSY;
    }

    result = acamera_command( TALGORITHMS, AF_MANUAL_CONTROL_ID, focus, COMMAND_SET, &ret_val );
    if ( result ) {
        LOG( LOG_ERR, "Failed to set AF_MANUAL_CONTROL_ID to %d, ret_value: %d.", focus, result );
        return result;
    }
#endif
    return 0;
}


/* ----------------------------------------------------------------
 * fw_interface config interface
 */
bool fw_intf_validate_control( uint32_t id )
{
    return isp_fw_do_validate_control( id );
}

int fw_intf_set_test_pattern( int val )
{
    return isp_fw_do_set_test_pattern( val );
}

int fw_intf_set_test_pattern_type( int val )
{
    return isp_fw_do_set_test_pattern_type( val );
}

int fw_intf_set_af_refocus( int val )
{
    return isp_fw_do_set_af_refocus( val );
}

int fw_intf_set_brightness( int val )
{
    return isp_fw_do_set_brightness( val );
}

int fw_intf_set_contrast( int val )
{
    return isp_fw_do_set_contrast( val );
}

int fw_intf_set_saturation( int val )
{
    return isp_fw_do_set_saturation( val );
}

int fw_intf_set_hue( int val )
{
    return isp_fw_do_set_hue( val );
}

int fw_intf_set_sharpness( int val )
{
    return isp_fw_do_set_sharpness( val );
}

int fw_intf_set_color_fx( int val )
{
    return isp_fw_do_set_color_fx( val );
}

int fw_intf_set_hflip( int val )
{
    return isp_fw_do_set_hflip( val ? 1 : 0 );
}

int fw_intf_set_vflip( int val )
{
    return isp_fw_do_set_vflip( val ? 1 : 0 );
}

int fw_intf_set_autogain( int val )
{
    /* autogain enable: disable manual gain.
     * autogain disable: enable manual gain.
     */
    return isp_fw_do_set_manual_gain( val ? 0 : 1 );
}

int fw_intf_set_gain( int val )
{
    return isp_fw_do_set_gain( val );
}

int fw_intf_set_exposure_auto( int val )
{
    return isp_fw_do_set_exposure_auto( val );
}

int fw_intf_set_exposure( int val )
{
    return isp_fw_do_set_exposure( val );
}

int fw_intf_set_variable_frame_rate( int val )
{
    return isp_fw_do_set_variable_frame_rate( val );
}

int fw_intf_set_white_balance_auto( int val )
{
#ifdef AWB_MODE_ID
    int mode;

    if ( val == true )
        mode = AWB_AUTO;
    else
        mode = AWB_MANUAL;

    return isp_fw_do_set_white_balance_mode( mode );
#endif

    // return SUCCESS for compatibility verfication issue
    return 0;
}

int fw_intf_set_white_balance( int val )
{
    int mode;

    switch ( val ) {
#ifdef AWB_MODE_ID
#if defined( ISP_HAS_AWB_MESH_FSM ) || defined( ISP_HAS_AWB_MESH_NBP_FSM ) || defined( ISP_HAS_AWB_MANUAL_FSM )
    case 8000:
        mode = AWB_SHADE;
        break;
    case 7000:
        mode = AWB_CLOUDY;
        break;
    case 6000:
    case 5000:
        mode = AWB_DAY_LIGHT;
        break;
    case 4000:
        mode = AWB_FLOURESCENT;
        break;
    case 3000:
        mode = AWB_WARM_FLOURESCENT;
        break;
    case 2000:
        mode = AWB_INCANDESCENT;
        break;
#endif
#endif
    default:
        // return SUCCESS for compatibility verfication issue
        return 0;
    }

    return isp_fw_do_set_white_balance_mode( mode );
}

int fw_intf_set_focus_auto( int val )
{
    return isp_fw_do_set_focus_auto( val );
}

int fw_intf_set_focus( int val )
{
    return isp_fw_do_set_focus( val );
}