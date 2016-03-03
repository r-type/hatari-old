/* Copyright  (C) 2010-2015 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (rpng.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <formats/rpng.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GEKKO
#include <malloc.h>
#endif

#include "rpng_common.h"
#include "rpng_decode_common.h"

static bool read_chunk_header(uint8_t *buf, struct png_chunk *chunk)
{
   unsigned i;
   uint8_t dword[4] = {0};

   for (i = 0; i < 4; i++)
      dword[i] = buf[i];

   buf += 4;

   chunk->size = dword_be(dword);

   for (i = 0; i < 4; i++)
      chunk->type[i] = buf[i];

   buf += 4;

   return true;
}

static bool png_parse_ihdr(uint8_t *buf,
      struct png_ihdr *ihdr)
{
   unsigned i;
   bool ret = true;

   buf += 4 + 4;

   ihdr->width       = dword_be(buf + 0);
   ihdr->height      = dword_be(buf + 4);
   ihdr->depth       = buf[8];
   ihdr->color_type  = buf[9];
   ihdr->compression = buf[10];
   ihdr->filter      = buf[11];
   ihdr->interlace   = buf[12];

   if (ihdr->width == 0 || ihdr->height == 0)
      GOTO_END_ERROR();

   if (ihdr->color_type == 2 ||
         ihdr->color_type == 4 || ihdr->color_type == 6)
   {
      if (ihdr->depth != 8 && ihdr->depth != 16)
         GOTO_END_ERROR();
   }
   else if (ihdr->color_type == 0)
   {
      static const unsigned valid_bpp[] = { 1, 2, 4, 8, 16 };
      bool correct_bpp = false;

      for (i = 0; i < ARRAY_SIZE(valid_bpp); i++)
      {
         if (valid_bpp[i] == ihdr->depth)
         {
            correct_bpp = true;
            break;
         }
      }

      if (!correct_bpp)
         GOTO_END_ERROR();
   }
   else if (ihdr->color_type == 3)
   {
      static const unsigned valid_bpp[] = { 1, 2, 4, 8 };
      bool correct_bpp = false;

      for (i = 0; i < ARRAY_SIZE(valid_bpp); i++)
      {
         if (valid_bpp[i] == ihdr->depth)
         {
            correct_bpp = true;
            break;
         }
      }

      if (!correct_bpp)
         GOTO_END_ERROR();
   }
   else
      GOTO_END_ERROR();

#ifdef RPNG_TEST
   fprintf(stderr, "IHDR: (%u x %u), bpc = %u, palette = %s, color = %s, alpha = %s, adam7 = %s.\n",
         ihdr->width, ihdr->height,
         ihdr->depth, ihdr->color_type == 3 ? "yes" : "no",
         ihdr->color_type & 2 ? "yes" : "no",
         ihdr->color_type & 4 ? "yes" : "no",
         ihdr->interlace == 1 ? "yes" : "no");
#endif

   if (ihdr->compression != 0)
      GOTO_END_ERROR();

#if 0
   if (ihdr->interlace != 0) /* No Adam7 supported. */
      GOTO_END_ERROR();
#endif

end:
   return ret;
}

static bool png_realloc_idat(const struct png_chunk *chunk, struct idat_buffer *buf)
{
   uint8_t *new_buffer = (uint8_t*)realloc(buf->data, buf->size + chunk->size);

   if (!new_buffer)
      return false;

   buf->data  = new_buffer;
   return true;
}

static bool png_read_plte_into_buf(uint8_t *buf,
      uint32_t *buffer, unsigned entries)
{
   unsigned i;

   if (entries > 256)
      return false;

   buf += 8;

   for (i = 0; i < entries; i++)
   {
      uint32_t r = buf[3 * i + 0];
      uint32_t g = buf[3 * i + 1];
      uint32_t b = buf[3 * i + 2];
      buffer[i] = (r << 16) | (g << 8) | (b << 0) | (0xffu << 24);
   }

   return true;
}

