#include <fcntl.h> 
#include <stddef.h>
#include <xf86drm.h>
#include <drm/drm_fourcc.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <malloc.h>

#include <linux/memfd.h>

#include <gbm.h>
#include "gbm_backend_abi.h"

#include <hybris/gralloc/gralloc.h>

#include <hardware/gralloc.h>

#include <assert.h>

#define DRM_EVDI_GBM_ADD_BUFF 0x05
#define DRM_EVDI_GBM_DEL_BUFF 0x0B
#define DRM_EVDI_GBM_CREATE_BUFF 0x0C

#define DRM_IOCTL_EVDI_GBM_DEL_BUFF DRM_IOWR(DRM_COMMAND_BASE +  \
	DRM_EVDI_GBM_DEL_BUFF, struct drm_evdi_gbm_del_buff)

#define DRM_IOCTL_EVDI_GBM_ADD_BUFF DRM_IOWR(DRM_COMMAND_BASE +  \
	DRM_EVDI_GBM_ADD_BUFF, struct drm_evdi_gbm_add_buf)

#define DRM_IOCTL_EVDI_GBM_CREATE_BUFF DRM_IOWR(DRM_COMMAND_BASE +  \
	DRM_EVDI_GBM_CREATE_BUFF, struct drm_evdi_gbm_create_buff)

struct drm_evdi_gbm_add_buf {
	int fd;
	int id;
};

struct drm_evdi_gbm_del_buff {
	int id;
};

struct gbm_hybris_bo {
   struct gbm_bo base;
   buffer_handle_t handle;
   int evdi_lindroid_buff_id;
   uint32_t pixel_stride;   /* gralloc stride in pixels; used by bo_map */
};

struct gbm_hybris_surface {
    struct gbm_surface base;
    struct gbm_hybris_bo *front_bo;
    bool front_locked;
    struct gbm_hybris_bo *bo[16];
    unsigned int bo_count;
};

struct drm_evdi_gbm_create_buff {
	int *id;
	uint32_t *stride;
	uint32_t format;
	uint32_t width;
	uint32_t height;
};

static const struct gbm_core *core;

struct gbm_surface *hybris_gbm_surface_create(struct gbm_device *gbm,
					      uint32_t width, uint32_t height,
					      uint32_t format, uint32_t flags,
					      const uint64_t *modifiers,
					      const unsigned count);

int memfd_create(const char *name, unsigned int flags);

struct gbm_hybris_bo *gbm_hybris_bo(struct gbm_bo *bo)
{
   return (struct gbm_hybris_bo *) bo;
}

static void hybris_gbm_destroy_kernel_bo(struct gbm_hybris_bo *bo)
{
    struct drm_evdi_gbm_del_buff close_args;

    if (!bo)
        return;

    (void)close_args;
    if (bo->handle) {
        hybris_gralloc_release(bo->handle, 1);
        bo->handle = NULL;
    }
    bo->evdi_lindroid_buff_id = -1;
}

static int get_hal_pixel_format(uint32_t gbm_format)
{
    int format;

    switch (gbm_format) {
    case GBM_FORMAT_ABGR8888:
        format = HAL_PIXEL_FORMAT_RGBA_8888;
        break;
    case GBM_FORMAT_XBGR8888:
        format = HAL_PIXEL_FORMAT_RGBX_8888;
        break;
    case GBM_FORMAT_RGB888:
        format = HAL_PIXEL_FORMAT_RGB_888;
        break;
    case GBM_FORMAT_RGB565:
        format = HAL_PIXEL_FORMAT_RGB_565;
        break;
    case GBM_FORMAT_ARGB8888:
        format = HAL_PIXEL_FORMAT_BGRA_8888;
        break;
    case GBM_FORMAT_GR88:
        /* GR88 corresponds to YV12 which is planar */
        format = HAL_PIXEL_FORMAT_YV12;
        break;
    case GBM_FORMAT_ABGR16161616F:
        format = HAL_PIXEL_FORMAT_RGBA_FP16;
        break;
    case GBM_FORMAT_ABGR2101010:
        format = HAL_PIXEL_FORMAT_RGBA_1010102;
        break;
    default:
        format = HAL_PIXEL_FORMAT_RGBA_8888; // Invalid or unsupported format assume RGBA8888
        break;
    }

    return format;
}

/* Formats we can actually back with a gralloc buffer (mirror of the explicit
 * cases in get_hal_pixel_format; the default RGBA8888 there is a fallback, not
 * a claim of support). */
