#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct device
{
    int fd;
    drmModeRes *resources;
    drmModePlaneRes *plane_resources;
};

struct object
{
    uint32_t id;
    uint32_t count_props;
    drmModePropertyPtr *props;
};

static struct device *device_open(const char *path)
{
    const int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open() failed: %s\n", strerror(errno));
        return NULL;
    }

    if (!drmIsKMS(fd)) {
        fprintf(stderr, "%s is not a KMS device\n", path);
        goto close_fd;
    }

    if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        fprintf(stderr, "drmSetClientCap(DRM_CLIENT_CAP_ATOMIC) failed\n");
        goto close_fd;
    }

	drmModeRes *res = drmModeGetResources(fd);
	if (res == NULL) {
		fprintf(stderr, "drmModeGetResources() failed\n");
		goto close_fd;
	}

    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
    if (plane_res == NULL) {
        fprintf(stderr, "drmModeGetPlaneResources() failed\n");
        goto free_resources;
    }

    struct device *device = calloc(1, sizeof(*device));
    device->fd = fd;
    device->resources = res;
    device->plane_resources = plane_res;

    return device;

free_resources:
    drmModeFreeResources(res);

close_fd:
    close(fd);

    return NULL;
}

static void device_close(struct device *device)
{
    close(device->fd);

    drmModeFreePlaneResources(device->plane_resources);
    drmModeFreeResources(device->resources);

    free(device);
}

static struct object *object_get(struct device *device, uint32_t object_id, uint32_t object_type)
{
    drmModeObjectPropertiesPtr object_props = drmModeObjectGetProperties(device->fd, object_id, object_type);
    if (object_props == NULL) {
        return NULL;
    }

    drmModePropertyPtr *props = calloc(object_props->count_props, sizeof(drmModePropertyPtr));
    for (uint32_t j = 0; j < object_props->count_props; ++j) {
        props[j] = drmModeGetProperty(device->fd, object_props->props[j]);
    }

    struct object *object = calloc(1, sizeof(*object));
    object->id = object_id;
    object->count_props = object_props->count_props;
    object->props = props;

    drmModeFreeObjectProperties(object_props);
    return object;
}

static void object_free(struct object *obj)
{
    for (uint32_t j = 0; j < obj->count_props; ++j) {
        drmModeFreeProperty(obj->props[j]);
    }

    free(obj->props);
    free(obj);
}

static drmModePropertyPtr object_find_property(struct object *obj, const char *name)
{
    for (uint32_t i = 0; i < obj->count_props; ++i) {
        if (!obj->props[i]) {
            continue;
        }
        if (strcmp(obj->props[i]->name, name) == 0) {
            return obj->props[i];
        }
    }
    return NULL;
}

static void add_property(drmModeAtomicReqPtr req, struct object *obj, const char *name, uint64_t value)
{
    const drmModePropertyPtr prop = object_find_property(obj, name);
    if (prop == NULL) {
        return;
    }

    const int ret = drmModeAtomicAddProperty(req, obj->id, prop->prop_id, value);
    if (ret < 0) {
        fprintf(stderr, "failed to set %s property: %s\n", name, strerror(-ret));
    }
}

static const char usage[] =
    "Usage: drm_monitor [options...]\n"
    "\n"
    "  -d              Specify DRM device (default /dev/dri/card0).\n"
    "  -h              Show help message and quit.\n";

static char *find_primary_node()
{
    int device_count = drmGetDevices2(0, NULL, 0);
    if (device_count <= 0) {
        return NULL;
    }

    drmDevice **devices = calloc(device_count, sizeof(drmDevice *));
    if (!devices) {
        return NULL;
    }

    device_count = drmGetDevices2(0, devices, device_count);
    if (device_count < 0) {
        fprintf(stderr, "drmGetDevices2() failed: %s", strerror(-device_count));
        free(devices);
        return NULL;
    }

    char *path = NULL;
    for (int i = 0; i < device_count; ++i) {
        if (devices[i]->available_nodes & (1 << DRM_NODE_PRIMARY)) {
            path = strdup(devices[i]->nodes[DRM_NODE_PRIMARY]);
            break;
        }
    }

    drmFreeDevices(devices, device_count);
    free(devices);

    return path;
}