bool rpng_nbio_load_image_argb_iterate(uint8_t *buf, struct rpng_t *rpng)
{
   unsigned i;

   rpng->chunk.size = 0;
   rpng->chunk.type[0] = '\0';

   if (!read_chunk_header(buf, &rpng->chunk))
      return false;

#if 0
   for (i = 0; i < 4; i++)
   {
      fprintf(stderr, "chunktype: %c\n", chunk.type[i]);
   }
#endif

   switch (png_chunk_type(&rpng->chunk))
   {
      case PNG_CHUNK_NOOP:
      default:
         break;

      case PNG_CHUNK_ERROR:
         return false;

      case PNG_CHUNK_IHDR:
         if (rpng->has_ihdr || rpng->has_idat || rpng->has_iend)
            return false;

         if (rpng->chunk.size != 13)
            return false;

         if (!png_parse_ihdr(buf, &rpng->ihdr))
            return false;

         rpng->has_ihdr = true;
         break;

      case PNG_CHUNK_PLTE:
         {
            unsigned entries = rpng->chunk.size / 3;

            if (!rpng->has_ihdr || rpng->has_plte || rpng->has_iend || rpng->has_idat)
               return false;

            if (rpng->chunk.size % 3)
               return false;

            if (!png_read_plte_into_buf(buf, rpng->palette, entries))
               return false;

            rpng->has_plte = true;
         }
         break;

      case PNG_CHUNK_IDAT:
         if (!(rpng->has_ihdr) || rpng->has_iend || (rpng->ihdr.color_type == 3 && !(rpng->has_plte)))
            return false;

         if (!png_realloc_idat(&rpng->chunk, &rpng->idat_buf))
            return false;

         buf += 8;

         for (i = 0; i < rpng->chunk.size; i++)
            rpng->idat_buf.data[i + rpng->idat_buf.size] = buf[i];

         rpng->idat_buf.size += rpng->chunk.size;

         rpng->has_idat = true;
         break;

      case PNG_CHUNK_IEND:
         if (!(rpng->has_ihdr) || !(rpng->has_idat))
            return false;

         rpng->has_iend = true;
         return false;
   }

   return true;
}

bool rpng_nbio_load_image_argb_process(struct rpng_t *rpng,
      uint32_t **data, unsigned *width, unsigned *height)
{
   if (!rpng)
      return false;

   rpng->process.inflate_buf_size = 0;
   rpng->process.inflate_buf      = NULL;

   if (inflateInit(&rpng->process.stream) != Z_OK)
      return false;

   png_pass_geom(&rpng->ihdr, rpng->ihdr.width,
         rpng->ihdr.height, NULL, NULL, &rpng->process.inflate_buf_size);
   if (rpng->ihdr.interlace == 1) /* To be sure. */
      rpng->process.inflate_buf_size *= 2;

   rpng->process.inflate_buf = (uint8_t*)malloc(rpng->process.inflate_buf_size);
   if (!rpng->process.inflate_buf)
      return false;

   rpng->process.stream.next_in   = rpng->idat_buf.data;
   rpng->process.stream.avail_in  = rpng->idat_buf.size;
   rpng->process.stream.avail_out = rpng->process.inflate_buf_size;
   rpng->process.stream.next_out  = rpng->process.inflate_buf;

   if (inflate(&rpng->process.stream, Z_FINISH) != Z_STREAM_END)
   {
      inflateEnd(&rpng->process.stream);
      return false;
   }
   inflateEnd(&rpng->process.stream);

   *width  = rpng->ihdr.width;
   *height = rpng->ihdr.height;
#ifdef GEKKO
   /* we often use these in textures, make sure they're 32-byte aligned */
   *data = (uint32_t*)memalign(32, rpng->ihdr.width *
         rpng->ihdr.height * sizeof(uint32_t));
#else
   *data = (uint32_t*)malloc(rpng->ihdr.width *
         rpng->ihdr.height * sizeof(uint32_t));
#endif
   if (!*data)
      return false;

   if (!png_reverse_filter_loop(rpng, data))
      return false;

   return true;
}

void rpng_nbio_load_image_free(struct rpng_t *rpng)
{
   if (!rpng)
      return;

   if (rpng->idat_buf.data)
      free(rpng->idat_buf.data);
   if (rpng->process.inflate_buf)
      free(rpng->process.inflate_buf);

   if (rpng)
      free(rpng);
}

bool rpng_nbio_load_image_argb_start(struct rpng_t *rpng)
{
   unsigned i;
   char header[8];

   if (!rpng)
      return false;

   for (i = 0; i < 8; i++)
      header[i] = rpng->buff_data[i];

   if (memcmp(header, png_magic, sizeof(png_magic)) != 0)
      return false;

   rpng->buff_data += 8;

   return true;
}