static bool is_known_gbm_format(uint32_t gbm_format)
{
    switch (gbm_format) {
    case GBM_FORMAT_ABGR8888:
    case GBM_FORMAT_XBGR8888:
    case GBM_FORMAT_RGB888:
    case GBM_FORMAT_RGB565:
    case GBM_FORMAT_ARGB8888:
    case GBM_FORMAT_GR88:
    case GBM_FORMAT_ABGR16161616F:
    case GBM_FORMAT_ABGR2101010:
        return true;
    default:
        return false;
    }
}

/* Bytes per pixel for the mappable single-plane formats above. Used only to
 * compute the CPU map stride/offset in bo_map; the scanout stride in
 * base.v0.stride is left untouched. */
static uint32_t hybris_bytes_per_pixel(uint32_t gbm_format)
{
    switch (gbm_format) {
    case GBM_FORMAT_ABGR16161616F:
        return 8;
    case GBM_FORMAT_RGB888:
        return 3;
    case GBM_FORMAT_RGB565:
    case GBM_FORMAT_GR88:
        return 2;
    case GBM_FORMAT_ABGR8888:
    case GBM_FORMAT_XBGR8888:
    case GBM_FORMAT_ARGB8888:
    case GBM_FORMAT_ABGR2101010:
    default:
        return 4;
    }
}

int hybris_gbm_bo_get_fd(struct gbm_bo* _bo);

// Dummy func to identify hybris gdb_device/bo/surface
static struct gbm_device *gbm_device_hybris(int x)
{
    return NULL;
}

struct gbm_bo* hybris_gbm_bo_create(struct gbm_device* device, uint32_t width, uint32_t height, uint32_t format, uint32_t flags, const uint64_t *modifiers, const unsigned int count) {
    if (!device) {
        errno = EINVAL;
        fprintf(stderr, "[libgbm-hybris] Invalid GBM device.\n");
        return NULL;
    }

    if (device->v0.fd < 0 || !core) {
        errno = EINVAL;
        fprintf(stderr, "[libgbm-hybris] Invalid GBM backend state.\n");
        return NULL;
    }

    struct gbm_hybris_bo *bo = calloc(1, sizeof(struct gbm_hybris_bo));
    if (!bo) {
        errno = ENOMEM;
        fprintf(stderr, "[libgbm-hybris] Failed to allocate memory for GBM buffer object.\n");
        return NULL;
    }

    bo->evdi_lindroid_buff_id = -1;
    bo->base.v0.user_data = NULL;

    format = core->v0.format_canonicalize(format);

    bo->base.gbm = device;

    bo->base.v0.width = width;
    bo->base.v0.height = height;
    bo->base.v0.format = format;

    uint32_t stride = 0;
    uint64_t byte_stride;
    struct drm_evdi_gbm_create_buff cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.width = width;
    cmd.height = height;
    cmd.format = get_hal_pixel_format(format);
    cmd.stride = &stride;
    cmd.id = &bo->evdi_lindroid_buff_id;
    (void)cmd; (void)device;
    int aret = hybris_gralloc_allocate(width, height, get_hal_pixel_format(format),
                 0x100|0x200|0x800|0x1000,
                 (buffer_handle_t*)&bo->handle, &stride);
    if (aret != 0 || !bo->handle || stride == 0) {
        fprintf(stderr, "[libgbm-hybris] hybris_gralloc_allocate failed ret=%d stride=%u\n", aret, stride);
        free(bo);
        return NULL;
    }

    byte_stride = (uint64_t)stride * 4u;
    if (byte_stride == 0 || byte_stride > UINT32_MAX) {
        fprintf(stderr, "[libgbm-hybris] Computed byte stride overflow: stride=%u\n", stride);
        hybris_gbm_destroy_kernel_bo(bo);
        free(bo);
        errno = EOVERFLOW;
        return NULL;
    }

    bo->base.v0.stride = (uint32_t)byte_stride;
    bo->pixel_stride = stride;

    bo->base.v0.handle.u32 = (uint32_t)bo->evdi_lindroid_buff_id;
    return &bo->base;
}

static void hybris_gbm_bo_destroy(struct gbm_bo *_bo)
{
    if (!_bo)
        return;

    struct gbm_hybris_bo *bo = gbm_hybris_bo(_bo);
    hybris_gbm_destroy_kernel_bo(bo);
    free(bo);
}

static void hybris_gbm_device_destroy(struct gbm_device *device)
{
    free(device);
}

