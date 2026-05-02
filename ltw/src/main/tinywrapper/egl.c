/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */
#include "egl.h"
#include "unordered_map/int_hash.h"
#include "string_utils.h"
#include "env.h"
#include <string.h>
#include <stdlib.h>

thread_local context_t *current_context;
unordered_map* context_map;

EGLContext (*host_eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean (*host_eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
EGLBoolean (*host_eglMakeCurrent) (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);

void init_egl() {
    context_map = alloc_intmap();
    host_eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext,
                                            const EGLint *)) host_eglGetProcAddress("eglCreateContext");
    host_eglDestroyContext = (EGLBoolean (*)(EGLDisplay, EGLContext)) host_eglGetProcAddress(
            "eglDestroyContext");
    host_eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface,
                                          EGLContext)) host_eglGetProcAddress("eglMakeCurrent");
}

static bool init_context(context_t* tw_context) {
    tw_context->shader_map = alloc_intmap_safe();
    if(!tw_context->shader_map) goto fail;
    tw_context->framebuffer_map = alloc_intmap_safe();
    if(!tw_context->framebuffer_map) goto fail_dealloc;
    tw_context->program_map = alloc_intmap_safe();
    if(!tw_context->program_map) goto fail_dealloc;
    tw_context->texture_swztrack_map = alloc_intmap_safe();
    if(!tw_context->texture_swztrack_map) goto fail_dealloc;
    for(int i = 0; i < MAX_BOUND_BASEBUFFERS; i++) {
        unordered_map *map = alloc_intmap_safe();
        if(!map) goto fail_dealloc;
        tw_context->bound_basebuffers[i] = map;
    }
    return true;

    fail_dealloc:
    for(int i = 0; i < MAX_BOUND_BASEBUFFERS; i++) {
        unordered_map *map = tw_context->bound_basebuffers[i];
        if(map) unordered_map_free(map);
    }
    if(tw_context->shader_map)
        unordered_map_free(tw_context->shader_map);
    if(tw_context->framebuffer_map)
        unordered_map_free(tw_context->framebuffer_map);
    if(tw_context->program_map)
        unordered_map_free(tw_context->program_map);
    if(tw_context->texture_swztrack_map)
        unordered_map_free(tw_context->texture_swztrack_map);
    fail:
    return false;
}

static void free_context(context_t* tw_context) {
    unordered_map_free(tw_context->shader_map);
    unordered_map_free(tw_context->program_map);
    unordered_map_free(tw_context->framebuffer_map);
    unordered_map_free(tw_context->texture_swztrack_map);
    if(tw_context->extensions_string != NULL) free(tw_context->extensions_string);
    if(tw_context->nextras != 0 && tw_context->extra_extensions_array != NULL) {
        for(int i = 0; i < tw_context->nextras; i++) {
            free((tw_context->extra_extensions_array[i]));
        }
        free(tw_context->extra_extensions_array);
    }
}

void init_extra_extensions(context_t* context, int* length) {
    const char* es_extensions = (const char*)es3_functions.glGetString(GL_EXTENSIONS);
    if(!es_extensions) {
        es_extensions = "";
    }
    *length = (int)strlen(es_extensions);
    context->extensions_string = malloc(*length + 1);
    if(!context->extensions_string) {
        printf("LTW: Failed to allocate extensions_string\n");
        *length = 0;
        return;
    }
    memcpy(context->extensions_string, es_extensions, *length+1);
}

// FIXED: Mali-safe extension adding with proper memory management
void add_extra_extension(context_t* context, int* length, const char* extension)  {
    if(!context || !extension || !length) return;
    
    size_t extension_len = strlen(extension);
    
    // FIXED: Limit extension name length for Mali compatibility
    if(extension_len > 256) {
        printf("LTW: Extension name too long for Mali: %zu (skipping %s)\n", extension_len, extension);
        return;
    }
    
    // FIXED: Allocate instead of VLA to prevent stack overflow
    char* str_append_extension = (char*)malloc(extension_len + 2);
    if(!str_append_extension) {
        printf("LTW: Failed to allocate memory for extension append\n");
        return;
    }
    
    memcpy(str_append_extension, extension, extension_len);
    str_append_extension[extension_len] = ' ';
    str_append_extension[extension_len + 1] = 0;
    
    char* old_extensions = context->extensions_string;
    context->extensions_string = gl4es_append(context->extensions_string, length, str_append_extension);
    
    if(!context->extensions_string) {
        printf("LTW: Failed to append extension string\n");
        context->extensions_string = old_extensions;
        free(str_append_extension);
        return;
    }

    int extension_idx = context->nextras++;
    char** new_array = (char**)realloc(context->extra_extensions_array, sizeof(char*)*context->nextras);
    if(!new_array) {
        printf("LTW: Failed to reallocate extensions array\n");
        context->nextras--;
        free(str_append_extension);
        return;
    }
    context->extra_extensions_array = new_array;
    
    char* extra_extension = (char*)malloc(extension_len + 1);
    if(!extra_extension) {
        printf("LTW: Failed to allocate extra_extension\n");
        context->nextras--;
        free(str_append_extension);
        return;
    }
    
    memcpy(extra_extension, extension, extension_len + 1);
    context->extra_extensions_array[extension_idx] = extra_extension;
    
    free(str_append_extension);
}

