/*
 * raster-snapshot.c
 *
 * Written by
 *  David Hogan <david.q.hogan@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "raster-snapshot.h"

#include "lib.h"
#include "raster.h"
#include "snapshot.h"
#include "videoarch.h"

#ifdef RASPI_COMPILE

#define RASTER_SNAPSHOT_SCRATCH_SIZE 256

static int raster_snapshot_external_layout(const struct draw_buffer_s *draw_buffer,
                                           const struct video_canvas_s *canvas,
                                           size_t *row_stride)
{
    unsigned int raster_skip = (unsigned int)canvas->raster_skip;

    if (draw_buffer->draw_buffer == NULL
        || draw_buffer->draw_buffer_width == 0
        || draw_buffer->draw_buffer_height == 0
        || draw_buffer->draw_buffer_pitch < draw_buffer->draw_buffer_width
        || draw_buffer->draw_buffer_height > UINT_MAX - 4
        || draw_buffer->draw_buffer_width
               > UINT_MAX / (draw_buffer->draw_buffer_height + 4)
        || (raster_skip != 1 && raster_skip != 2)
        || draw_buffer->draw_buffer_pitch > SIZE_MAX / raster_skip) {
        return -1;
    }

    *row_stride = (size_t)draw_buffer->draw_buffer_pitch * raster_skip;

    if (draw_buffer->draw_buffer_height > 1
        && *row_stride
               > (SIZE_MAX - draw_buffer->draw_buffer_width)
                     / (draw_buffer->draw_buffer_height - 1)) {
        return -1;
    }

    return 0;
}

static int raster_snapshot_write_zeros(snapshot_module_t *m, unsigned int count)
{
    static const uint8_t zeros[RASTER_SNAPSHOT_SCRATCH_SIZE] = { 0 };

    while (count > 0) {
        unsigned int chunk = count > sizeof(zeros) ? sizeof(zeros) : count;

        if (SMW_BA(m, zeros, chunk) < 0) {
            return -1;
        }
        count -= chunk;
    }

    return 0;
}

static int raster_snapshot_discard(snapshot_module_t *m, unsigned int count)
{
    uint8_t scratch[RASTER_SNAPSHOT_SCRATCH_SIZE];

    while (count > 0) {
        unsigned int chunk = count > sizeof(scratch) ? sizeof(scratch) : count;

        if (SMR_BA(m, scratch, chunk) < 0) {
            return -1;
        }
        count -= chunk;
    }

    return 0;
}

static int raster_snapshot_write_external_field(snapshot_module_t *m,
                                                 const struct draw_buffer_s *draw_buffer,
                                                 size_t row_stride)
{
    unsigned int y;
    unsigned int padding = draw_buffer->draw_buffer_width * 2;

    if (raster_snapshot_write_zeros(m, padding) < 0) {
        return -1;
    }

    for (y = 0; y < draw_buffer->draw_buffer_height; y++) {
        if (SMW_BA(m, draw_buffer->draw_buffer + (size_t)y * row_stride,
                   draw_buffer->draw_buffer_width) < 0) {
            return -1;
        }
    }

    return raster_snapshot_write_zeros(m, padding);
}

static int raster_snapshot_write_external(snapshot_module_t *m, raster_t *raster)
{
    struct video_canvas_s *canvas = raster->canvas;
    struct draw_buffer_s *draw_buffer = canvas->draw_buffer;
    size_t row_stride;

    if (raster_snapshot_external_layout(draw_buffer, canvas, &row_stride) < 0
        || SMW_DW(m, raster->current_line) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_width) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_height) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_pitch) < 0
        || raster_snapshot_write_external_field(m, draw_buffer, row_stride) < 0) {
        return -1;
    }

    /* BMX exposes one externally owned buffer, even for interlaced chips. */
    if (canvas->videoconfig->cap->interlace_allowed
        && (raster_snapshot_write_external_field(m, draw_buffer, row_stride) < 0
            || SMW_DW(m, canvas->videoconfig->interlace_field) < 0)) {
        return -1;
    }

    return 0;
}

static int raster_snapshot_read_external_field(snapshot_module_t *m,
                                                struct video_canvas_s *canvas,
                                                unsigned int saved_width,
                                                unsigned int saved_height,
                                                size_t row_stride,
                                                int restore)
{
    struct draw_buffer_s *draw_buffer = canvas->draw_buffer;
    unsigned int y;
    unsigned int padding = saved_width * 2;

    if (raster_snapshot_discard(m, padding) < 0) {
        return -1;
    }

    for (y = 0; y < saved_height; y++) {
        if (restore) {
            uint8_t *row = draw_buffer->draw_buffer + (size_t)y * row_stride;

            if (SMR_BA(m, row, saved_width) < 0) {
                return -1;
            }

            if (canvas->raster_skip == 2) {
                if (canvas->raster_lines) {
                    memset(row + draw_buffer->draw_buffer_pitch, 0, saved_width);
                } else {
                    memcpy(row + draw_buffer->draw_buffer_pitch, row, saved_width);
                }
            }
        } else if (raster_snapshot_discard(m, saved_width) < 0) {
            return -1;
        }
    }

    return raster_snapshot_discard(m, padding);
}