struct gbm_bo *hybris_gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format,
                             const uint64_t *modifiers,
                             const unsigned int count)
{
   /* Force linear: ignore modifier list and allocate a normal BO */
   return hybris_gbm_bo_create(gbm, width, height, format, 0, NULL, 0);
}

struct gbm_bo * hybris_gbm_bo_create_with_modifiers2(struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format, const uint64_t *modifiers, const unsigned int count, uint32_t flags){
    /* Force linear: ignore modifier list and allocate a normal BO */
    return hybris_gbm_bo_create(gbm, width, height, format, flags, NULL, 0);
}

struct gbm_bo *hybris_gbm_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer, uint32_t usage){
// How do that even work with fake dma buf's?
   printf("[libgbm-hybris] gbm_bo_import called\n");
   return NULL;
}

// Suprisingly not part of libgbm
uint32_t hybris_gbm_bo_get_stride(struct gbm_bo* bo, int plane) {
    // x4 the stride, as it's checked by drm and drm expexcts stride to be at very least width*bpp
    return bo ? (uint32_t)(bo->v0.stride) : 0;
}

uint32_t hybris_gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane)
{
    if (!bo) {
        errno = EINVAL;
        return 0;
    }
    if (plane != 0) {
        errno = EINVAL;
        return 0;
    }
    return hybris_gbm_bo_get_stride(bo, plane);
}

uint64_t hybris_gbm_bo_get_modifier(struct gbm_bo* bo) {
    return DRM_FORMAT_MOD_LINEAR;
}

void* hybris_gbm_bo_map(struct gbm_bo *_bo, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t flags, uint32_t *stride, void **map_data) {
    struct gbm_hybris_bo *bo = gbm_hybris_bo(_bo);

    if (!bo || !bo->handle || !stride || !map_data) {
        errno = EINVAL;
        return NULL;
    }

    /* Translate GBM transfer flags to gralloc SW usage. */
    int usage = 0;
    if (flags & GBM_BO_TRANSFER_READ)
        usage |= GRALLOC_USAGE_SW_READ_OFTEN;
    if (flags & GBM_BO_TRANSFER_WRITE)
        usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;
    if (usage == 0)
        usage = GRALLOC_USAGE_SW_READ_OFTEN;

    void *vaddr = NULL;
    int ret = hybris_gralloc_lock(bo->handle, usage,
                                  (int)x, (int)y, (int)width, (int)height, &vaddr);
    if (ret != 0 || !vaddr) {
        fprintf(stderr, "[libgbm-hybris] gralloc_lock failed ret=%d\n", ret);
        errno = EIO;
        return NULL;
    }

    /* gralloc maps the whole buffer starting at (0,0); GBM wants a pointer to
     * the (x,y) origin and the byte stride of the mapped region. Compute the
     * real byte stride here (pixel_stride * bpp), independent of the x4 scanout
     * stride in base.v0.stride. */
    uint32_t bpp = hybris_bytes_per_pixel(bo->base.v0.format);
    uint32_t byte_stride = bo->pixel_stride * bpp;
    *stride = byte_stride;
    /* map_data is opaque to the caller; unmap re-derives the handle from the bo,
     * so a non-NULL token is all that's needed here. */
    *map_data = vaddr;
    return (uint8_t *)vaddr + (size_t)y * byte_stride + (size_t)x * bpp;
}

void hybris_gbm_surface_destroy(struct gbm_surface *surf) {
    struct gbm_hybris_surface *hsurf = (struct gbm_hybris_surface *)surf;
    int i;

    if (!hsurf)
        return;

    // We own nothing
    free(hsurf);
}

int hybris_gbm_surface_has_free_buffers(struct gbm_surface *surface)
{
    struct gbm_hybris_surface *hsurf = (struct gbm_hybris_surface *)surface;

    if(hsurf->front_locked)
        return 1;

    return 0;
}

struct gbm_bo* hybris_gbm_surface_lock_front_buffer(struct gbm_surface* surface) {
    struct gbm_hybris_surface *hsurf = (struct gbm_hybris_surface *)surface;

    if (!hsurf || !hsurf->front_bo) {
        errno = EAGAIN;
        return NULL;
    }

    if (hsurf->front_locked) {
        errno = EAGAIN;
        return NULL;
    }

    hsurf->front_locked = true;
    return &hsurf->front_bo->base;
}

