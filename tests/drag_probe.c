/* Test-only virtual pointer. Used against our own pet; not installed. */
#include <wayland-client.h>
#include "virtual-pointer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version) {
    (void)data;
    if(!strcmp(interface,zwlr_virtual_pointer_manager_v1_interface.name))
        manager=wl_registry_bind(registry,name,&zwlr_virtual_pointer_manager_v1_interface,version<2?version:2);
}
static void removed(void *data,struct wl_registry *registry,uint32_t name) {(void)data;(void)registry;(void)name;}
static uint32_t now(void) {struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec*1000+ts.tv_nsec/1000000;}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"usage: drag-probe dx dy | --wheel notches | --click hold-ms\n");return 2;}
    int wheel=!strcmp(argv[1],"--wheel");
    int click=!strcmp(argv[1],"--click");
    double dx=strtod(argv[1],NULL),dy=strtod(argv[2],NULL);
    struct wl_display *display=wl_display_connect(NULL);
    if(!display)return 1;
    struct wl_registry *registry=wl_display_get_registry(display);
    const struct wl_registry_listener listener={global,removed};
    wl_registry_add_listener(registry,&listener,NULL);wl_display_roundtrip(display);
    if(!manager){fprintf(stderr,"virtual pointer protocol unavailable\n");return 1;}
    struct zwlr_virtual_pointer_v1 *pointer=zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager,NULL);
    wl_display_roundtrip(display);usleep(100000);
    if(wheel) {
        zwlr_virtual_pointer_v1_motion(pointer,now(),wl_fixed_from_int(0),wl_fixed_from_int(0));
        zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(50000);
        zwlr_virtual_pointer_v1_axis_source(pointer,WL_POINTER_AXIS_SOURCE_WHEEL);
        zwlr_virtual_pointer_v1_axis_discrete(pointer,now(),WL_POINTER_AXIS_VERTICAL_SCROLL,
            wl_fixed_from_double(15*dy),(int)dy);
        zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(100000);
        zwlr_virtual_pointer_v1_destroy(pointer);wl_display_roundtrip(display);wl_display_disconnect(display);
        return 0;
    }
    zwlr_virtual_pointer_v1_motion(pointer,now(),wl_fixed_from_int(0),wl_fixed_from_int(0));
    zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(50000);
    zwlr_virtual_pointer_v1_button(pointer,now(),272,WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(80000);
    if(click)usleep((useconds_t)(dy<0?0:dy>10000?10000000:dy*1000));
    else for(int i=0;i<12;++i){
        zwlr_virtual_pointer_v1_motion(pointer,now(),wl_fixed_from_double(dx/12),wl_fixed_from_double(dy/12));
        zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(20000);
    }
    zwlr_virtual_pointer_v1_button(pointer,now(),272,WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(120000);
    zwlr_virtual_pointer_v1_destroy(pointer);wl_display_roundtrip(display);wl_display_disconnect(display);
    return 0;
}