void fin_extra_extensions(context_t* context, int length) {
    if(!context || !context->extensions_string) return;
    if(length < 2) return;
    
    if(context->extensions_string[length-2] != ' ') return;
    char* orig_string = context->extensions_string;
    context->extensions_string = (char*)realloc(context->extensions_string, length - 1);
    if(context->extensions_string == NULL) {
        free(orig_string);
        return;
    }
    context->extensions_string[length-2] = 0;
}

void build_extension_string(context_t* context) {
    if(!context) return;
    int length;
    init_extra_extensions(context, &length);
    if(context->buffer_storage) {
        if(!env_istrue("LTW_HIDE_BUFFER_STORAGE"))
            add_extra_extension(context, &length, "GL_ARB_buffer_storage");
        else printf("LTW: The buffer storage extension is hidden.\n");
    }
    if(context->buffer_texture_ext || context->es32) {
        add_extra_extension(context, &length, "GL_ARB_texture_buffer_object");
    }
    add_extra_extension(context, &length, "GL_ARB_draw_elements_base_vertex");
    // Required by Iris. Indexed variants are available since ES3.2 or with OES/EXT_draw_buffers_indexed extensions
    if(context->blending.available)
        add_extra_extension(context, &length, "GL_ARB_draw_buffers_blend");
    // Used by Minecraft for the GPU usage counter (see Blaze3D TimerQuery)
    add_extra_extension(context, &length, "GL_ARB_timer_query");
    // More extensions are possible, but will need way more wraps and tracking.
    fin_extra_extensions(context, length);
}