void hybris_gbm_surface_release_buffer(struct gbm_surface* surface, struct gbm_bo* bo) {
    struct gbm_hybris_surface *hsurf = (struct gbm_hybris_surface *)surface;

    if (!hsurf || !bo)
        return;

    if (hsurf->front_bo == (struct gbm_hybris_bo *)bo)
        hsurf->front_locked = false;
}

int hybris_gbm_bo_get_fd(struct gbm_bo* _bo) {
    if(!_bo) {
        errno = EINVAL;
        printf("[libgbm-hybris] gbm_bo_get_fd missing bo\n");
        return -1;
    }

    struct gbm_hybris_bo *bo = gbm_hybris_bo(_bo);
    if(!bo) {
        errno = EINVAL;
        printf("[libgbm-hybris] gbm_bo_get_fd missing bo->handle\n");
        return -1;
    }

    if (!_bo->gbm || _bo->gbm->v0.fd < 0) {
        errno = EBADF;
        printf("[libgbm-hybris] invalid gbm device/fd\n");
        return -1;
    }

    if (!bo->handle) {
        errno = EINVAL;
        printf("[libgbm-hybris] missing gralloc handle\n");
        return -1;
    }

    if (!bo->handle || bo->handle->numFds < 1) {
        errno = EINVAL;
        printf("[libgbm-hybris] gralloc handle has no dmabuf fd\n");
        return -1;
    }
    int fd = dup(bo->handle->data[0]);
    if (fd < 0) { printf("[libgbm-hybris] dup dmabuf failed\n"); return -1; }
    return fd;
}

static union gbm_bo_handle hybris_gbm_bo_get_handle_for_plane(struct gbm_bo *_bo, int plane)
{
    union gbm_bo_handle handle;
    handle.u32 = _bo->v0.handle.u32;
    return handle;
}

int hybris_gbm_bo_get_plane_count(struct gbm_bo *bo)
{
    return 1;
}

int hybris_gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane)
{
    if (plane != 0) {
        fprintf(stderr, "[libgbm-hybris] Error: requested plane %d, only 0 is supported\n", plane);
        errno = EINVAL;
        return -1;
    }

    return hybris_gbm_bo_get_fd(bo);
}

uint32_t hybris_bo_get_offset(struct gbm_bo *bo, int plane)
{
//   printf("[libgbm-hybris] gbm_bo_get_offset called\n");
   return 0;
}

struct gbm_surface *hybris_gbm_surface_create_with_modifiers(struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format, const uint64_t *modifiers, const unsigned int count){
   printf("[libgbm-hybris] gbm_surface_create_with_modifiers\n");
   if ((count && !modifiers) || (modifiers && !count)) {
      errno = EINVAL;
      return NULL;
   }

   return hybris_gbm_surface_create(gbm, width, height, format, 0, modifiers, count);
}

struct gbm_surface *hybris_gbm_surface_create(struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags, const uint64_t *modifiers, const unsigned count) {
    struct gbm_hybris_surface *surf;
    uint32_t canon_format = format;

    printf("[libgbm-hybris] gbm_surface_create called with width: %u, height: %u, format: %u, flags: %u\n", width, height, format, flags);

    surf = calloc(1, sizeof *surf);
    if (surf == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (core && core->v0.format_canonicalize) {
        canon_format = core->v0.format_canonicalize(format);
    }

    surf->base.gbm = gbm;
    surf->base.v0.width = width;
    surf->base.v0.height = height;
    surf->base.v0.format = canon_format;
    surf->base.v0.flags = flags;
    surf->base.v0.modifiers = NULL;
    surf->base.v0.count = 0;

    if (count) {
	// Force linear
        surf->base.v0.modifiers = calloc(1, sizeof(uint64_t));
        if (!surf->base.v0.modifiers) {
            errno = ENOMEM;
            free(surf);
            return NULL;
        }
        surf->base.v0.modifiers[0] = DRM_FORMAT_MOD_LINEAR;
        surf->base.v0.count = 1;
    }

    return &surf->base;
}

void hybris_gbm_bo_unmap(struct gbm_bo* _bo, void* map_data) {
    struct gbm_hybris_bo *bo = gbm_hybris_bo(_bo);
    (void)map_data;   /* map_data was the gralloc vaddr, not a heap pointer */
    if (bo && bo->handle)
        hybris_gralloc_unlock(bo->handle);
}

int hybris_gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count){
    return 0;
}

