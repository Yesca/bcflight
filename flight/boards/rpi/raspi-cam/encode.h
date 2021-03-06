/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bcm_host.h>
#include <IL/OMX_Video.h>
#include <IL/OMX_Types.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Core.h>
#include <IL/OMX_Broadcom.h>

#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP

// #define HD
// #define PREV_RSZ
// #define DUAL_ENCODERS
// #define CAM_RSZ

#ifdef HD
	#define FPS       30
	#define WIDTH     1280
	#define HEIGHT    ((WIDTH)*9/16)
	#define PREV_BITRATE   1024 * 2
	#define PREV_WIDTH     640
	#define PREV_HEIGHT    ((PREV_WIDTH)*9/16)
#else
// 	#define FPS       50
// 	#define FPS       30
// 	#define WIDTH     640
// 	#define WIDTH     1280
// 	#define HEIGHT    720
// 	#define PREV_BITRATE   (1152)
// 	#define PREV_WIDTH     640
	#define PREV_WIDTH     1280
	#define PREV_HEIGHT    720
#endif

#define PITCH     ((WIDTH+31)&~31)
#define HEIGHT16  ((HEIGHT+15)&~15)
#define SIZE      ((WIDTH * HEIGHT16 * 3)/2)

#define PREV_PITCH     ((PREV_WIDTH+31)&~31)
#define PREV_HEIGHT16  ((PREV_HEIGHT+15)&~15)
#define PREV_SIZE      ((PREV_WIDTH * PREV_HEIGHT16 * 3)/2)

#define CAM_DEVICE_NUMBER               0
#define CAM_SHARPNESS                   100                       // -100 .. 100
#define CAM_CONTRAST                    0                        // -100 .. 100
#define CAM_BRIGHTNESS                  58                       // 0 .. 100
#define CAM_SATURATION                  32                        // -100 .. 100
#define CAM_EXPOSURE_VALUE_COMPENSATION 00
#define CAM_EXPOSURE_ISO_SENSITIVITY    800
#define CAM_EXPOSURE_AUTO_SENSITIVITY   OMX_TRUE
#define CAM_FRAME_STABILISATION         OMX_FALSE //OMX_TRUE
#define CAM_WHITE_BALANCE_CONTROL       OMX_WhiteBalControlAuto // OMX_WHITEBALCONTROLTYPE
// #define CAM_IMAGE_FILTER                OMX_ImageFilterNoise    // OMX_IMAGEFILTERTYPE
#define CAM_IMAGE_FILTER                OMX_ImageFilterNone    // OMX_IMAGEFILTERTYPE
#define CAM_FLIP_HORIZONTAL             OMX_FALSE
#define CAM_FLIP_VERTICAL               OMX_TRUE

typedef struct video_context {
	int running;
	int exiting;
	pthread_mutex_t lock;
	pthread_mutex_t mutex_enc1;
	pthread_mutex_t mutex_enc2;
	pthread_cond_t cond_enc1;
	pthread_cond_t cond_enc2;
	// Handles
// 	OMX_HANDLETYPE clk;
	OMX_HANDLETYPE cam;
	OMX_HANDLETYPE spl;
	OMX_HANDLETYPE rsz;
	OMX_HANDLETYPE enc1;
	OMX_HANDLETYPE enc2;
	OMX_HANDLETYPE wrm;
	OMX_HANDLETYPE nll;
	// Camera
	int camera_ready;
	OMX_BUFFERHEADERTYPE* cambufs;
	uint32_t brightness;
	int32_t contrast;
	int32_t saturation;
	// Encoders
	OMX_BUFFERHEADERTYPE* enc1bufs;
	OMX_BUFFERHEADERTYPE* enc2bufs;
	int enc1_data_avail;
	int enc2_data_avail;
} video_context;

video_context* video_configure( uint32_t fps, uint32_t live_width, uint32_t live_height, uint32_t live_kbps );
void video_start( video_context* ctx );
void video_stop( video_context* ctx );
void video_start_recording( video_context* ctx, const char* filename );
void video_set_brightness( video_context* ctx, uint32_t value );
void video_recover( video_context* ctx );
uint32_t video_get_crc( OMX_HANDLETYPE enc );
