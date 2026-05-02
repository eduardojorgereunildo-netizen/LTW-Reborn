/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "proc.h"
#include "egl.h"
#include <string.h>
#include "libraryinternal.h"
#include "env.h"
//#include <GL/glext.h>

#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46

// FIXED: Debug flag for texture swizzle operations
static bool swizzle_debug = false;

static void swizzle_process_bgra(GLenum* swizzle) {
    if(!swizzle) return;
    GLenum red_src = swizzle[0];
    GLenum blue_src = swizzle[2];
    swizzle[0] = blue_src;
    swizzle[2] = red_src;
    if(swizzle_debug) {
        printf("LTW Swizzle: Applied BGRA transform: R=%x B=%x\n", red_src, blue_src);
    }
}

static void swizzle_process_endianness(GLenum* swizzle) {
    if(!swizzle) return;
    GLenum orig_swizzle[4];
    memcpy(orig_swizzle, swizzle, 4 * sizeof(GLenum));
    swizzle[0] = orig_swizzle[3];
    swizzle[1] = orig_swizzle[2];
    swizzle[2] = orig_swizzle[1];
    swizzle[3] = orig_swizzle[0];
    if(swizzle_debug) {
        printf("LTW Swizzle: Applied endianness transform: RGBA(%x,%x,%x,%x) -> (%x,%x,%x,%x)\n",
            orig_swizzle[0], orig_swizzle[1], orig_swizzle[2], orig_swizzle[3],
            swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
    }
}

static texture_swizzle_track_t* get_swizzle_track(GLenum target) {
    if(!current_context) return NULL;
    
    GLint texture;
    GLenum getter = get_textarget_query_param(target);
    if(getter == 0) {
        if(swizzle_debug) printf("LTW Swizzle: Invalid texture target: 0x%x\n", target);
        return NULL;
    }
    
    es3_functions.glGetIntegerv(getter, &texture);
    texture_swizzle_track_t* track = unordered_map_get(current_context->texture_swztrack_map, (void*)(intptr_t)texture);
    
    if(track == NULL) {
        track = malloc(sizeof(texture_swizzle_track_t));
        if(!track) {
            printf("LTW Swizzle ERROR: Failed to allocate swizzle track\n");
            return NULL;
        }
        
        // FIXED: Initialize default swizzles properly
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_R, (GLint*)&track->original_swizzle[0]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_G, (GLint*)&track->original_swizzle[1]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_B, (GLint*)&track->original_swizzle[2]);
        es3_functions.glGetTexParameteriv(target, GL_TEXTURE_SWIZZLE_A, (GLint*)&track->original_swizzle[3]);
        
        // FIXED: Initialize flags to false
        track->goofy_byte_order = false;
        track->upload_bgra = false;
        
        if(swizzle_debug) {
            printf("LTW Swizzle: Created new track for texture %u, defaults: R=%x G=%x B=%x A=%x\n",
                texture, track->original_swizzle[0], track->original_swizzle[1], 
                track->original_swizzle[2], track->original_swizzle[3]);
        }
        
        unordered_map_put(current_context->texture_swztrack_map, (void*)(intptr_t)texture, track);
    }
    return track;
}

static void apply_swizzles(GLenum target, texture_swizzle_track_t* track) {
    if(!track) return;
    
    GLenum new_swizzle[4];
    memcpy(new_swizzle, track->original_swizzle, 4 * sizeof(GLenum));
    
    if(swizzle_debug) {
        printf("LTW Swizzle: Applying transforms (endian=%d, bgra=%d) to: R=%x G=%x B=%x A=%x\n",
            track->goofy_byte_order, track->upload_bgra,
            new_swizzle[0], new_swizzle[1], new_swizzle[2], new_swizzle[3]);
    }
    
    // FIXED: Apply transformations in correct order
    if(track->goofy_byte_order) swizzle_process_endianness(new_swizzle);
    if(track->upload_bgra) swizzle_process_bgra(new_swizzle);
    
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, new_swizzle[0]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, new_swizzle[1]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, new_swizzle[2]);
    es3_functions.glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, new_swizzle[3]);
    
    if(swizzle_debug) {
        printf("LTW Swizzle: Applied result: R=%x G=%x B=%x A=%x\n",
            new_swizzle[0], new_swizzle[1], new_swizzle[2], new_swizzle[3]);
    }
}