static int raster_snapshot_read_external(snapshot_module_t *m, raster_t *raster)
{
    struct video_canvas_s *canvas = raster->canvas;
    struct draw_buffer_s *draw_buffer = canvas->draw_buffer;
    uint32_t saved_current_line;
    uint32_t saved_width;
    uint32_t saved_height;
    uint32_t saved_pitch;
    int saved_interlace_field = 0;
    size_t row_stride = 0;
    int restore;

    if (SMR_DW(m, &saved_current_line) < 0
        || SMR_DW(m, &saved_width) < 0
        || SMR_DW(m, &saved_height) < 0
        || SMR_DW(m, &saved_pitch) < 0
        || saved_width == 0
        || saved_height == 0
        || saved_pitch < saved_width
        || saved_height > UINT_MAX - 4
        || saved_width > UINT_MAX / (saved_height + 4)) {
        return -1;
    }

    restore = !canvas->videoconfig->cap->interlace_allowed
              && saved_width == draw_buffer->draw_buffer_width
              && saved_height == draw_buffer->draw_buffer_height
              && raster_snapshot_external_layout(draw_buffer, canvas,
                                                  &row_stride) == 0;

    if (raster_snapshot_read_external_field(m, canvas, saved_width,
                                            saved_height, row_stride,
                                            restore) < 0) {
        return -1;
    }

    if (canvas->videoconfig->cap->interlace_allowed
        && (raster_snapshot_read_external_field(m, canvas, saved_width,
                                                saved_height, row_stride, 0) < 0
            || SMR_DW_INT(m, &saved_interlace_field) < 0)) {
        return -1;
    }

    /* Keep the externally owned framebuffer and its geometry unchanged. */
    raster->current_line = saved_current_line;
    if (canvas->videoconfig->cap->interlace_allowed) {
        canvas->videoconfig->interlace_field = saved_interlace_field;
    }

    return 0;
}

#endif

int raster_snapshot_write(snapshot_module_t *m, raster_t *raster)
{
    unsigned int padded_size;
    unsigned int unpadded_offset;
    struct draw_buffer_s *draw_buffer = raster->canvas->draw_buffer;

#ifdef RASPI_COMPILE
    if (raster->canvas->video_draw_buffer_callback != NULL) {
        return raster_snapshot_write_external(m, raster);
    }
#endif

    if (0
        || SMW_DW(m, raster->current_line) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_width) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_height) < 0
        || SMW_DW(m, draw_buffer->draw_buffer_pitch) < 0
        ) {
        return -1;
    }

    raster_calculate_padding_size(draw_buffer->draw_buffer_width,
                                  draw_buffer->draw_buffer_height,
                                  &padded_size,
                                  &unpadded_offset);

    if (0
        || SMW_BA(m, draw_buffer->draw_buffer_padded_allocations[0], padded_size) < 0
        ) {
        return -1;
    }

    /* If the chip supports interlaced output, store the additional field */

    if (raster->canvas->videoconfig->cap->interlace_allowed) {
        if (0
            || SMW_BA(m, draw_buffer->draw_buffer_padded_allocations[1], padded_size) < 0
            || SMW_DW(m, raster->canvas->videoconfig->interlace_field) < 0
            ) {
            return -1;
        }
    }

    return 0;
}

int raster_snapshot_read(snapshot_module_t *m, raster_t *raster)
{
    unsigned int padded_size;
    unsigned int unpadded_offset;
    struct draw_buffer_s *draw_buffer = raster->canvas->draw_buffer;

#ifdef RASPI_COMPILE
    if (raster->canvas->video_draw_buffer_callback != NULL) {
        return raster_snapshot_read_external(m, raster);
    }
#endif

    if (0
        || SMR_DW(m, &raster->current_line) < 0
        || SMR_DW(m, &draw_buffer->draw_buffer_width) < 0
        || SMR_DW(m, &draw_buffer->draw_buffer_height) < 0
        || SMR_DW(m, &draw_buffer->draw_buffer_pitch) < 0
        ) {
        return -1;
    }

    raster_calculate_padding_size(draw_buffer->draw_buffer_width,
                                  draw_buffer->draw_buffer_height,
                                  &padded_size,
                                  &unpadded_offset);

    draw_buffer->draw_buffer_padded_allocations[0] = lib_realloc(draw_buffer->draw_buffer_padded_allocations[0], padded_size);
    draw_buffer->draw_buffer_non_padded[0] = draw_buffer->draw_buffer_padded_allocations[0] + unpadded_offset;
    draw_buffer->draw_buffer = draw_buffer->draw_buffer_non_padded[0];

    if (0
        || SMR_BA(m, draw_buffer->draw_buffer_padded_allocations[0], padded_size) < 0
        ) {
        return -1;
    }

    /*
     * Interlaced video chips retain two draw buffers
     */

    if (raster->canvas->videoconfig->cap->interlace_allowed) {
        draw_buffer->draw_buffer_padded_allocations[1] = lib_realloc(draw_buffer->draw_buffer_padded_allocations[1], padded_size);
        draw_buffer->draw_buffer_non_padded[1] = draw_buffer->draw_buffer_padded_allocations[1] + unpadded_offset;

        if (0
            || SMR_BA(m, draw_buffer->draw_buffer_padded_allocations[1], padded_size) < 0
            || SMR_DW_INT(m, &raster->canvas->videoconfig->interlace_field) < 0
            ) {
            return -1;
        }

        /* Update the current draw buffer based on the interlace field */
        draw_buffer->draw_buffer = draw_buffer->draw_buffer_non_padded[raster->canvas->videoconfig->interlace_field & 1];
    }

    return 0;
}
