#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>
#include <string.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GL/gl.h>


static int gpu = 0;

static int connector_id = -1;
drmModeModeInfo mode;
drmModeCrtc* crtc = NULL;

static EGLDisplay display = NULL;
static EGLContext context = NULL;
static EGLSurface surface = NULL;
static struct gbm_device* gbm_dev = NULL;
static struct gbm_surface* gbm_surf = NULL;

void setup_opengl(int device, drmModeModeInfo* mode);
void swap_buffers();
void draw(float progress);

int main()
{
    char inBuffer[3];

    FILE* outfd = fopen("/tmp/drm.log", "w");
//    outfd = stdout;
    stdout = outfd;

    gpu = open("/dev/dri/card0", O_RDWR|O_CLOEXEC);

    if(gpu < 0) {
        perror("Could not open DRM device");
//        fprintf(stderr, "Could not open DRM device\n");
        return 1;
    }

    drmModeRes* res;
    drmModeConnector* conn = NULL;
    drmModeEncoder* encoder = NULL;

    memset(&mode, 0, sizeof(drmModeModeInfo));

    res = drmModeGetResources(gpu);

    if(!res) {
        perror("Could not initialize DRM resources");
//        fprintf(stderr, "Could not initialize DRM resources %d\n", gpu);
        return 1;
    }

    for (int i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(gpu, res->connectors[i]);
        if (!conn) {
            fprintf(outfd, "cannot retrieve DRM connector %u:%u (%d): %m\n",
                i, res->connectors[i], errno);
            continue;
        }

        if(conn->connection == DRM_MODE_CONNECTED) {
            fprintf(outfd, "[%d] %d, type:%d\n", i, conn->connector_id, conn->connector_type);
        } else {
            continue;
        }

        drmModeFreeConnector(conn);
    }
    printf("Select connector: ");
    fgets(inBuffer, 3, stdin);

    {
    int connector_num = atoi(inBuffer);
    conn = drmModeGetConnector(gpu, res->connectors[connector_num]);
    }

    fprintf(outfd, "Connector: %d selected\n", conn->connector_id);

    fprintf(outfd, "Available modes:\n");

    for (int i = 0; i < conn->count_modes; i++) {
        fprintf(outfd, "[%d] %ux%u@%u\n", i, conn->modes[i].hdisplay, conn->modes[i].vdisplay, conn->modes[i].vrefresh);
    }

    printf("Select mode: ");
    fgets(inBuffer, 3, stdin);
    {
        int mode_num = atoi(inBuffer);
        memcpy(&mode, &(conn->modes[mode_num]), sizeof(drmModeModeInfo));
    }

    fprintf(outfd, "Selected mode %ux%u@%u\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);
    connector_id = conn->connector_id;

    encoder = drmModeGetEncoder(gpu, conn->encoder_id);
    if(!encoder) {
        fprintf(outfd, "Unable to find encoder %d for connector %d\n", conn->encoder_id, conn->connector_id);
        fprintf(outfd, "Enumerate existing encoders:\n");
        for (int i = 0; i < conn->count_encoders; i++) {
            fprintf(outfd, "[%i] %d\n", i, conn->encoders[i]);
        }
        printf("Select encoder: ");
        fgets(inBuffer, 3, stdin);
        {
            int encoder_num = atoi(inBuffer);
            encoder = drmModeGetEncoder(gpu, conn->encoders[encoder_num]);
        }
    }

    if(!encoder) {
        fprintf(outfd, "Unable to find encoder exit!\n");
        return 1;
    }

    fprintf(outfd, "Initializing CRTC: %d\n",encoder->crtc_id);
    crtc = drmModeGetCrtc(gpu, encoder->crtc_id);
    if(!crtc) {
        fprintf(outfd, "CRTC is not available\n");
        fprintf(outfd, "Enumerate existing CRTC's:\n");
        for (int i = 0; i < res->count_crtcs; i++) {
            if (!(encoder->possible_crtcs & (1 << i)))
                continue;
            fprintf(outfd, "[%i] %d\n", i, res->crtcs[i]);
        }
        printf("Select crtc: ");
        fgets(inBuffer, 3, stdin);
        {
            int crtc_num = atoi(inBuffer);
            crtc = drmModeGetCrtc(gpu, res->crtcs[crtc_num]);
        }
    }

    if(!crtc) {
        fprintf(outfd, "CRTC is not available exit!\n");
        return 1;
    }

    setup_opengl(gpu, &mode);

    int i;
    for (i = 0; i < 6000; i++)
        draw (i / 6000.0f);

    drmModeFreeEncoder(encoder);
    encoder = NULL;

    drmModeFreeConnector(conn);
    conn = NULL;

    drmModeFreeResources(res);

    fflush(outfd);
    sleep(5);
    close(gpu);
    fprintf(outfd, "Exit normally\n");
    fclose(outfd);
    return 0;
}

void setup_opengl(int device, drmModeModeInfo* mode) {
    gbm_dev = gbm_create_device (device);
    display = eglGetDisplay (gbm_dev);
    eglInitialize (display, NULL, NULL);

    // create an OpenGL context
    eglBindAPI (EGL_OPENGL_API);
    EGLint attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
    EGL_NONE};
    EGLConfig config;
    EGLint num_config;
    eglChooseConfig (display, attributes, &config, 1, &num_config);
    context = eglCreateContext (display, config, EGL_NO_CONTEXT, NULL);

    // create the GBM and EGL surface
    gbm_surf = gbm_surface_create (gbm_dev, mode->hdisplay, mode->vdisplay, GBM_FORMAT_RGBA8888, GBM_BO_USE_SCANOUT|GBM_BO_USE_RENDERING);
    surface = eglCreateWindowSurface (display, config, gbm_surf, NULL);
    eglMakeCurrent (display, surface, surface, context);
}
static struct gbm_bo *previous_bo = NULL;
static uint32_t previous_fb;

void swap_buffers() {
    eglSwapBuffers(display, surface);
    struct gbm_bo *bo = gbm_surface_lock_front_buffer (gbm_surf);
    uint32_t handle = gbm_bo_get_handle (bo).u32;
    uint32_t pitch = gbm_bo_get_stride (bo);
    uint32_t fb;
    drmModeAddFB(gpu, mode.hdisplay, mode.vdisplay, 24, 32, pitch, handle, &fb);
    drmModeSetCrtc(gpu, crtc->crtc_id, fb, 0, 0, &connector_id, 1, &mode);

    if (previous_bo) {
        drmModeRmFB(gpu, previous_fb);
        gbm_surface_release_buffer(gbm_surf, previous_bo);
    }
    previous_bo = bo;
    previous_fb = fb;
}

void draw(float progress) {
    glClearColor (1.0f-progress, progress, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    swap_buffers();
}