INTERNAL void swizzle_process_upload(GLenum target, GLenum* format, GLenum* type) {
    if(!current_context || !format || !type) return;
    
    texture_swizzle_track_t* track = get_swizzle_track(target);
    if(track == NULL) return;
    
    bool apply_upload_bgra = false;
    bool apply_goofy_order = false;
    
    GLenum orig_format = *format;
    GLenum orig_type = *type;
    
    // FIXED: Detect and handle BGRA format
    if((*format) == GL_BGRA_EXT) {
        apply_upload_bgra = true;
        *format = GL_RGBA;
        if(swizzle_debug) printf("LTW Swizzle: Detected BGRA format, converting to RGBA\n");
    }
    
    // FIXED: Handle special type encodings
    if((*type) == 0x8035) {  // GL_UNSIGNED_INT_8_8_8_8_REV or similar
        apply_goofy_order = true;
        *type = GL_UNSIGNED_BYTE;
        if(swizzle_debug) printf("LTW Swizzle: Detected special type 0x8035, treating as goofy byte order\n");
    }
    
    if((*type) == 0x8367) {  // GL_UNSIGNED_SHORT_1_5_5_5_REV or similar
        *type = GL_UNSIGNED_BYTE;
        if(swizzle_debug) printf("LTW Swizzle: Detected special type 0x8367, converting to UNSIGNED_BYTE\n");
    }
    
    // FIXED: Only update if state changed
    if(apply_goofy_order != track->goofy_byte_order || apply_upload_bgra != track->upload_bgra) {
        if(swizzle_debug) {
            printf("LTW Swizzle: State change detected (goofy: %d->%d, bgra: %d->%d)\n",
                track->goofy_byte_order, apply_goofy_order,
                track->upload_bgra, apply_upload_bgra);
        }
        track->goofy_byte_order = apply_goofy_order;
        track->upload_bgra = apply_upload_bgra;
        apply_swizzles(target, track);
    } else if(swizzle_debug && (orig_format != GL_RGBA || orig_type != GL_UNSIGNED_BYTE)) {
        printf("LTW Swizzle: No state change for format=0x%x type=0x%x\n", orig_format, orig_type);
    }
}

INTERNAL void swizzle_process_swizzle_param(GLenum target, GLenum swizzle_param, const GLenum* swizzle) {
    if(!current_context || !swizzle) return;
    
    // FIXED: Validate swizzle parameter
    switch (swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
        case GL_TEXTURE_SWIZZLE_RGBA:
            break;
        default:
            if(swizzle_debug) printf("LTW Swizzle: Invalid swizzle parameter: 0x%x\n", swizzle_param);
            return;
    }
    
    texture_swizzle_track_t* track = get_swizzle_track(target);
    if(track == NULL) {
        if(swizzle_debug) printf("LTW Swizzle: Failed to get track for swizzle parameter 0x%x\n", swizzle_param);
        return;
    }
    
    switch(swizzle_param) {
        case GL_TEXTURE_SWIZZLE_R:
        case GL_TEXTURE_SWIZZLE_G:
        case GL_TEXTURE_SWIZZLE_B:
        case GL_TEXTURE_SWIZZLE_A:
            track->original_swizzle[swizzle_param - GL_TEXTURE_SWIZZLE_R] = *swizzle;
            if(swizzle_debug) {
                printf("LTW Swizzle: Set swizzle component %u to %x\n",
                    swizzle_param - GL_TEXTURE_SWIZZLE_R, *swizzle);
            }
            apply_swizzles(target, track);
            break;
        case GL_TEXTURE_SWIZZLE_RGBA:
            memcpy(track->original_swizzle, swizzle, 4 * sizeof(GLenum));
            if(swizzle_debug) {
                printf("LTW Swizzle: Set RGBA swizzle to R=%x G=%x B=%x A=%x\n",
                    swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
            }
            apply_swizzles(target, track);
            break;
    }
}

// FIXED: Initialize debug flag from environment
__attribute__((constructor)) void init_swizzle_debug() {
    swizzle_debug = env_istrue("LTW_DEBUG_SWIZZLE");
    if(swizzle_debug) {
        printf("LTW: Texture swizzle debug logging enabled\n");
    }
}