char *hybris_gbm_format_get_name(uint32_t gbm_format, struct gbm_format_name_desc *desc)
{
//TBD
   //gbm_format = gbm_format_canonicalize(gbm_format);
//   printf("[libgbm-hybris] gbm_format_get_name called\n");
   desc->name[0] = 0;
   desc->name[1] = 0;
   desc->name[2] = 0;
   desc->name[3] = 0;
   desc->name[4] = 0;

   return desc->name;
}

/* Mesa's GBM frontend calls these two through the dispatch table with no NULL
 * guard (gbm.c gbm_device_is_format_supported / _get_format_modifier_plane_count),
 * so leaving the slots NULL segfaults any app that probes formats/modifiers.
 * Provide honest answers for our linear, single-plane, gralloc-backed formats. */
static int hybris_gbm_is_format_supported(struct gbm_device *gbm,
                                          uint32_t format, uint32_t usage)
{
    (void)gbm; (void)usage;
    if (core && core->v0.format_canonicalize)
        format = core->v0.format_canonicalize(format);
    return is_known_gbm_format(format) ? 1 : 0;
}

static int hybris_gbm_get_format_modifier_plane_count(struct gbm_device *gbm,
                                                      uint32_t format,
                                                      uint64_t modifier)
{
    (void)gbm;
    if (core && core->v0.format_canonicalize)
        format = core->v0.format_canonicalize(format);
    if (!is_known_gbm_format(format))
        return 0;
    if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID)
        return 0;
    return 1;   /* all supported formats are single-plane linear */
}

static struct gbm_device *hybris_device_create(int fd, uint32_t gbm_backend_version){
  //  printf("[libgbm-hybris] hybris_device_create called\n");
    struct gbm_device *device;

    if (gbm_backend_version != GBM_BACKEND_ABI_VERSION) {
        printf("Wrong gbm version, built for: %d current: %d\n", GBM_BACKEND_ABI_VERSION, gbm_backend_version);
        return NULL;
    }

    device = calloc(1, sizeof *device);
    if (!device)
       return NULL;
   hybris_gralloc_initialize(0);

   device->dummy = gbm_device_hybris;
   device->v0.fd = fd;
   device->v0.name = "hybris";
   device->v0.backend_version = gbm_backend_version;
   device->v0.bo_create = hybris_gbm_bo_create;
   device->v0.bo_destroy = hybris_gbm_bo_destroy;
   device->v0.destroy = hybris_gbm_device_destroy;
   device->v0.bo_get_fd = hybris_gbm_bo_get_fd;
   device->v0.bo_get_handle = hybris_gbm_bo_get_handle_for_plane;
   device->v0.bo_get_stride = hybris_gbm_bo_get_stride;
   device->v0.bo_get_modifier = hybris_gbm_bo_get_modifier;
   device->v0.bo_get_planes = hybris_gbm_bo_get_plane_count;
   device->v0.bo_get_plane_fd = hybris_gbm_bo_get_fd_for_plane;
   device->v0.surface_create = hybris_gbm_surface_create;
   device->v0.surface_destroy = hybris_gbm_surface_destroy;
   device->v0.surface_lock_front_buffer = hybris_gbm_surface_lock_front_buffer;
   device->v0.surface_release_buffer = hybris_gbm_surface_release_buffer;
   device->v0.surface_has_free_buffers = hybris_gbm_surface_has_free_buffers;
   device->v0.bo_get_offset = hybris_bo_get_offset;
   device->v0.bo_write = hybris_gbm_bo_write;
   /* Previously-NULL slots — leaving these unset segfaulted apps that probe
    * formats or map/import BOs (Mesa calls them unguarded). */
   device->v0.is_format_supported = hybris_gbm_is_format_supported;
   device->v0.get_format_modifier_plane_count = hybris_gbm_get_format_modifier_plane_count;
   device->v0.bo_import = hybris_gbm_bo_import;
   device->v0.bo_map = hybris_gbm_bo_map;
   device->v0.bo_unmap = hybris_gbm_bo_unmap;
   return device;
}

struct gbm_backend gbm_hybris_backend = {
   .v0.backend_version = GBM_BACKEND_ABI_VERSION,
   .v0.backend_name = "hybris",
   .v0.create_device = hybris_device_create,
};

struct gbm_backend * gbmint_get_backend(const struct gbm_core *gbm_core);

struct gbm_backend *
gbmint_get_backend(const struct gbm_core *gbm_core) {
   core = gbm_core;
   return &gbm_hybris_backend;
};