// FIXED: Better ES version detection with full ES 3.2 support
static void find_esversion(context_t* context) {
    const char* version = (const char*) es3_functions.glGetString(GL_VERSION);
    const char* shader_version = (const char*) es3_functions.glGetString(GL_SHADING_LANGUAGE_VERSION);

    if(!version || !shader_version) {
        printf("LTW: Failed to get GL version strings\n");
        goto fail;
    }

    int esmajor = 0, esminor = 0, shadermajor = 3, shaderminor = 0;
    sscanf(version, " OpenGL ES %i.%i", &esmajor, &esminor);
    sscanf(shader_version, " OpenGL ES GLSL ES %i.%i", &shadermajor, &shaderminor);
    context->shader_version = shadermajor * 100 + shaderminor;
    printf("LTW: Running on OpenGL ES %i.%i with ESSL %i\n", esmajor, esminor, context->shader_version);
    
    if(esmajor == 0 && esminor == 0) goto fail;
    if(esmajor < 3 || context->shader_version < 300) {
        printf("LTW: Unsupported OpenGL ES version. This will cause you problems down the line.\n");
        return;
    }
    
    // FIXED: Proper ES version detection
    if(esmajor == 3) {
        context->es31 = esminor >= 1;
        context->es32 = esminor >= 2;
    } else if(esmajor > 3) {
        context->es31 = true;
        context->es32 = true;
    }
    
    if(context->es32) {
        printf("LTW: OpenGL ES 3.2 detected - full feature support enabled\n");
    } else if(context->es31) {
        printf("LTW: OpenGL ES 3.1 detected - most features available\n");
    }

    const char* extensions = (const char*) es3_functions.glGetString(GL_EXTENSIONS);
    if(extensions) {
        if(strstr(extensions, "GL_EXT_buffer_storage")) context->buffer_storage = true;
        if(strstr(extensions, "GL_EXT_texture_buffer")) context->buffer_texture_ext = true;
        if(strstr(extensions, "GL_EXT_multi_draw_indirect")) context->multidraw_indirect = true;

        // EXT_disjoint_timer_query provides accurate int64 timer queries
        // on Core Profile it's ARB_timer_query instead
        // This enables real time queries via mentioned extension, otherwise faked ones are used (see query.c)
        if(strstr(extensions, "GL_EXT_disjoint_timer_query") || env_istrue_d("LTW_ENABLE_TIMER_QUERY", false)) context->timer_query = true;

        bool basevertex_oes = strstr(extensions, "GL_OES_draw_elements_base_vertex") != NULL;
        bool basevertex_ext = strstr(extensions, "GL_EXT_draw_elements_base_vertex") != NULL;
        if(context->es32) {
            context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertex;
        }
        else if(basevertex_oes) context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexOES;
        else if(basevertex_ext) context->drawelementsbasevertex = es3_functions.glDrawElementsBaseVertexEXT;
        else context->drawelementsbasevertex = NULL;

        bool drawbuffersi_oes = strstr(extensions, "GL_OES_draw_buffers_indexed") != NULL;
        bool drawbuffersi_ext = strstr(extensions, "GL_EXT_draw_buffers_indexed") != NULL;
        blending_functions_t* blend = &context->blending;
        blend->available = true;
#define SET_FUNC(type) \
        blend->blendequationi = es3_functions.glBlendEquationi ## type; \
        blend->blendequationseparatei = es3_functions.glBlendEquationSeparatei ## type; \
        blend->blendfunci = es3_functions.glBlendFunci ## type; \
        blend->blendfuncseparatei = es3_functions.glBlendFuncSeparatei ## type; \
        blend->colormaski = es3_functions.glColorMaski ## type; \

        if(context->es32){
            SET_FUNC()
        }
        else if(drawbuffersi_oes){
            SET_FUNC(OES)
        }
        else if(drawbuffersi_ext){
            SET_FUNC(EXT)
        }
        else {
            blend->available = false;
        }
#undef SET_FUNC
    }
    
    build_extension_string(context);

    return;
    fail:
    printf("LTW: Failed to detect OpenGL ES version");
}

void basevertex_init(context_t* context);
void buffer_copier_init(context_t* context);
static void init_incontext(context_t* tw_context) {
    es3_functions.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tw_context->maxTextureSize);
    es3_functions.glGetIntegerv(GL_MAX_DRAW_BUFFERS, &tw_context->max_drawbuffers);
    es3_functions.glGetIntegerv(GL_NUM_EXTENSIONS, &tw_context->nextensions_es);
    if(tw_context->max_drawbuffers > MAX_DRAWBUFFERS) {
        tw_context->max_drawbuffers = MAX_DRAWBUFFERS;
    }

    find_esversion(tw_context);

    basevertex_init(tw_context);
    buffer_copier_init(tw_context);
    es3_functions.glGenBuffers(1, &tw_context->multidraw_element_buffer);
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    EGLContext phys_context = host_eglCreateContext(dpy, config, share_context, attrib_list);
    if(phys_context == EGL_NO_CONTEXT) return phys_context;
    context_t* tw_context = calloc(1, sizeof(context_t));
    if(tw_context == NULL || !init_context(tw_context)) {
        if(tw_context) free(tw_context);
        host_eglDestroyContext(dpy, phys_context);
        return EGL_NO_CONTEXT;
    }
    unordered_map_put(context_map, phys_context, tw_context);
    return phys_context;
}

EGLBoolean eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
    if(!host_eglDestroyContext(dpy, ctx)) return EGL_FALSE;
    context_t* old_ctx = unordered_map_remove(context_map, ctx);
    if(old_ctx) {
        free_context(old_ctx);
        free(old_ctx);
    }
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if(!host_eglMakeCurrent(dpy, draw, read, ctx)) return EGL_FALSE;
    if(ctx == EGL_NO_CONTEXT) {
        current_context = NULL;
        return EGL_TRUE;
    }
    context_t* tw_context = unordered_map_get(context_map, ctx);
    if(tw_context == NULL) {
        printf("LTW: Failed to find context %p\n", ctx);
        abort();
    }
    if(!tw_context->context_rdy) {
        init_incontext(tw_context);
        tw_context->context_rdy = true;
    }
    current_context = tw_context;
    return EGL_TRUE;
}
