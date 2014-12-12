#ifndef __HW_CAMERA_HW_H__
#define __HW_CAMERA_HW_H__
#include <linux/videodev2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdbool.h>

#include <linux/videodev2.h>
#include <DebugUtils.h>

#define NB_BUFFER 6
#define NB_PIC_BUFFER 2
#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define V4L2_ROTATE_ID 0x980922  //V4L2_CID_ROTATE
typedef struct FrameV4L2Info {
	struct	v4l2_format format;
	struct	v4l2_buffer buf;
	struct	v4l2_requestbuffers rb;
}FrameV4L2Info;

struct VideoInfo {
		struct	v4l2_capability cap;
		FrameV4L2Info preview;
		FrameV4L2Info picture;
        void    *mem[NB_BUFFER];
		void    *mem_pic[NB_PIC_BUFFER];
        unsigned int canvas[NB_BUFFER];
        bool isStreaming;
		bool isPicture;
        bool canvas_mode;
        int width;
        int height;
        int formatIn;
        int framesizeIn;
        uint32_t idVendor;
        uint32_t idProduct;

        int idx;
        int fd;
};

extern int camera_open(struct VideoInfo *cam_dev);
extern void camera_close(struct VideoInfo *vinfo);
extern int setBuffersFormat(struct VideoInfo *cam_dev);
extern int start_capturing(struct VideoInfo *vinfo);
extern int start_picture(struct VideoInfo *vinfo,int rotate);
extern void stop_picture(struct VideoInfo *vinfo);
extern void releasebuf_and_stop_picture(struct VideoInfo *vinfo);
extern int stop_capturing(struct VideoInfo *vinfo);
extern int releasebuf_and_stop_capturing(struct VideoInfo *vinfo);

extern uintptr_t get_frame_phys(struct VideoInfo *vinfo);

extern void *get_frame(struct VideoInfo *vinfo);
extern void *get_picture(struct VideoInfo *vinfo);
extern int putback_frame(struct VideoInfo *vinfo);
#endif
