#include "bitmap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

size_t imv_bitmap_size(struct imv_bitmap bmp)
{
  if (bmp.width <= 0 || bmp.height <= 0 ||
      (size_t)bmp.width > SIZE_MAX / bmp.height / BYTES_PER_CHANNEL) {
    return 0;
  }
  return (size_t)bmp.width * bmp.height * BYTES_PER_CHANNEL;
}

struct imv_bitmap imv_bitmap_alloc(int32_t width, int32_t height)
{
  struct imv_bitmap result = {
      .width = width,
      .height = height,
      .data = NULL,
  };

  size_t size = imv_bitmap_size(result);
  if (size) {
    result.data = malloc(size);
  }

  return result;
}

void imv_bitmap_free(struct imv_bitmap bmp) { free(bmp.data); }