int main(int argc, char *argv[])
{
    char *device_path = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "hd:")) != -1) {
        switch (opt) {
	case 'h':
	    printf("%s", usage);
	    return EXIT_SUCCESS;
	case 'd':
	    device_path = optarg;
	    break;
	default:
	    return EXIT_FAILURE;
	}
    }

    if (!device_path) {
        device_path = find_primary_node();
        if (!device_path) {
            return 0;
        }
    }

    struct device *device = device_open(device_path);
    if (device == NULL) {
        return EXIT_FAILURE;
    }

    drmModeAtomicReq *req = drmModeAtomicAlloc();

    for (int i = 0; i < device->resources->count_connectors; ++i) {
        struct object *obj = object_get(device, device->resources->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
        if (obj == NULL) {
            continue;
        }

        add_property(req, obj, "CRTC_ID", 0);

        add_property(req, obj, "Colorspace", 0);
        add_property(req, obj, "HDR_OUTPUT_METADATA", 0);

        object_free(obj);
    }

    for (int i = 0; i < device->resources->count_crtcs; ++i) {
        struct object *obj = object_get(device, device->resources->crtcs[i], DRM_MODE_OBJECT_CRTC);
        if (obj == NULL) {
            continue;
        }

        add_property(req, obj, "ACTIVE", 0);
        add_property(req, obj, "MODE_ID", 0);

        add_property(req, obj, "GAMMA_LUT", 0);
        add_property(req, obj, "DEGAMMA_LUT", 0);
        add_property(req, obj, "CTM", 0);
        add_property(req, obj, "VRR_ENABLED", 0);
        add_property(req, obj, "OUT_FENCE_PTR", 0);
        add_property(req, obj, "AMD_CRTC_REGAMMA_TF", 0);

        object_free(obj);
    }

    for (uint32_t i = 0; i < device->plane_resources->count_planes; ++i) {
        struct object *obj = object_get(device, device->plane_resources->planes[i], DRM_MODE_OBJECT_PLANE);
        if (obj == NULL) {
            continue;
        }

        add_property(req, obj, "FB_ID", 0);
        add_property(req, obj, "IN_FENCE_FD", -1);
        add_property(req, obj, "CRTC_ID", 0);
        add_property(req, obj, "SRC_X", 0);
        add_property(req, obj, "SRC_Y", 0);
        add_property(req, obj, "SRC_W", 0);
        add_property(req, obj, "SRC_H", 0);
        add_property(req, obj, "CRTC_X", 0);
        add_property(req, obj, "CRTC_Y", 0);
        add_property(req, obj, "CRTC_W", 0);
        add_property(req, obj, "CRTC_H", 0);

        add_property(req, obj, "rotation", DRM_MODE_ROTATE_0);
        add_property(req, obj, "alpha", 0xffff);
        // add_property(fd, plane_id, "zpos", )

        add_property(req, obj, "AMD_PLANE_DEGAMMA_TF", 0);
        add_property(req, obj, "AMD_PLANE_DEGAMMA_LUT", 0);
        add_property(req, obj, "AMD_PLANE_CTM", 0);
        add_property(req, obj, "AMD_PLANE_HDR_MULT", 0x100000000ULL);
        add_property(req, obj, "AMD_PLANE_SHAPER_TF", 0);
        add_property(req, obj, "AMD_PLANE_SHAPER_LUT", 0);
        add_property(req, obj, "AMD_PLANE_LUT3D", 0);
        add_property(req, obj, "AMD_PLANE_BLEND_TF", 0);
        add_property(req, obj, "AMD_PLANE_BLEND_LUT", 0);

        object_free(obj);
    }

    int ret = drmModeAtomicCommit(device->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    if (ret != 0) {
        fprintf(stderr, "drmModeAtomicCommit() failed: %s\n", strerror(-ret));
    }

    device_close(device);
    sleep(1); // otherwise Xorg may start with a black screen, seems like a race condition

    return EXIT_SUCCESS;
}
